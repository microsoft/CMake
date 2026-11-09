#!/usr/bin/env python3
"""Collect and summarize CMake artifacts without executing artifact commands."""

import argparse
import collections
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import shlex
import shutil
import subprocess
import sys


COMMAND_ROLES = {"compile", "link", "custom"}
ROOT_ROLES = {"configure", "generate", "cmakeBuild", "build"}


def read_json(path):
    with path.open(encoding="utf-8-sig") as stream:
        return json.load(stream)


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def version(value):
    if isinstance(value, int) and not isinstance(value, bool):
        return value, 0
    if isinstance(value, dict):
        major, minor = value.get("major"), value.get("minor", 0)
        if all(isinstance(v, int) and not isinstance(v, bool) for v in (major, minor)):
            return major, minor
    raise ValueError(f"Invalid instrumentation version: {value!r}")


def relative_path(name):
    if not isinstance(name, str) or not name:
        raise ValueError(f"Invalid artifact reference: {name!r}")
    posix = PurePosixPath(name.replace("\\", "/"))
    windows = PureWindowsPath(name)
    if posix.is_absolute() or windows.drive or windows.root or ".." in posix.parts:
        raise ValueError(f"Artifact reference must stay inside its data directory: {name}")
    return Path(*posix.parts)


def resolve_reference(index_path, name, flattened=False):
    relative = relative_path(name)
    candidates = [index_path.parent / relative]
    # Live CMake indexes reside in data/index; archived indexes sit by snippets.
    if index_path.parent.name == "index":
        candidates.append(index_path.parent.parent / relative)
    if flattened:
        candidates.append(index_path.parent / relative.name)
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return None


def archive(index_path):
    index_path = index_path.resolve()
    index = read_json(index_path)
    if version(index["version"])[0] != 1:
        raise ValueError("Only instrumentation data major version 1 is supported")
    destination = (
        Path(index["buildDir"]) / "instrumentation" / "cmake-build-doctor"
        / index_path.stem
    )
    destination.mkdir(parents=True, exist_ok=True)
    missing, copied = [], set()

    def copy_reference(name, flattened=False):
        if not name or name in copied:
            return
        copied.add(name)
        source = resolve_reference(index_path, name, flattened)
        if source is None:
            missing.append(name)
            return
        target = destination / relative_path(name)
        target.parent.mkdir(parents=True, exist_ok=True)
        if source != target.resolve():
            shutil.copyfile(source, target)

    for name in index["snippets"]:
        copy_reference(name)
        snippet_path = resolve_reference(index_path, name)
        if snippet_path:
            snippet = read_json(snippet_path)
            copy_reference(snippet.get("cmakeContent"))
            copy_reference(snippet.get("traceFile"))
    copy_reference(index.get("trace"), flattened=True)
    write_json(destination / "archive-notes.json", {"missing_references": missing})
    if index_path != (destination / index_path.name).resolve():
        shutil.copyfile(index_path, destination / index_path.name)
    for name in missing:
        print(f"warning: could not archive {name}", file=sys.stderr)
    return {"archive": str(destination), "missing_references": missing}


def prepare(build, cmake):
    result = subprocess.run(
        [cmake, "--version"], check=True, capture_output=True, text=True
    )
    match = re.search(r"cmake version (\d+)\.(\d+)", result.stdout)
    if not match or tuple(map(int, match.groups())) < (4, 3):
        raise ValueError("Instrumentation collection requires CMake 4.3 or newer")
    cmake_version = tuple(map(int, match.groups()))
    options = ["staticSystemInformation", "dynamicSystemInformation", "trace"]
    if cmake_version >= (4, 4):
        options += ["captureOutput", "compileTrace"]
    if cmake_version >= (4, 5):
        options.append("processMetrics")
    command = [sys.executable, str(Path(__file__).resolve()), "archive"]
    callback = subprocess.list2cmdline(command) if os.name == "nt" else shlex.join(command)
    query = build / ".cmake" / "instrumentation" / "v1" / "query" / "cmake-build-doctor.json"
    write_json(query, {
        "version": 1,
        "callbacks": [callback],
        "hooks": ["postGenerate", "preBuild", "postBuild", "preCMakeBuild", "postCMakeBuild"],
        "options": options,
    })
    api_query = build / ".cmake" / "api" / "v1" / "query" / "client-cmake-build-doctor"
    api_query.mkdir(parents=True, exist_ok=True)
    for name in ("codemodel-v2", "configureLog-v1", "toolchains-v1"):
        (api_query / name).touch()
    return {
        "instrumentation_query": str(query),
        "file_api_query": str(api_query),
        "cmake_version": result.stdout.splitlines()[0],
        "next": "Run your normal configure, then build. No project sources were changed.",
    }


