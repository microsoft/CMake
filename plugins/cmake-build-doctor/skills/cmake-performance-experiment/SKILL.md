---
name: cmake-performance-experiment
description: Collect missing CMake performance evidence by enabling instrumentation and running configure/build tasks, including for an unconfigured checkout. Also supports explicitly requested, bounded optimization experiments.
---

# Collect or measure a CMake performance experiment

There are two independent entry points: diagnostic collection and
source-changing optimization. A general performance investigation can require
collection even when the user has not explicitly asked to configure or build.
Optimization still requires a request to change and measure the project.
If suitable artifacts already exist, use `cmake-artifact-analysis` instead.
Do not collect another capture merely because an existing run is inconvenient.
Respect explicit analysis-only/no-build restrictions and historical-capture
requests.

## Diagnostic collection, including an unconfigured checkout

1. Inspect repository instructions, documented build commands, CMake presets,
   configure/build task definitions, CI setup, and any existing cache. Determine
   the source/build directories, CMake executable, generator, compiler/toolchain,
   configuration, target, and parallelism. Prefer the project's supported
   tasks/presets over invented generic commands.
2. If no build tree exists, select a dedicated diagnostic tree. If a tree is
   already configured, preserve its generator/toolchain/options when
   reconfiguring; use a separate tree for an incompatible setup or a fresh-build
   workload. State reasonable assumptions when the request lacks a target.
3. Before collecting a configure or build, prepare queries as below. The build
   directory need not exist yet. Run configure after adding the queries, even
   for an already-configured project, so File API replies and instrumented build
   rules are generated.
4. Execute the actual configure task and wait for its result. If it fails,
   inspect its diagnostics and configure log; do not proceed as though a valid
   build were generated. Address ordinary command/environment setup errors
   within scope, or report a concrete blocker.
5. For a build-performance question, execute the corresponding build task and
   wait for completion. For a configure-only question, skip this step. If valid
   instrumented rules and collection are already active and only the build
   capture is missing, a build task alone may suffice.
6. Check exit codes and archived indexes/snippets, then use
   `cmake-artifact-analysis`. Verify that the capture contains the requested
   work, not just an empty index or a no-op. For compilation-cost diagnosis
   after a no-op, use a fresh dedicated tree with the same setup; do not
   automatically clean or touch sources in the user's existing tree.

Run available task runners when appropriate, or the equivalent documented
commands directly without an IDE. Preserve task dependencies, working
directories, environment variables, and toolchain initialization. Record task
names or exact commands, exit codes, and outer wall-clock timings. Starting a
background task is not completion: wait for it before analyzing its artifacts.
Diagnostic collection is one representative pass, not the repeated A/B
measurement workflow below.

### Prepare, configure, build, analyze

Resolve `..\..\scripts\cmake_inspect.py` relative to this skill's directory.
Select the actual CMake executable and normal toolchain environment. Set up
Windows SDK/compiler environments and invoke dependent commands in the same
process.

```powershell
python "<script>" prepare "<build-directory>" --cmake "<cmake-executable>"
```

This only writes build-local queries. It does not configure or build. It checks
the chosen CMake version and enables supported optional fields: core collection
requires 4.3+, captured output/compiler-trace collection 4.4+, process metrics
4.5+. The callback uses this Python interpreter and this script's absolute
location; regenerate queries after moving the plugin or interpreter.

`prepare` is not a substitute for configuring and building. Execute the
project's actual configure task after preparation, then its build task when
needed. Keep the user's generator, compiler, configuration, target, flags, and
parallelism unless the experiment specifically changes one of them. Preserve
nonzero exit codes. Do not silently enable compiler profiling flags.

For a project with no preset or documented task, the command shape is:

```powershell
python "<script>" prepare "<build-directory>" --cmake "<cmake-executable>"
& "<cmake-executable>" -S "<source-directory>" -B "<build-directory>" -G "<generator>" <project-configure-options>
# Check configure success before running the build.
& "<cmake-executable>" --build "<build-directory>" --config "<configuration>" --target "<target>" --parallel <jobs>
# Wait for build completion and preserve its result before analysis.
python "<script>" analyze "<build-directory>" --output "<report.json>"
```

Replace placeholders with the discovered settings; do not run this template
literally or use it to bypass required preset/toolchain configuration. An
explicit preset `binaryDir` must match the directory passed to `prepare`.
On command failure, preserve diagnostics and analyze available partial
evidence without presenting it as a successful representative build.

The callback archives indexes, snippets, CMake-content files, Google traces,
and referenced compiler traces under:

```text
<build>\instrumentation\cmake-build-doctor\<index-name>\
```

Other CMake query callbacks remain in place. Queries merge: callbacks can run
at hooks requested by other queries, and empty indexes are normal. The plugin
does not delete CMake-owned artifacts. Check `archive-notes.json` and analysis
warnings for missing attachments.

The File API queries request codemodel, toolchains, and configure-log discovery.
They take effect at the next configure. No MCP server, global installation,
ETW capture, or IDE is required. Do not treat partial Visual Studio collection
as detailed generator support.

After collection, run the analysis skill. For a configure-function bottleneck,
the YAML alone is insufficient: use separately requested CMake profiling rather
than assigning costs to event counts.

## Bounded optimization

1. Record source revision and dirty state, tool versions, generator,
   configuration, target/workload, parallelism, flags/PCH/cache settings,
   machine, and whether collection was enabled. Keep a session artifact with
   exact commands, exit codes, wall-clock samples, evidence, and decisions.
2. Establish that the unchanged workload builds and its correctness checks
   pass. Identify one evidence-backed hypothesis and expected benefit.
3. Before editing, collect at least three wall-clock baseline samples with
   consistent collection settings. For clean builds clean only the selected
   dedicated build tree; for incremental work dirty the same inputs each time.
   Keep cold configure, warm configure, clean, incremental, and no-op samples
   distinct. Do not change project sources during baseline collection.
4. Apply one narrow, reversible change. Preserve unrelated work. Do not relax
   correctness, runtime behavior, shipping optimization, or ABI for build speed.
5. Run relevant correctness tests and at least three matched candidate samples
   with the same toolchain, workload, parallelism, cache/source state, and
   collection settings. Reanalyze for attribution. Measure actual command wall
   time, not sums of snippet durations.
6. Report individual samples, mean, standard deviation, and observed delta.
   If variability makes the decision ambiguous, take additional matched samples
   within the user's budget or label the result inconclusive. Keep a change
   only when correctness passes and the benefit exceeds observed noise.

Instrumented measurements describe an instrumented workflow. Do not claim
uninstrumented savings or instrumentation overhead without a separate matched
control. If measuring without instrumentation, use a dedicated build tree or
explicitly account for every project/user query; removing only this plugin's
query does not disable instrumentation requested elsewhere.

Stop after one experiment unless further iterations were requested. Do not
commit, publish, or open a pull request unless explicitly asked.
