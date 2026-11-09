import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "cmake_inspect.py"
SPEC = importlib.util.spec_from_file_location("cmake_inspect", SCRIPT)
doctor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(doctor)


class InspectorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write(self, name, value):
        path = self.root / name
        doctor.write_json(path, value)
        return path

    def record(self, role, start=0, duration=100, **fields):
        return {
            "version": {"major": 1, "minor": 2}, "role": role,
            "timeStart": start, "duration": duration, "result": 0, **fields,
        }

    def index(self, folder, records, **fields):
        path = self.root / folder
        for name, value in records.items():
            doctor.write_json(path / name, value)
        return self.write(path / "index-001.json", {
            "version": 1, "buildDir": str(self.root / "build"),
            "hook": "postCMakeBuild", "snippets": list(records), **fields,
        })

    def test_runs_do_not_pool_rebuilds_or_double_count_native_wrapper(self):
        self.index("archive", {
            "cmakeBuild-1.json": self.record("cmakeBuild", 0, 100),
            "build-1.json": self.record("build", 1, 98),
            "compile-a.json": self.record("compile", 10, 40, target="app"),
            "compile-b.json": self.record("compile", 30, 40, target="app"),
            "cmakeBuild-2.json": self.record("cmakeBuild", 200, 20),
        })
        report = doctor.analyze(self.root)
        runs = report["runs"]
        self.assertEqual(len(runs), 2)
        self.assertEqual(runs[0]["cumulative_command_ms"], 80)
        self.assertEqual(runs[0]["invocation"]["duration"], 100)
        self.assertEqual(runs[0]["concurrency"]["peak"], 2)
        self.assertAlmostEqual(runs[0]["concurrency"]["mean_over_command_span"], 80 / 60)
        self.assertIsNone(runs[1]["cumulative_command_ms"])
        self.assertIsNone(runs[1]["concurrency"])
        self.assertTrue(any("may be a no-op" in w for w in report["warnings"]))
        self.assertEqual(doctor.analyze(self.root, run="latest")["runs"][0]["id"], "cmakeBuild-2")

    def test_mixed_versions_and_duplicate_archives_are_deduplicated(self):
        records = {
            "configure-1.json": {**self.record("configure"), "version": 1},
            "generate-1.json": self.record("generate", 100, 10),
        }
        self.index("first", records)
        self.index("second", records, version={"major": 1, "minor": 2})
        report = doctor.analyze(self.root)
        self.assertEqual(report["unique_snippet_count"], 2)
        self.assertEqual(len(report["indexes"]), 2)
        self.assertEqual(len(report["runs"]), 2)

    def test_empty_indexes_and_missing_snippets_are_not_zero_work(self):
        self.index("empty", {})
        self.index("missing", {}, snippets=["missing.json"])
        report = doctor.analyze(self.root)
        self.assertEqual(report["runs"], [])
        self.assertTrue(any("Missing snippet" in w for w in report["warnings"]))
        with self.assertRaisesRegex(ValueError, "No matching run"):
            doctor.analyze(self.root, run="latest")

    def test_rejects_unknown_major_and_multiple_build_trees(self):
        path = self.index("one", {}, version={"major": 2, "minor": 0})
        with self.assertRaisesRegex(ValueError, "Unsupported instrumentation"):
            doctor.analyze(self.root)
        doctor.write_json(path, {"version": 1, "snippets": [], "buildDir": "first"})
        self.index("two", {}, buildDir="second")
        with self.assertRaisesRegex(ValueError, "multiple build trees"):
            doctor.analyze(self.root)

    def test_missing_attachments_keep_evidence_and_unknown_metrics(self):
        self.index("archive", {
            "compile-1.json": self.record(
                "compile", cmakeContent="content/missing.json", traceFile="compile-trace/missing.json"
            ),
        })
        run = doctor.analyze(self.root)["runs"][0]
        command = run["slowest_commands"][0]
        self.assertEqual(run["scope"], "commands_without_a_containing_build_record")
        self.assertEqual(command["attachments"]["cmakeContent"]["status"], "missing")
        self.assertEqual(command["attachments"]["traceFile"]["status"], "missing")
        self.assertIsNone(run["process_metrics"]["max_rss_kib"])
        self.assertIsNone(run["process_metrics"]["reported_cpu_us"])
        self.assertIsNone(run["captured_output"]["stdout_bytes"])
        self.assertEqual(run["captured_output"]["records_with_stdout"], 0)
        self.assertTrue(Path(command["evidence"]).is_file())

    def test_resource_units_and_target_content_are_preserved(self):
        path = self.index("archive", {
            "compile-1.json": self.record(
                "compile", target="app", config="Debug", cmakeContent="content/cmake.json",
                processMetrics={"userTime": 5000, "systemTime": 1000, "maxRSS": 2048},
                stdout="", stderr="warning",
            ),
        })
        doctor.write_json(path.parent / "content" / "cmake.json", {
            "version": 1, "project": "demo", "targets": {"app": {"type": "EXECUTABLE"}},
        })
        report = doctor.analyze(self.root)
        run = report["runs"][0]
        self.assertEqual(run["process_metrics"]["reported_cpu_us"], 6000)
        self.assertEqual(run["process_metrics"]["max_rss_kib"], 2048)
        self.assertEqual(run["captured_output"]["stderr_bytes"], 7)
        self.assertEqual(run["top_targets"][0]["content"][0]["type"], "EXECUTABLE")
        self.assertEqual(report["units"]["cpu"], "us")

    def test_archive_copies_shared_content_and_both_trace_types(self):
        data = self.root / "data"
        index = self.index(data / "index", {}, dataDir=str(data))
        payload = doctor.read_json(index)
        payload["snippets"] = ["compile-a.json", "compile-b.json"]
        payload["trace"] = "trace/google.json"
        doctor.write_json(index, payload)
        for name in payload["snippets"]:
            doctor.write_json(data / name, self.record(
                "compile", cmakeContent="content/cmake.json", traceFile="compile-trace/clang.json"
            ))
        doctor.write_json(data / "content" / "cmake.json", {"project": "demo"})
        doctor.write_json(data / "compile-trace" / "clang.json", {"traceEvents": []})
        doctor.write_json(data / "trace" / "google.json", {"traceEvents": []})
        with mock.patch.object(doctor.shutil, "copyfile", wraps=doctor.shutil.copyfile) as copy:
            result = doctor.archive(index)
        archive = Path(result["archive"])
        self.assertEqual(result["missing_references"], [])
        self.assertTrue((archive / "content" / "cmake.json").is_file())
        self.assertTrue((archive / "compile-trace" / "clang.json").is_file())
        self.assertTrue((archive / "trace" / "google.json").is_file())
        self.assertEqual(sum(call.args[1] == archive / "content" / "cmake.json" for call in copy.call_args_list), 1)
        self.assertEqual(len(doctor.analyze(archive)["runs"][0]["slowest_commands"]), 2)

    def test_archive_handles_empty_index_without_trace_and_notes_missing_files(self):
        empty = self.index("empty", {})
        self.assertEqual(doctor.archive(empty)["missing_references"], [])
        missing = self.index("missing", {}, snippets=["gone.json"])
        with contextlib.redirect_stderr(io.StringIO()):
            result = doctor.archive(missing)
        self.assertEqual(result["missing_references"], ["gone.json"])
        notes = doctor.read_json(Path(result["archive"]) / "archive-notes.json")
        self.assertEqual(notes["missing_references"], ["gone.json"])

    def test_legacy_flattened_trace_and_backslash_reference(self):
        path = self.index("old", {}, trace="trace/google.json")
        doctor.write_json(path.parent / "google.json", {})
        doctor.write_json(path.parent / "content" / "cmake.json", {})
        self.assertEqual(
            doctor.resolve_reference(path, "trace/google.json", flattened=True),
            (path.parent / "google.json").resolve(),
        )
        self.assertEqual(
            doctor.resolve_reference(path, "content\\cmake.json"),
            (path.parent / "content" / "cmake.json").resolve(),
        )
        for name in ("../escape.json", "/absolute.json", "C:\\outside.json"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                doctor.resolve_reference(path, name)

    def test_file_api_reads_only_referenced_objects_and_target_backtrace(self):
        build = self.root / "build"
        reply = build / ".cmake" / "api" / "v1" / "reply"
        doctor.write_json(reply / "index-001.json", {
            "cmake": {"generator": {"name": "Ninja"}},
            "objects": [
                {"kind": "codemodel", "version": {"major": 2, "minor": 8}, "jsonFile": "model.json"},
                {"kind": "configureLog", "version": 1, "jsonFile": "log.json"},
                {"kind": "toolchains", "version": 1, "jsonFile": "tools.json"},
            ],
        })
        doctor.write_json(reply / "model.json", {
            "configurations": [{"name": "Debug", "targets": [{"jsonFile": "app.json"}]}],
        })
        doctor.write_json(reply / "app.json", {
            "id": "app::1", "name": "app", "type": "EXECUTABLE", "backtrace": 0,
            "dependencies": [{"id": "lib::1"}],
            "backtraceGraph": {
                "nodes": [{"file": 0, "line": 12, "command": 0}],
                "files": ["CMakeLists.txt"], "commands": ["add_executable"],
            },
        })
        doctor.write_json(reply / "log.json", {"path": "CMakeFiles/CMakeConfigureLog.yaml"})
        doctor.write_json(reply / "tools.json", {
            "toolchains": [{"language": "CXX", "compiler": {"id": "Clang"}}],
        })
        doctor.write_json(reply / "unreferenced-garbage.json", {"not": "an object to load"})
        result = doctor.file_api(build)
        self.assertEqual(result["targets"][0]["dependencies"], ["lib::1"])
        self.assertEqual(result["targets"][0]["definition"][0]["line"], 12)
        self.assertEqual(result["toolchains"][0]["compiler"]["id"], "Clang")
        self.assertEqual(result["configure_log"], "CMakeFiles/CMakeConfigureLog.yaml")

    def test_partial_optional_file_api_does_not_break_instrumentation_analysis(self):
        self.index("archive", {"compile.json": self.record("compile")})
        build = self.root / "build"
        doctor.write_json(build / ".cmake" / "api" / "v1" / "reply" / "index-001.json", {
            "objects": [{"kind": "codemodel", "version": 2, "jsonFile": "gone.json"}],
        })
        report = doctor.analyze(self.root / "archive", build=build)
        self.assertEqual(report["file_api"]["status"], "parse_error")
        self.assertEqual(report["runs"][0]["observed_command_count"], 1)

    def test_latest_invocation_is_not_displaced_by_older_unscoped_commands(self):
        self.index("archive", {
            "compile-old.json": self.record("compile", 0, 10),
            "cmakeBuild-new.json": self.record("cmakeBuild", 200, 20),
        })
        report = doctor.analyze(self.root, run="latest")
        self.assertEqual(report["runs"][0]["id"], "cmakeBuild-new")

    def test_missing_shared_content_is_compact_and_timing_errors_are_explicit(self):
        self.index("archive", {
            **{
                f"compile-{i}.json": self.record(
                    "compile", i, 1, cmakeContent="content/gone.json"
                ) for i in range(25)
            },
            "compile-invalid.json": self.record("compile", duration=-1),
        })
        report = doctor.analyze(self.root)
        self.assertEqual(report["indexes"][0]["missing_references"]["occurrences"], 25)
        self.assertEqual(report["indexes"][0]["missing_references"]["distinct_references"], 1)
        self.assertEqual(len(report["warnings"]), 2)
        self.assertEqual(report["runs"][0]["observed_command_count"], 25)

    def test_configure_log_documents_are_separate_and_negative_checks_are_not_failures(self):
        try:
            import yaml
        except ImportError:
            self.skipTest("PyYAML is optional")
        log = self.root / "CMakeConfigureLog.yaml"
        documents = [
            {"events": [{"kind": "try_compile-v1", "buildResult": {"exitCode": 1, "cached": True}}]},
            {"events": [
                {"kind": "try_compile-v1", "checks": ["feature"], "backtrace": ["CMakeLists.txt:12"],
                 "buildResult": {"exitCode": 0, "cached": True}},
                {"kind": "find_package-v1", "name": "Optional", "found": None},
            ]},
        ]
        log.write_text(yaml.safe_dump_all(documents), encoding="utf-8")
        result = doctor.configure_log(log, 5)
        self.assertEqual(len(result["documents"]), 2)
        self.assertEqual(result["latest_document"]["nonzero_probe_count"], 0)
        self.assertEqual(result["latest_document"]["unresolved_search_count"], 1)
        self.assertTrue(any("need not indicate" in note for note in result["limitations"]))

    def test_yaml_is_optional_and_unsafe_yaml_is_not_executed(self):
        log = self.root / "log.yaml"
        log.write_text("!!python/object/apply:builtins.print ['must not run']", encoding="utf-8")
        with mock.patch.dict(sys.modules, {"yaml": None}):
            self.assertEqual(doctor.configure_log(log, 5)["status"], "requires_pyyaml")
        try:
            import yaml
        except ImportError:
            return
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(doctor.configure_log(log, 5)["status"], "parse_error")
        self.assertEqual(output.getvalue(), "")

    def test_prepare_gates_options_and_preserves_other_queries(self):
        for cmake_version, expected in (("4.3.0", "trace"), ("4.4.0", "compileTrace"), ("4.5.0-rc1", "processMetrics")):
            with self.subTest(version=cmake_version):
                build = self.root / cmake_version
                other = build / ".cmake" / "instrumentation" / "v1" / "query" / "other.json"
                doctor.write_json(other, {"version": 1})
                completed = subprocess.CompletedProcess([], 0, f"cmake version {cmake_version}\n", "")
                with mock.patch.object(doctor.subprocess, "run", return_value=completed):
                    result = doctor.prepare(build, "chosen-cmake")
                query = doctor.read_json(Path(result["instrumentation_query"]))
                self.assertIn(expected, query["options"])
                self.assertEqual("processMetrics" in query["options"], cmake_version.startswith("4.5"))
                self.assertEqual(doctor.read_json(other), {"version": 1})
                self.assertTrue((Path(result["file_api_query"]) / "codemodel-v2").exists())
        completed = subprocess.CompletedProcess([], 0, "cmake version 4.2.0\n", "")
        with mock.patch.object(doctor.subprocess, "run", return_value=completed):
            with self.assertRaisesRegex(ValueError, "4.3"):
                doctor.prepare(self.root / "unsupported", "old-cmake")

    def test_cli_writes_report_and_reports_bad_input_without_traceback(self):
        self.index("archive", {"compile.json": self.record("compile")})
        output = self.root / "report.json"
        completed = subprocess.run(
            [sys.executable, str(SCRIPT), "analyze", str(self.root / "archive"), "--output", str(output)],
            text=True, capture_output=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(json.loads(output.read_text())["unique_snippet_count"], 1)
        completed = subprocess.run(
            [sys.executable, str(SCRIPT), "analyze", str(self.root / "missing")],
            text=True, capture_output=True,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("No instrumentation indexes", completed.stderr)
        self.assertNotIn("Traceback", completed.stderr)


if __name__ == "__main__":
    unittest.main()