def interval(record):
    start, duration = record.get("timeStart"), record.get("duration")
    if all(
        isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)
        for v in (start, duration)
    ) and duration >= 0:
        return start, start + duration
    return None


def contains(parent, child):
    outer, inner = interval(parent), interval(child)
    return bool(outer and inner and outer[0] <= inner[0] and inner[1] <= outer[1])


def evidence(record):
    fields = (
        "role", "timeStart", "duration", "target", "source", "config", "language",
        "command", "workingDir", "outputs", "outputSizes", "result", "processMetrics",
    )
    result = {key: record[key] for key in fields if key in record}
    result["evidence"] = record["_path"]
    result["attachments"] = record["_attachments"]
    return result


def load_records(source):
    build = source if source.is_dir() and (source / "CMakeCache.txt").exists() else None
    if source.is_file():
        indexes = [source]
    else:
        directory = source / "instrumentation" if (source / "instrumentation").is_dir() else source
        if build and directory.name != "instrumentation":
            directory = source / ".cmake" / "instrumentation" / "v1" / "data"
        indexes = sorted(directory.rglob("index-*.json")) if directory.exists() else []
    records, summaries, warnings = [], [], []
    seen, build_dirs = set(), set()
    for path in indexes:
        index = read_json(path)
        if "snippets" not in index:
            continue
        data_version = version(index.get("version"))
        if data_version[0] != 1:
            raise ValueError(f"Unsupported instrumentation major version in {path}")
        if not isinstance(index["snippets"], list):
            raise ValueError(f"Invalid snippets list in {path}")
        if index.get("buildDir"):
            build_dirs.add(str(Path(index["buildDir"]).resolve()))
        summary = {
            "evidence": str(path), "hook": index.get("hook"),
            "version": list(data_version), "snippet_references": len(index["snippets"]),
            "build_directory_at_capture": index.get("buildDir"),
        }
        summaries.append(summary)
        missing = collections.Counter()
        for name in index["snippets"]:
            snippet_path = resolve_reference(path, name)
            if snippet_path is None:
                missing[("snippet", name)] += 1
                continue
            data = snippet_path.read_bytes()
            identity = (index.get("buildDir"), name, hashlib.sha256(data).hexdigest())
            if identity in seen:
                continue
            seen.add(identity)
            record = json.loads(data.decode("utf-8-sig"))
            if version(record.get("version"))[0] != 1:
                raise ValueError(f"Unsupported snippet major version in {snippet_path}")
            record["_path"] = str(snippet_path)
            record["_attachments"] = {}
            for key in ("cmakeContent", "traceFile"):
                name = record.get(key)
                resolved = resolve_reference(path, name) if name else None
                record["_attachments"][key] = {
                    "reference": name,
                    "path": str(resolved) if resolved else None,
                    "status": "available" if resolved else "missing" if name else "not_collected",
                }
                if name and resolved is None:
                    missing[(key, name)] += 1
            if interval(record) is None:
                warnings.append(f"Invalid or missing timing; excluded from run timing: {snippet_path}")
            records.append(record)
        if missing:
            summary["missing_references"] = {
                "occurrences": sum(missing.values()), "distinct_references": len(missing),
                "examples": [
                    {"kind": key, "reference": name, "occurrences": count}
                    for (key, name), count in missing.most_common(10)
                ],
            }
            warnings.append(
                f"Missing snippets/attachments in {path}: {sum(missing.values())} references; "
                "see indexes[].missing_references"
            )
    if not summaries:
        raise ValueError(f"No instrumentation indexes found under {source}")
    if len(build_dirs) > 1:
        raise ValueError("Input contains multiple build trees; analyze each tree separately")
    return records, summaries, warnings, build


def concurrency(commands):
    edges = collections.Counter()
    for record in commands:
        bounds = interval(record)
        if bounds and bounds[1] > bounds[0]:
            edges[bounds[0]] += 1
            edges[bounds[1]] -= 1
    if not edges:
        return None
    times = sorted(edges)
    active = peak = 0
    occupancy = collections.Counter()
    previous = times[0]
    for timestamp in times:
        occupancy[active] += timestamp - previous
        active += edges[timestamp]
        peak = max(peak, active)
        previous = timestamp
    span = times[-1] - times[0]
    return {
        "command_span_ms": span,
        "peak": peak,
        "mean_over_command_span": sum(k * v for k, v in occupancy.items()) / span,
        "uncovered_gap_ms": occupancy[0],
    }


