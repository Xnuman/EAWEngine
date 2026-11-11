<#
.SYNOPSIS
    Runs the reference headless battle twice and checks its state hash.

.DESCRIPTION
    The battle every simulation change is checked against (AGENTS.md, "Build and validation"):
    Utapau, two opposing groups ordered to the center, 1500 ticks, run twice with
    --check-determinism. Fails when the runs differ or the hash is not -Expected. A change that
    alters the hash on purpose says so and why, and updates -Expected's default here.

.EXAMPLE
    .\scripts\check-battle.ps1
    .\scripts\check-battle.ps1 -Configuration Debug -GameRoot 'D:\Games\Star Wars Empire at War'
#>
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    # The state hash at tick 1500 since the last change that altered the simulation on purpose.
    [string] $Expected = '0xd2ccd9c1fee34a51',
    [string] $GameRoot
)

Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot) -ErrorAction Stop
$suffix = if ($Configuration -eq 'Debug') { '_d' } else { '' }
$engine = "executables\$Configuration\EAWEngine$suffix.exe"
if (-not (Test-Path -LiteralPath $engine)) {
    Write-Host "$engine is missing; build it with .\build.ps1 -Configuration $Configuration" -ForegroundColor Red
    exit 2
}

$arguments = @('battle', '--game', 'foc', '--map', 'DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED',
    '--spawn', 'Rebel_Infantry_Squad:2@1', '--spawn', 'T2B_Tank:3@2', '--auto-move', '--ticks', '1500', '--check-determinism')
if ($GameRoot) { $arguments += @('--game-root', $GameRoot) }

$output = & $engine @arguments 2>&1 | ForEach-Object { "$_" }
$exitCode = $LASTEXITCODE
$hashLine = $output | Where-Object { $_ -match 'State hash: (0x[0-9a-f]+)' } | Select-Object -Last 1
if ($exitCode -ne 0 -or -not $hashLine) {
    $output | Select-Object -Last 15 | ForEach-Object { Write-Host $_ }
    Write-Host "The battle failed (exit code $exitCode)." -ForegroundColor Red
    exit 1
}
$null = $hashLine -match 'State hash: (0x[0-9a-f]+)'
$hash = $Matches[1]
if ($hash -ne $Expected) {
    Write-Host "State hash $hash differs from the expected $Expected." -ForegroundColor Red
    exit 1
}
Write-Host "State hash $hash as expected; both runs agree." -ForegroundColor Green
