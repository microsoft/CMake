$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$version = '3.0.26091502'
$expectedHash = '052CAA6096FA45DB2D4499ABD9DD2C2EB43C15432D8C57F8293294BBE7BE0930'
$feed = 'https://pkgs.dev.azure.com/azure-public/VisualCpp/_packaging/cpp_PublicPackages/nuget/v3/index.json'
$results = Join-Path $env:GITHUB_WORKSPACE 'build-perf-results'
$build = Join-Path $env:RUNNER_TEMP 'cmake-build-perf'
$install = Join-Path $env:LOCALAPPDATA 'vcperf\build-perf-cpp'
New-Item -ItemType Directory -Path $results -Force | Out-Null

$nuget = (Get-Command nuget.exe -ErrorAction Stop).Source
& $nuget install Microsoft.Cpp.vcperf -Version $version -Source $feed `
  -OutputDirectory $install -NonInteractive -NoCache -DirectDownload -PackageSaveMode nupkg `
  2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) {
  throw "Installing Microsoft.Cpp.vcperf $version failed with exit code $LASTEXITCODE"
}
$package = Join-Path $install "Microsoft.Cpp.vcperf.$version"
$packageHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $package "Microsoft.Cpp.vcperf.$version.nupkg")).Hash
if ($packageHash -ne $expectedHash) {
  throw "Microsoft.Cpp.vcperf $version package checksum mismatch: $packageHash"
}
$vcperf = Join-Path $package 'x64\vcperf.exe'
if (-not (Test-Path -LiteralPath $vcperf -PathType Leaf)) {
  throw "The pinned package does not contain x64\vcperf.exe: $package"
}
$vcperfDirectory = Split-Path -Parent $vcperf
$vcperfDirectory | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
"VCPERF_EXE=$vcperf" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append

$isAdmin = ([Security.Principal.WindowsPrincipal]::new(
  [Security.Principal.WindowsIdentity]::GetCurrent()
)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
  throw 'ETW capture requires an elevated Windows runner. This workflow will not request UAC or change trace permissions.'
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = & $vswhere -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $visualStudio) {
  throw 'Could not locate an installed x64 MSVC toolchain.'
}
Import-Module (Join-Path $visualStudio 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $visualStudio -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:PATH = "$vcperfDirectory;$env:PATH"
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$null = Get-Command ninja.exe, cl.exe -ErrorAction Stop
$parallel = [int]$env:NUMBER_OF_PROCESSORS
if ($parallel -lt 1) {
  throw 'NUMBER_OF_PROCESSORS must describe the runner build parallelism.'
}
if (Test-Path -LiteralPath $build) {
  throw "Expected a fresh build directory; refusing to reuse $build"
}

& $cmake -S $env:GITHUB_WORKSPACE -B $build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
  -DBUILD_TESTING=OFF `
  -DBUILD_QtDialog=OFF -DBUILD_CursesDialog=OFF `
  2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) {
  throw "CMake configuration failed with exit code $LASTEXITCODE"
}
$session = "CMakeBuild_$($env:GITHUB_RUN_ID)_$($env:GITHUB_RUN_ATTEMPT)"
$etl = Join-Path $results 'build.etl'
$buildExit = $null
$timer = [System.Diagnostics.Stopwatch]::new()
try {
  & $vcperf /start /level3 $session 2>&1 | Out-Host
  $timer.Start()
  & $cmake --build $build --parallel $parallel `
    2>&1 | Out-Host
  $buildExit = $LASTEXITCODE
} finally {
  $timer.Stop()
  & $vcperf /stop $session $etl 2>&1 | Out-Host
}

if ($buildExit -ne 0) {
  throw "The instrumented CMake build failed with exit code $buildExit"
}
if (-not (Test-Path -LiteralPath $etl -PathType Leaf)) {
  throw "vcperf did not produce $etl. See the workflow logs."
}
# vcperf can return an HRESULT on success; do not pass it to the Actions pwsh wrapper.
$global:LASTEXITCODE = 0

@"
### CMake build profile captured

- Tool: Microsoft.Cpp.vcperf $version (x64).
- Build: MSVC x64, Ninja, Release, $parallel parallel jobs.
- Tests and optional GUIs: disabled.
- Instrumented build: $([Math]::Round($timer.Elapsed.TotalSeconds, 2)) seconds (not an uninstrumented baseline).
- ETL trace: the build-performance artifact. Command diagnostics are in the workflow logs.
"@ | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Encoding utf8 -Append