def summarize_commands(commands, top, content_cache):
    by_target = {}
    for record in commands:
        name = record.get("target") or "<unattributed>"
        key = (name, record.get("config"))
        target = by_target.setdefault(key, {
            "target": name, "configuration": record.get("config"),
            "command_count": 0, "cumulative_command_ms": 0, "content": [],
        })
        target["command_count"] += 1
        target["cumulative_command_ms"] += record["duration"]
        path = record["_attachments"]["cmakeContent"]["path"]
        if path:
            if path not in content_cache:
                content_cache[path] = read_json(Path(path))
            content = content_cache[path]
            metadata = {
                "project": content.get("project"),
                **content.get("targets", {}).get(name, {}),
                "evidence": path,
            }
            if metadata not in target["content"]:
                target["content"].append(metadata)
    metrics = [r.get("processMetrics") or {} for r in commands]
    rss = [m["maxRSS"] for m in metrics if isinstance(m.get("maxRSS"), (int, float))]
    cpu = [
        m["userTime"] + m["systemTime"] for m in metrics
        if all(isinstance(m.get(k), (int, float)) for k in ("userTime", "systemTime"))
    ]
    output = {}
    for key in ("stdout", "stderr"):
        values = [r[key] for r in commands if isinstance(r.get(key), str)]
        output[f"records_with_{key}"] = len(values)
        output[f"{key}_bytes"] = sum(len(v.encode("utf-8")) for v in values) if values else None
    return {
        "observed_command_count": len(commands),
        "roles": dict(collections.Counter(r["role"] for r in commands)),
        "cumulative_command_ms": sum(r["duration"] for r in commands) if commands else None,
        "concurrency": concurrency(commands),
        "process_metrics": {
            "records_with_rss": len(rss), "max_rss_kib": max(rss) if rss else None,
            "records_with_cpu": len(cpu), "reported_cpu_us": sum(cpu) if cpu else None,
        },
        "nonzero_results": [
            evidence(r) for r in commands if r.get("result") not in (None, 0)
        ][:top],
        "captured_output": output,
        "compiler_traces_available": sum(
            r["_attachments"]["traceFile"]["status"] == "available"
            for r in commands if r["role"] == "compile"
        ),
        "top_targets": sorted(
            by_target.values(), key=lambda t: t["cumulative_command_ms"], reverse=True
        )[:top],
        "slowest_commands": [
            evidence(r) for r in sorted(commands, key=lambda r: r["duration"], reverse=True)[:top]
        ],
        "last_commands": [
            evidence(r) for r in sorted(commands, key=lambda r: interval(r)[1], reverse=True)[:top]
        ],
    }


def group_runs(records, top):
    valid = [r for r in records if interval(r)]
    wrappers = [r for r in valid if r.get("role") == "cmakeBuild"]
    roots = [
        r for r in valid if r.get("role") in ROOT_ROLES
        and not (r["role"] == "build" and any(contains(w, r) for w in wrappers))
    ]
    commands = [r for r in valid if r.get("role") in COMMAND_ROLES]
    assigned, runs, content_cache = set(), [], {}
    for root in sorted(roots, key=lambda r: r["timeStart"]):
        work = [
            r for r in commands if root["role"] in ("build", "cmakeBuild") and contains(root, r)
        ]
        assigned.update(r["_path"] for r in work)
        runs.append({
            "id": Path(root["_path"]).stem,
            "kind": root["role"],
            "scope": "instrumented_invocation",
            "invocation": evidence(root),
            **summarize_commands(work, top, content_cache),
        })
    unscoped = [r for r in commands if r["_path"] not in assigned]
    if unscoped:
        runs.append({
            "id": "unscoped-commands", "kind": "commands",
            "scope": "commands_without_a_containing_build_record",
            "invocation": None,
            **summarize_commands(unscoped, top, content_cache),
        })
    return runs


