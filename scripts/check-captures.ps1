<#
.SYNOPSIS
    Renders the reference scenes on WARP and compares them with an earlier capture set.

.DESCRIPTION
    The captures a change that should not alter the look is checked against (AGENTS.md, "Build and
    validation"): a Kuat fighter battle at two moments, a Utapau land battle, a capital ship duel,
    the dark trooper model, and the validation scene, each at a fixed frame on the fixed
    presentation clock (docs/tools.md, "Captures and image regression"). Captures go to -Output;
    with -Reference, each is compared with the file of the same name there by
    EAWEngine compare-images. Captures of the stock game derive from protected data: keep both
    folders outside the repository.

.EXAMPLE
    .\scripts\check-captures.ps1 -Output D:\eaw-captures\before
    .\scripts\check-captures.ps1 -Output D:\eaw-captures\after -Reference D:\eaw-captures\before
#>
param(
    [Parameter(Mandatory)]
    [string] $Output,
    [string] $Reference,
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
New-Item -ItemType Directory -Force -Path $Output -ErrorAction Stop | Out-Null

$common = @('sandbox', '--game', 'foc', '--warp', '--vsync', 'off', '--width', '960', '--height', '540')
if ($GameRoot) { $common += @('--game-root', $GameRoot) }
$kuat = @('--map', 'DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED', '--spawn', 'Rebel_X-Wing_Squadron:2@1', '--spawn', 'TIE_Fighter_Squadron:2@2', '--auto-move')
$scenes = [ordered]@{
    'kuat_90'        = $kuat + @('--capture-frame', '90')
    'kuat_300'       = $kuat + @('--capture-frame', '300')
    'utapau_200'     = @('--map', 'DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED', '--spawn', 'Rebel_Infantry_Squad:2@1', '--spawn', 'T2B_Tank:3@2', '--auto-move', '--capture-frame', '200')
    'capital_120'    = @('--map', 'DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED', '--spawn', 'Star_Destroyer:1@2', '--spawn', 'Calamari_Cruiser:1@1', '--spawn', 'Rebel_X-Wing_Squadron:1@1', '--auto-move', '--capture-frame', '120')
    'darktrooper_20' = @('--model', 'DATA/ART/MODELS/EI_DARKTROOPER_ONE.ALO', '--capture-frame', '20')
    'default_5'      = @('--capture-frame', '5')
}

$failed = 0
foreach ($name in $scenes.Keys) {
    $image = Join-Path $Output "$name.png"
    & $engine @common @($scenes[$name]) '--capture' $image *> (Join-Path $Output "$name.log")
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $image)) {
        Write-Host "$name`: capture failed (exit code $LASTEXITCODE; see $name.log)" -ForegroundColor Red
        $failed++
        continue
    }
    if (-not $Reference) {
        Write-Host "$name`: captured"
        continue
    }
    $before = Join-Path $Reference "$name.png"
    if (-not (Test-Path -LiteralPath $before)) {
        Write-Host "$name`: no reference capture in $Reference" -ForegroundColor Yellow
        $failed++
        continue
    }
    $result = & $engine 'compare-images' $before $image '--diff' (Join-Path $Output "$name.diff.png") 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -eq 0) {
        Write-Host "$name`: unchanged ($($result | Select-Object -Last 1))"
    } else {
        Write-Host "$name`: CHANGED ($($result | Select-Object -Last 1))" -ForegroundColor Red
        $failed++
    }
}
if ($failed -gt 0) {
    Write-Host "$failed of $($scenes.Count) scenes failed or changed." -ForegroundColor Red
    exit 1
}
Write-Host "All $($scenes.Count) scenes $(if ($Reference) { 'match the reference' } else { 'captured' })." -ForegroundColor Green
