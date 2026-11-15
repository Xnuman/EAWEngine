<#
.SYNOPSIS
    Measures headless battle frames against the 60 FPS budget while the units keep getting new
    orders, at 10, 50, 100, and 500 units.

.DESCRIPTION
    Not part of every change: run it when a change risks route planning or simulation performance
    (pathfinding, movement, group cohesion, targeting). For each unit count, the map is built and the
    units spawned, then EAWEngine battle --stress-orders runs -Seconds of battle at 60 Hz: every
    second each group of up to eight units gets a new order (move, attack-move, attack, guard) to
    a random point or a hostile target, and the spawned units cannot be killed, so the load stays
    that of every unit. Half the units are each side's; each side is half single vehicles, half
    infantry companies (a company counts as one unit; it lands as two or three squads, each a team
    entity and its soldiers).

    A frame is the tick a 60 Hz frame runs, without rendering. Prints, per count, the frames over
    the budget (16.7 ms), the average, the 99th percentile, the worst, and the route searches, and
    fails when any frame went over. The tick that applies the spawns is loading and not measured;
    the first tick of fighting is. Timings are only representative in Release on a quiet machine.

.EXAMPLE
    .\scripts\benchmark-battle.ps1
    .\scripts\benchmark-battle.ps1 -Counts 100,500 -Seconds 20
    .\scripts\benchmark-battle.ps1 -GameRoot 'D:\Games\Star Wars Empire at War'
#>
param(
    [int[]] $Counts = @(10, 50, 100, 500),
    [ValidateRange(1, 3600)]
    [int] $Seconds = 10,
    [string] $Map = 'DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED',
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [string] $GameRoot
)

Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot) -ErrorAction Stop
$suffix = if ($Configuration -eq 'Debug') { '_d' } else { '' }
$engine = "executables\$Configuration\EAWEngine$suffix.exe"
if (-not (Test-Path -LiteralPath $engine)) {
    Write-Host "$engine is missing; build it with .\build.ps1 -Configuration $Configuration" -ForegroundColor Red
    exit 2
}

# The engine's default rate; the first tick applies the spawns and is not measured.
$ticksPerSecond = 60
$ticks = 1 + $Seconds * $ticksPerSecond

$failed = 0
$rows = @()
foreach ($count in $Counts) {
    if ($count -lt 2) {
        Write-Host "A battle needs at least 2 units (one a side); $count skipped." -ForegroundColor Yellow
        continue
    }
    $side = [math]::Floor($count / 2)
    $vehicles = [math]::Ceiling($side / 2)
    $squads = $side - $vehicles
    $spawns = @('--spawn', "T2B_Tank:$vehicles@1", '--spawn', "AT_ST_Walker:$vehicles@2")
    if ($squads -gt 0) {
        $spawns += @('--spawn', "Rebel_Infantry_Squad:$squads@1", '--spawn', "Imperial_Stormtrooper_Squad:$squads@2")
    }
    $arguments = @('battle', '--game', 'foc', '--map', $Map) + $spawns + @('--stress-orders', '--ticks', "$ticks")
    if ($GameRoot) { $arguments += @('--game-root', $GameRoot) }
    $output = & $engine @arguments 2>&1 | ForEach-Object { "$_" }
    $exitCode = $LASTEXITCODE
    $frames = $output | Where-Object { $_ -match '^Frame budget' } | Select-Object -Last 1
    $entities = $output | Where-Object { $_ -match '^Entities: (\d+) at start, (\d+) at end' } | Select-Object -Last 1
    $paths = $output | Where-Object { $_ -match '^Paths: (\d+) planned' } | Select-Object -Last 1
    if ($exitCode -ne 0 -or -not $frames -or -not $entities -or -not $paths) {
        $output | Select-Object -Last 15 | ForEach-Object { Write-Host $_ }
        Write-Host "$count units: the battle failed (exit code $exitCode)." -ForegroundColor Red
        $failed += 1
        continue
    }
    $null = $frames -match 'ms: (\d+) of (\d+) frames over \| average ([\d.]+) ms, 99th percentile ([\d.]+) ms, worst ([\d.]+) ms'
    $over = [int]$Matches[1]
    $row = [pscustomobject]@{
        units = $count; frames = [int]$Matches[2]; over = $over; average_ms = $Matches[3]; p99_ms = $Matches[4]; worst_ms = $Matches[5]
    }
    $null = $entities -match 'Entities: (\d+) at start, (\d+) at end'
    $row | Add-Member entities ([int]$Matches[2] - [int]$Matches[1])
    $null = $paths -match 'Paths: (\d+) planned'
    $row | Add-Member route_searches ([int]$Matches[1])
    $rows += $row
    if ($over -gt 0) { $failed += 1 }
}

$rows | Format-Table units, entities, frames, over, average_ms, p99_ms, worst_ms, route_searches -AutoSize | Out-String | Write-Host
if ($failed -gt 0) {
    Write-Host "FAIL: $failed of $($Counts.Count) unit counts had frames over the 16.7 ms budget or did not run." -ForegroundColor Red
    exit 1
}
Write-Host "PASS: every frame within the 16.7 ms budget." -ForegroundColor Green
exit 0
