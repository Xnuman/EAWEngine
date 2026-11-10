<#
.SYNOPSIS
    Configures and builds EAWEngine: the entry point for building the project.

.DESCRIPTION
    Configures build/ with the Visual Studio 2022 generator (x64, C++20) and builds the chosen
    configurations. Executables go to executables/<Configuration>/; Debug files carry the _d suffix
    (EAWEngine_d.exe). -Clean starts from an empty build tree but keeps the fetched third-party
    sources (build/_deps), and removes the built executables.

.EXAMPLE
    .\build.ps1                              # Debug
    .\build.ps1 -Configuration Release       # Release
    .\build.ps1 -Configuration All -Tests    # Debug and Release, with EAWEngineTests
    .\build.ps1 -Configuration Release -Clean

    From cmd or Explorer: powershell -ExecutionPolicy Bypass -File build.ps1 -Configuration Release
#>
param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string] $Configuration = 'Debug',
    # Build EAWEngineTests too (run it with executables\<Configuration>\EAWEngineTests[_d].exe or ctest).
    [switch] $Tests,
    # Remove the build tree (except fetched third-party sources) and the built executables first.
    [switch] $Clean,
    [string] $BuildDirectory = 'build'
)

# Cmdlet failures stop the script through -ErrorAction Stop and cmake's through its exit code; a
# global 'Stop' would turn cmake's stderr warnings into errors whenever the output is redirected.
Set-Location -LiteralPath $PSScriptRoot -ErrorAction Stop

function Invoke-Step([string] $Description, [scriptblock] $Command) {
    Write-Host "== $Description" -ForegroundColor Cyan
    & $Command
    if ($LASTEXITCODE -ne 0) {
        Write-Host "$Description failed (exit code $LASTEXITCODE)." -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

if ($Clean) {
    Write-Host "== Cleaning $BuildDirectory (keeping _deps) and executables" -ForegroundColor Cyan
    if (Test-Path -LiteralPath $BuildDirectory) {
        Get-ChildItem -LiteralPath $BuildDirectory -Force | Where-Object { $_.Name -ne '_deps' } | Remove-Item -Recurse -Force -ErrorAction Stop
    }
    if (Test-Path -LiteralPath 'executables') {
        Remove-Item -LiteralPath 'executables' -Recurse -Force -ErrorAction Stop
    }
}

$testsOption = if ($Tests) { 'ON' } else { 'OFF' }
Invoke-Step 'Configure' {
    cmake -S . -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 `
        -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON "-DEAW_BUILD_TESTS=$testsOption"
}

$configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
foreach ($config in $configurations) {
    Invoke-Step "Build $config" { cmake --build $BuildDirectory --config $config --parallel }
}

Write-Host "Build succeeded: executables\$($configurations -join ', executables\')" -ForegroundColor Green
