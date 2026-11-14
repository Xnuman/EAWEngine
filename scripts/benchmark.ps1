<#
.SYNOPSIS
    Runs EAWBenchmarks and compares the result with an earlier run.

.DESCRIPTION
    Times every suite serially and on the job system (EAWBenchmarks, docs/tools.md), writes the
    results to -Output (tab-separated), and, with -Reference, prints each configuration's median
    against the reference's and the change in percent. Keep result files outside the repository
    (they belong to one machine). Fails when a job-system run's output differs from the serial
    run's. Timings are only representative in Release.

.EXAMPLE
    .\scripts\benchmark.ps1 -Output D:\bench\before.tsv
    .\scripts\benchmark.ps1 -Output D:\bench\after.tsv -Reference D:\bench\before.tsv
    .\scripts\benchmark.ps1 -Output D:\bench\paths.tsv -Suites paths -Workers 0,3,7 -Repeat 9
#>
param(
    [Parameter(Mandatory)]
    [string] $Output,
    [string] $Reference,
    [string[]] $Suites = @(),
    # Worker counts for the job system; empty runs EAWBenchmarks' default (0, 1, 2, 4, and all).
    [string] $Workers,
    [int] $Repeat = 7,
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release'
)

Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot) -ErrorAction Stop
$suffix = if ($Configuration -eq 'Debug') { '_d' } else { '' }
$bench = "executables\$Configuration\EAWBenchmarks$suffix.exe"
if (-not (Test-Path -LiteralPath $bench)) {
    Write-Host "$bench is missing; build it with .\build.ps1 -Configuration $Configuration" -ForegroundColor Red
    exit 2
}
if ($Reference -and -not (Test-Path -LiteralPath $Reference)) {
    Write-Host "The reference $Reference does not exist." -ForegroundColor Red
    exit 2
}

$arguments = @($Suites) + @('--repeat', "$Repeat", '--output', $Output)
if ($Workers) { $arguments += @('--workers', $Workers) }
& $bench @arguments
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    Write-Host "EAWBenchmarks failed (exit code $exitCode)." -ForegroundColor Red
    exit 1
}

if ($Reference) {
    $before = @{}
    foreach ($row in (Import-Csv -LiteralPath $Reference -Delimiter "`t")) {
        $before["$($row.suite)/$($row.variant)"] = [double]$row.median_ms
    }
    Write-Host ''
    Write-Host ('{0,-14}{1,-10}{2,14}{3,14}{4,10}' -f 'suite', 'variant', 'reference ms', 'now ms', 'change')
    foreach ($row in (Import-Csv -LiteralPath $Output -Delimiter "`t")) {
        $key = "$($row.suite)/$($row.variant)"
        $now = [double]$row.median_ms
        if ($before.ContainsKey($key) -and $before[$key] -gt 0) {
            $change = ($now - $before[$key]) / $before[$key] * 100.0
            $color = if ($change -le -5) { 'Green' } elseif ($change -ge 5) { 'Red' } else { 'Gray' }
            Write-Host ('{0,-14}{1,-10}{2,14:N3}{3,14:N3}{4,9:+0.0;-0.0}%' -f $row.suite, $row.variant, $before[$key], $now, $change) -ForegroundColor $color
        }
        else {
            Write-Host ('{0,-14}{1,-10}{2,14}{3,14:N3}{4,10}' -f $row.suite, $row.variant, '-', $now, 'new')
        }
    }
}
Write-Host "Benchmarks done; every job-system run matched the serial output. Results in $Output." -ForegroundColor Green
