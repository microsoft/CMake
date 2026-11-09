# CMake build doctor

A local Agency/Copilot plugin for explaining CMake configure and build costs.
It bundles a Python extractor, two skills, and a `build-doctor` agent. The agent
can start from an unconfigured source checkout: it collects missing evidence
by running configure/build tasks, then analyzes it. Source-changing optimization
is a separate workflow.

## Load locally

From the repository root:

```powershell
agency copilot --plugin "local:.\plugins\cmake-build-doctor" --agent "cmake-build-doctor:build-doctor"
```

Or load directly in Copilot:

```powershell
copilot --plugin-dir ".\plugins\cmake-build-doctor" --agent "cmake-build-doctor:build-doctor"
```

Neither command installs the plugin globally. Start a new session to load its
skills and agent; existing sessions do not automatically acquire these files.

Example request:

> Investigate this project's build performance. Configure and build with
> instrumentation if a suitable capture does not exist yet.

For existing evidence only:

> Analyze build\agent-evaluation\clang. Explain the expensive targets and
> commands, compare the configure invocations, and identify what evidence is
> missing before suggesting an optimization. Do not rebuild.

The agent inspects project instructions, presets, task definitions, and the
existing build state before choosing commands. It prepares queries before
configure, runs configure and build when needed, waits for task completion, and
then analyzes the resulting capture. Configure-only questions need not trigger
a build. Suitable existing captures are reused; an explicit "do not rebuild"
or "existing artifacts only" request prevents new collection. A no-op capture
is not treated as evidence of compilation cost.

The plugin has no automatic session hooks, MCP server, downloads, or runtime
dependency installation. Captures can contain local paths, command arguments,
diagnostics, and host information; keep them local unless intentionally shared.

## Extract directly

Python 3.9+ is sufficient for instrumentation and File API JSON. PyYAML is
optional for configure logs; without it the report explicitly marks YAML
analysis unavailable. If needed, install it in your chosen Python environment:

```powershell
python -m pip install PyYAML
```

Analyze the retained experiments without running another build:

```powershell
python .\plugins\cmake-build-doctor\scripts\cmake_inspect.py analyze .\build\agent-evaluation\clang --output .\build\agent-evaluation\clang-doctor.json
python .\plugins\cmake-build-doctor\scripts\cmake_inspect.py analyze .\build\agent-evaluation\ninja --output .\build\agent-evaluation\ninja-doctor.json
python .\plugins\cmake-build-doctor\scripts\cmake_inspect.py analyze .\build\agent-evaluation\visual-studio --output .\build\agent-evaluation\vs-doctor.json
```

The input can also be an archive directory or one instrumentation index.
`--build-dir` adds optional current build context to a moved archive;
`--configure-log` selects a saved YAML; `--top` bounds ranked lists; `--run`
selects an ID from the report or `latest`. By default all invocations are
reported separately, including no-op builds.

The report contains run-scoped timings, target rankings, command overlap,
resource coverage, result codes, attachment references, and evidence paths.
It summarizes the latest configure-log document while listing event counts for
all documents. It reads referenced File API targets/dependencies/backtraces and
toolchain metadata when available. It does not parse compiler trace events or
rank configure functions by duration.

## Collect in another build tree

Choose the same CMake executable that will configure the project:

```powershell
python .\plugins\cmake-build-doctor\scripts\cmake_inspect.py prepare .\build\doctor --cmake "C:\Program Files\CMake\bin\cmake.exe"
```

When using the script directly, run your normal configure and build commands
after `prepare`; the script does not execute them. The agent's collection
workflow is responsible for running those tasks when needed. `prepare` works
before the build directory exists and only writes:

```text
<build>\.cmake\instrumentation\v1\query\cmake-build-doctor.json
<build>\.cmake\api\v1\query\client-cmake-build-doctor\
```

The callback copies indexes, snippets, content, and trace attachments into
`<build>\instrumentation\cmake-build-doctor\<index-name>\` before CMake can remove
them. Missing references are recorded in `archive-notes.json`. It preserves
other queries and does not change `CMakeLists.txt` or the repository's existing
`instrument.py`. The callback points to an absolute script/interpreter path;
rerun `prepare` after moving either. To stop this plugin's collection, remove
only the query file and client query directory listed above, then reconfigure.
Other instrumentation queries can still enable collection.

## Boundaries

| Surface | What it adds | Important limit |
|---|---|---|
| Instrumentation | Configure/build timings, commands, targets, optional output/resources | CMake 4.3+; detailed documented support for Ninja, Makefiles, FASTBuild |
| Optional instrumentation fields | Captured output/compiler traces in 4.4+, process metrics in 4.5+ | `compileTrace` does not enable compiler profiling |
| CMake File API | Current target dependencies, definition backtraces, compiler identity, log discovery | Separate API; replies require queries and a configure |
| Configure log | Per-invocation checks, searches, diagnostics, source backtraces | No measured per-event durations; negative checks can be expected |

Visual Studio produced only coarse configure/whole-build records in the retained
experiment. Missing commands are not proof of a no-op. Missing resource values
are unknown, not zero. Parent build durations and child command durations
overlap; neither their sum nor command concurrency is a dependency critical path.
Current cache/File API/log data does not establish a historical capture's
revision or configuration. Compare only matched workloads.

## Validate

```powershell
python -m unittest discover -s .\plugins\cmake-build-doctor\tests -v
agency plugin check .\plugins\cmake-build-doctor
copilot --plugin-dir .\plugins\cmake-build-doctor plugin list --json
copilot --plugin-dir .\plugins\cmake-build-doctor skill list
```

Tests use temporary fixtures. Real-artifact validation uses the retained
Ninja/MSVC, Visual Studio, and Ninja/Clang experiments; no target rebuild is
needed. See the bundled skills for the evidence and matched-experiment rules.
