<#
.SYNOPSIS
    Runs the reference headless battle twice and checks its state hash; with -Extended, four more
    battles that cover what the reference does not.

.DESCRIPTION
    The battle every simulation change is checked against (AGENTS.md, "Build and validation"):
    Utapau, two opposing groups ordered to the center, 3000 ticks (50 seconds at 60 Hz), run twice with
    --check-determinism. Fails when the runs differ or the hash is not -Expected. A change that
    alters the hash on purpose says so and why, and updates the expected hashes here.

    -Extended adds, each run twice as well: a large Utapau battle (3,300 entities: squads, tanks,
    walkers), fighters at Kuat (flight, loops, rolls), capital ships at Kuat (space avoidance), and
    AT-ATs on Utapau (walks and turns played as clips).

.EXAMPLE
    .\scripts\check-battle.ps1
    .\scripts\check-battle.ps1 -Extended
    .\scripts\check-battle.ps1 -Configuration Debug -GameRoot 'D:\Games\Star Wars Empire at War'
#>
param(
    [ValidateSet('Debug', 'Release', 'DebugValidation')]
    [string] $Configuration = 'Release',
    # The reference battle's state hash at tick 3000 since the last change that altered the
    # simulation on purpose.
    [string] $Expected = '0xb8bb0bc00cc5dc3c',
    [switch] $Extended,
    [string] $GameRoot
)

Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot) -ErrorAction Stop
$suffix = switch ($Configuration) { 'Debug' { '_d' } 'DebugValidation' { '_dv' } default { '' } }
$engine = "executables\$Configuration\EAWEngine$suffix.exe"
if (-not (Test-Path -LiteralPath $engine)) {
    Write-Host "$engine is missing; build it with .\build.ps1 -Configuration $Configuration" -ForegroundColor Red
    exit 2
}

$utapau = 'DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED'
$kuat = 'DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED'
$battles = @(
    @{ Name = 'reference'; Expected = $Expected
        Arguments = @('--map', $utapau, '--spawn', 'Rebel_Infantry_Squad:2@1', '--spawn', 'T2B_Tank:3@2', '--auto-move', '--ticks', '3000') }
)
if ($Extended) {
    $battles += @(
        @{ Name = 'large Utapau'; Expected = '0x1b99ba1a99c2a6eb'
            Arguments = @('--map', $utapau, '--spawn', 'Rebel_Infantry_Squad:80@1', '--spawn', 'Imperial_Stormtrooper_Squad:80@2', '--spawn', 'T2B_Tank:60@1',
                '--spawn', 'AT_ST_Walker:40@2', '--auto-move', '--ticks', '1800') },
        @{ Name = 'Kuat fighters'; Expected = '0x2f9f088ae9d4ad5a'
            Arguments = @('--map', $kuat, '--spawn', 'Rebel_X-Wing_Squadron:4@1', '--spawn', 'TIE_Fighter_Squadron:4@2', '--ticks', '3000') },
        @{ Name = 'Kuat capital ships'; Expected = '0xd8ec09e272be2614'
            Arguments = @('--map', $kuat, '--spawn', 'Nebulon_B_Frigate:4@1', '--spawn', 'Corellian_Corvette:6@1', '--spawn', 'Acclamator_Assault_Ship:3@2',
                '--spawn', 'Tartan_Patrol_Cruiser:6@2', '--auto-move', '--ticks', '3000') },
        @{ Name = 'Utapau AT-ATs'; Expected = '0x7bceedb3786e5477'
            Arguments = @('--map', $utapau, '--spawn', 'AT_AT_Walker:4@2', '--spawn', 'Rebel_Infantry_Squad:6@1', '--spawn', 'T2B_Tank:4@1', '--auto-move', '--ticks', '3600') }
    )
}

$failed = 0
foreach ($battle in $battles) {
    $arguments = @('battle', '--game', 'foc') + $battle.Arguments + @('--check-determinism')
    if ($GameRoot) { $arguments += @('--game-root', $GameRoot) }
    $output = & $engine @arguments 2>&1 | ForEach-Object { "$_" }
    $exitCode = $LASTEXITCODE
    $hashLine = $output | Where-Object { $_ -match 'State hash: (0x[0-9a-f]+)' } | Select-Object -Last 1
    if ($exitCode -ne 0 -or -not $hashLine) {
        $output | Select-Object -Last 15 | ForEach-Object { Write-Host $_ }
        Write-Host "$($battle.Name): the battle failed (exit code $exitCode)." -ForegroundColor Red
        $failed += 1
        continue
    }
    $null = $hashLine -match 'State hash: (0x[0-9a-f]+)'
    $hash = $Matches[1]
    if ($hash -ne $battle.Expected) {
        Write-Host "$($battle.Name): state hash $hash differs from the expected $($battle.Expected)." -ForegroundColor Red
        $failed += 1
        continue
    }
    Write-Host "$($battle.Name): state hash $hash as expected; both runs agree." -ForegroundColor Green
}
if ($failed -gt 0) {
    Write-Host "$failed of $($battles.Count) battles failed." -ForegroundColor Red
    exit 1
}