def configure_log(path, top):
    if path is None or not path.is_file():
        return {"status": "not_available", "path": str(path) if path else None}
    try:
        import yaml
    except ImportError:
        return {
            "status": "requires_pyyaml", "path": str(path),
            "reason": "Install PyYAML to parse configure logs; JSON analysis is independent.",
        }
    documents, latest = [], None
    loader = getattr(yaml, "CSafeLoader", yaml.SafeLoader)
    try:
        with path.open(encoding="utf-8-sig") as stream:
            for document in yaml.load_all(stream, Loader=loader):
                if not document:
                    continue
                events = document.get("events", [])
                number = len(documents) + 1
                summary = {
                    "document": number,
                    "event_counts": dict(collections.Counter(e.get("kind") for e in events)),
                }
                documents.append(summary)
                probes, searches = [], []
                for event_number, event in enumerate(events, 1):
                    context = {
                        "event": event_number, "kind": event.get("kind"),
                        "backtrace": event.get("backtrace", []),
                    }
                    if event.get("kind") in ("try_compile-v1", "try_run-v1"):
                        results = {
                            key: event[key].get("exitCode")
                            for key in ("buildResult", "runResult") if isinstance(event.get(key), dict)
                        }
                        if any(v not in (None, 0) for v in results.values()):
                            probes.append({**context, "checks": event.get("checks"), "results": results})
                    if (
                        event.get("kind") in ("find-v1", "find_package-v1")
                        and "found" in event and event["found"] is None
                    ):
                        searches.append({
                            **context, "name": event.get("name"),
                            "variable": event.get("variable"),
                        })
                latest = {
                    **summary,
                    "nonzero_probe_count": len(probes), "nonzero_probes": probes[:top],
                    "unresolved_search_count": len(searches), "unresolved_searches": searches[:top],
                }
    except (yaml.YAMLError, AttributeError, TypeError) as error:
        return {"status": "parse_error", "path": str(path), "reason": str(error)}
    return {
        "status": "available", "path": str(path), "documents": documents,
        "latest_document": latest,
        "limitations": [
            "Documents are configure invocations; historical documents are not current work.",
            "No per-event durations: probe/search counts are not measured costs.",
            "Negative probes and unresolved optional searches need not indicate configure failure.",
            "buildResult.cached means cache storage, not that a probe was skipped.",
        ],
    }


def backtrace(target):
    graph, cursor, frames, seen = target.get("backtraceGraph", {}), target.get("backtrace"), [], set()
    nodes, files, commands = graph.get("nodes", []), graph.get("files", []), graph.get("commands", [])
    while isinstance(cursor, int) and 0 <= cursor < len(nodes) and cursor not in seen:
        seen.add(cursor)
        node = nodes[cursor]
        frame = {"line": node.get("line")}
        for field, values in (("file", files), ("command", commands)):
            position = node.get(field)
            if isinstance(position, int) and 0 <= position < len(values):
                frame[field] = values[position]
        frames.append(frame)
        cursor = node.get("parent")
    return frames


def file_api(build):
    result = {"status": "not_available", "targets": []}
    if build is None:
        return result
    reply = build / ".cmake" / "api" / "v1" / "reply"
    indexes = sorted(reply.glob("index-*.json"))
    if not indexes:
        return result
    path = indexes[-1]
    index = read_json(path)
    result.update({
        "status": "available", "evidence": str(path), "cmake": index.get("cmake"),
        "scope": "current_reply_not_proof_of_historical_capture_configuration",
    })
    for reference in index.get("objects", []):
        kind = reference.get("kind")
        expected = {"codemodel": 2, "configureLog": 1, "toolchains": 1}
        if kind not in expected or version(reference.get("version"))[0] != expected[kind]:
            continue
        object_path = reply / relative_path(reference["jsonFile"])
        obj = read_json(object_path)
        if kind == "configureLog":
            result["configure_log"] = obj.get("path")
        elif kind == "toolchains":
            result["toolchains"] = [
                {"language": t.get("language"), "compiler": t.get("compiler")}
                for t in obj.get("toolchains", [])
            ]
        else:
            for config in obj.get("configurations", []):
                for target_ref in config.get("targets", []):
                    target_path = reply / relative_path(target_ref["jsonFile"])
                    target = read_json(target_path)
                    result["targets"].append({
                        "id": target.get("id"), "name": target.get("name"),
                        "configuration": config.get("name"), "type": target.get("type"),
                        "dependencies": [d.get("id") for d in target.get("dependencies", [])],
                        "definition": backtrace(target), "evidence": str(target_path),
                    })
    return result


