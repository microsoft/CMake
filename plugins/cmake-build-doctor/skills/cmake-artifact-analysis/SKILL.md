---
name: cmake-artifact-analysis
description: Analyze existing CMake instrumentation, configure logs, and File API replies. If suitable captures are missing, route to instrumentation collection before analysis unless the user requested existing artifacts only.
---

# Analyze CMake artifacts

## Check whether analysis can start

Do not assume the project is configured, built, or instrumented. Check the
requested workload against the available indexes/snippets before extraction.
A source checkout, CMake cache, configure log, or File API reply alone does not
provide an instrumented build capture.

If suitable evidence is missing and the user requested a performance
investigation, use `cmake-performance-experiment` to enable instrumentation and
execute the necessary configure/build tasks, then return here. A missing-index
error from the extractor is a signal to collect, not the end of the workflow.
Collection does not require the user to separately ask to run each normal
configure/build task. Do not collect if the user explicitly requested existing
artifacts only, prohibited rebuilding, or supplied a historical capture to
analyze. Report the resulting limitation instead.

Do not automatically collect again for known generator limitations, missing
compiler-internal profiling, or a gap unrelated to the user's question.

## Extract available artifacts

Resolve `..\..\scripts\cmake_inspect.py` relative to this skill's directory.
Use the absolute resolved path below as `<script>`; do not assume the plugin
is installed inside the project being analyzed.

```powershell
python "<script>" analyze "<build-directory>" --output "<report.json>"
```

Accepted input: a build tree, instrumentation archive directory, an archived
index directory, or one index JSON. One index may represent only part of a
build; prefer the complete archive. The script recognizes both the repository's
`instrument.py` archives and its own callback archives and deduplicates repeated
snippets. It does not interpret File API indexes as instrumentation indexes.

For a moved archive, supply current context only if appropriate:

```powershell
python "<script>" analyze "<archive>" --build-dir "<build-directory>" --output "<report.json>"
```

`--configure-log "<path>"` selects a log explicitly. JSON extraction uses only
Python's standard library. YAML parsing is optional and requires PyYAML; report
its absence instead of silently installing it. A missing log or metadata reply
must not be interpreted as no configure work.

## Read the compact report

1. Check `warnings`, `indexes`, `observed_roles`, and `limitations`.
2. Read `runs` separately. Each configure, generate, or whole-build invocation
   has an ID and source snippet. Do not combine cold, warm, incremental, and
   no-op invocations. Native `build` records contained in `cmakeBuild` wrappers
   are not separate runs. Commands without a containing build are explicitly
   unscoped, not a measured whole build.
3. For a build, inspect `top_targets`, `slowest_commands`, `last_commands`,
   concurrency, result codes, and metric coverage. Cumulative command time
   overlaps and is not end-to-end time. RSS is KiB; CPU is microseconds;
   duration is milliseconds; output sizes are bytes.
4. Follow a few `evidence` paths for complete commands, stdout/stderr, and
   attachments. The summary bounds ranked lists with `--top` (default 10);
   absence from a top list does not mean absence from the capture.
5. Consult `file_api.targets` for optional dependency IDs and definition
   backtraces. Match by configuration and target name; do not guess between
   ambiguous matches. This is current build-graph context, not a measured
   critical path or automatically verified historical context.
6. For configure questions, read `configure_log.latest_document` and document
   counts. Its samples identify events/backtraces, not durations. Open the YAML
   at those events to explain checks/searches. If investigating an older
   invocation, select a matching saved log rather than assuming the latest
   document belongs to it.

To narrow a report, use an exact ID from `runs`:

```powershell
python "<script>" analyze "<build-directory>" --run "<run-id>" --top 5 --output "<focused.json>"
```

`--run latest` selects the latest instrumented invocation, which can legitimately
be a no-op build or configure/generate, not the last build that compiled files.

## Interpretation limits

- Detailed documented generator coverage is Ninja, Makefiles, and FASTBuild.
  Visual Studio may produce only coarse records. No command records can mean
  unsupported/incomplete capture, not just a no-op.
- Host snapshots are not process CPU utilization. Windows wrapped-link metrics
  can miss child-process resource usage.
- A slow command does not identify expensive headers, templates, compiler
  phases, or the reason it rebuilt. Do not prescribe PCH from timings alone.
- `compileTrace` collects compiler-produced traces; it does not enable
  `-ftime-trace`. The extractor inventories attachments, not their inner events.
- Configure logs append documents. Negative probes and unresolved optional
  searches need not mean configure failure. `cached` describes storage of a
  result variable, not a cache hit.
- Revision, flags, workload, parallelism, machine conditions, and capture
  overhead must be established separately before performance comparisons.

Give at most a few prioritized findings with evidence and uncertainty. Use the
experiment skill for missing diagnostic captures as described above; its
source-changing optimization phase still requires an optimization request.
