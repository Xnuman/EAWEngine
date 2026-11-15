<#
.SYNOPSIS
    Compares two EAWEngine path-audit reports pair by pair.

.DESCRIPTION
    Joins the path-audit.tsv of -Reference and -Candidate (folders written by EAWEngine path-audit
    --report) on map, movement class, and pair, and prints for each movement class and overall:
    pairs whose reach changed (reached in one, partial in the other), the nodes expanded and the time
    in the candidate against the reference (sums, so large searches weigh by their cost), and how the
    paths' lengths changed for pairs both reached (the mean ratio and the share that got longer by
    more than 1%). Fails when a pair one run planned is missing from the other (the runs did not plan
    the same pairs: other maps, movement classes, or --pairs). Keep reports outside the repository.

.EXAMPLE
    .\executables\Release\EAWEngine.exe path-audit --game foc --report D:\eaw-audit\before
    .\executables\Release\EAWEngine.exe path-audit --game foc --report D:\eaw-audit\after
    .\scripts\compare-path-audit.ps1 -Reference D:\eaw-audit\before -Candidate D:\eaw-audit\after
#>
param(
    [Parameter(Mandatory)]
    [string] $Reference,
    [Parameter(Mandatory)]
    [string] $Candidate
)

$files = @{}
foreach ($name in @('Reference', 'Candidate')) {
    $folder = Get-Variable -Name $name -ValueOnly
    $file = Join-Path $folder 'path-audit.tsv'
    if (-not (Test-Path -LiteralPath $file)) {
        Write-Host "$file does not exist; write it with EAWEngine path-audit --report $folder" -ForegroundColor Red
        exit 2
    }
    $files[$name] = $file
}

$before = @{}
foreach ($row in (Import-Csv -LiteralPath $files.Reference -Delimiter "`t")) {
    $before["$($row.map)|$($row.class)|$($row.pair)"] = $row
}
$classes = [ordered]@{}
$missing = 0
$seen = 0
foreach ($row in (Import-Csv -LiteralPath $files.Candidate -Delimiter "`t")) {
    $key = "$($row.map)|$($row.class)|$($row.pair)"
    if (-not $before.ContainsKey($key)) {
        $missing += 1
        continue
    }
    $seen += 1
    $old = $before[$key]
    foreach ($group in @($row.class, 'all')) {
        if (-not $classes.Contains($group)) {
            $classes[$group] = [pscustomobject]@{ pairs = 0; reach_changed = 0; old_nodes = 0.0; new_nodes = 0.0; old_us = 0.0; new_us = 0.0; both = 0; ratio_sum = 0.0; longer = 0 }
        }
        $c = $classes[$group]
        $c.pairs += 1
        if ($old.status -ne $row.status) { $c.reach_changed += 1 }
        $c.old_nodes += [double]$old.expanded
        $c.new_nodes += [double]$row.expanded
        $c.old_us += [double]$old.microseconds
        $c.new_us += [double]$row.microseconds
        if ($old.status -eq 'reached' -and $row.status -eq 'reached' -and [double]$old.length -gt 0) {
            $ratio = [double]$row.length / [double]$old.length
            $c.both += 1
            $c.ratio_sum += $ratio
            if ($ratio -gt 1.01) { $c.longer += 1 }
        }
    }
}
$missing += $before.Count - $seen

Write-Host ('{0,-48}{1,8}{2,10}{3,10}{4,10}{5,12}{6,10}' -f 'class', 'pairs', 'reach +/-', 'nodes x', 'time x', 'length x', 'longer')
foreach ($group in $classes.Keys) {
    $c = $classes[$group]
    $nodes = if ($c.old_nodes -gt 0) { $c.new_nodes / $c.old_nodes } else { 0 }
    $time = if ($c.old_us -gt 0) { $c.new_us / $c.old_us } else { 0 }
    $length = if ($c.both -gt 0) { $c.ratio_sum / $c.both } else { 0 }
    $longer = if ($c.both -gt 0) { '{0:P1}' -f ($c.longer / $c.both) } else { '-' }
    Write-Host ('{0,-48}{1,8}{2,10}{3,10:N3}{4,10:N3}{5,12:N4}{6,10}' -f $group, $c.pairs, $c.reach_changed, $nodes, $time, $length, $longer)
}
if ($missing -gt 0) {
    Write-Host "FAIL: $missing pairs appear in only one report (the runs planned different pairs)." -ForegroundColor Red
    exit 1
}
Write-Host "Compared $seen pairs." -ForegroundColor Green
exit 0