def build_context(build):
    if build is None or not (build / "CMakeCache.txt").is_file():
        return None
    fields = {
        "CMAKE_GENERATOR", "CMAKE_BUILD_TYPE", "CMAKE_CONFIGURATION_TYPES",
        "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_HOME_DIRECTORY",
    }
    values = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r"([^:#]+):[^=]+=(.*)", line)
        if match and match[1] in fields:
            values[match[1]] = match[2]
    return {"scope": "current_cache_not_historical_capture_metadata", "values": values}


def analyze(source, top=10, build=None, log=None, run=None):
    records, indexes, warnings, inferred_build = load_records(source.resolve())
    build = build.resolve() if build else inferred_build
    try:
        api = file_api(build)
    except (OSError, ValueError, KeyError, TypeError) as error:
        api = {"status": "parse_error", "targets": [], "reason": str(error)}
        warnings.append(f"Optional File API context could not be read: {error}")
    if log is None:
        if api.get("configure_log"):
            log = Path(api["configure_log"])
            if not log.is_absolute() and build:
                log = build / log
        elif build:
            log = build / "CMakeFiles" / "CMakeConfigureLog.yaml"
    if log and not log.is_file():
        warnings.append(f"Configure log not found: {log}")
    runs = group_runs(records, top)
    if run:
        if run == "latest":
            scoped = [r for r in runs if r["invocation"] is not None]
            runs = [max(scoped, key=lambda r: interval(r["invocation"])[1])] if scoped else runs[-1:]
        else:
            runs = [r for r in runs if r["id"] == run]
        if not runs:
            raise ValueError(f"No matching run: {run}")
    context = build_context(build)
    generator = (context or {}).get("values", {}).get("CMAKE_GENERATOR", "")
    if generator.startswith("Visual Studio"):
        warnings.append("Visual Studio is outside documented detailed instrumentation support; command absence is not a no-op.")
    if any(r["kind"] in ("build", "cmakeBuild") and not r["observed_command_count"] for r in runs):
        warnings.append("A build has no command records: it may be a no-op, unsupported, or incompletely captured.")
    return {
        "schema_version": 1, "source": str(source.resolve()),
        "units": {"duration": "ms", "timeStart": "Unix epoch ms", "cpu": "us", "rss": "KiB", "output": "bytes"},
        "indexes": indexes,
        "unique_snippet_count": len(records),
        "observed_roles": dict(collections.Counter(r.get("role", "<unknown>") for r in records)),
        "runs": runs, "current_build_context": context,
        "file_api": api, "configure_log": configure_log(log, top),
        "warnings": list(dict.fromkeys(warnings)),
        "limitations": [
            "Runs are reported separately; configure and generate are separate invocations.",
            "Command durations overlap each other and parent builds; cumulative time is not wall time or potential savings.",
            "Command overlap is not dependency critical path, CPU utilization, or a rebuild-causality explanation.",
            "Missing metrics are unknown, not zero; Windows wrapped-link metrics may under-report child work.",
            "Instrumentation alone does not provide header, template, or compiler-phase costs.",
            "Compiler trace references are inventoried, not interpreted; compileTrace does not enable -ftime-trace.",
            "Current File API/cache/log data is not automatically correlated to a historical instrumentation run.",
            "Source revision, workload equivalence, and instrumentation overhead are not established by this report.",
        ],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    setup = sub.add_parser("prepare", help="Write build-local queries; do not configure or build")
    setup.add_argument("build", type=Path)
    setup.add_argument("--cmake", default="cmake", help="CMake executable used for version gating")
    collect = sub.add_parser("archive", help="Archive an instrumentation callback index and its attachments")
    collect.add_argument("index", type=Path)
    inspect = sub.add_parser("analyze", help="Summarize a build tree, archive directory, or instrumentation index")
    inspect.add_argument("source", type=Path)
    inspect.add_argument("--output", type=Path)
    inspect.add_argument("--build-dir", type=Path, help="Optional current File API/cache/log context for an archive")
    inspect.add_argument("--configure-log", type=Path)
    inspect.add_argument("--run", help="Run ID from a report, or latest; default: report all runs separately")
    inspect.add_argument("--top", type=int, default=10)
    args = parser.parse_args(argv)
    try:
        if args.action == "prepare":
            report = prepare(args.build.resolve(), args.cmake)
        elif args.action == "archive":
            report = archive(args.index)
        else:
            if args.top < 1:
                raise ValueError("--top must be positive")
            report = analyze(args.source, args.top, args.build_dir, args.configure_log, args.run)
        if getattr(args, "output", None):
            write_json(args.output, report)
            print(f"Wrote {args.output}")
        else:
            print(json.dumps(report, indent=2))
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
