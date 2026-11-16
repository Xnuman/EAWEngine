<#
.SYNOPSIS
    Floods the sandbox with synthetic mouse movement at a high polling rate and checks that reading
    input stays cheap.

.DESCRIPTION
    Stands in for a high polling rate mouse (4-8 kHz): starts the sandbox, and while its window has
    the focus sends relative mouse moves through SendInput at -Rate per second for -Seconds,
    alternating one pixel right and one left so the cursor stays where it is (no clicks, no keys).
    The sandbox prints its input load at exit (raw input records a frame and the time reading all
    input took, average and worst frame); the check fails when the worst frame's read exceeds
    -MaxReadMilliseconds or no records arrived (the window never had the focus).

    It moves the real cursor by a pixel and back many times a second: do not use the computer while
    it runs. SendInput does not mimic a device's timing exactly (records come in bursts of the
    sender's loop), so it checks throughput, not a particular mouse.

.EXAMPLE
    .\scripts\check-input-load.ps1
    .\scripts\check-input-load.ps1 -Rate 4000 -Seconds 10
#>
param(
    [ValidateSet('Debug', 'Release', 'DebugValidation')]
    [string] $Configuration = 'Release',
    # Moves a second (8000: an 8 kHz mouse).
    [int] $Rate = 8000,
    [int] $Seconds = 10,
    # The worst frame's input read allowed (an engine choice: an eighth of a 60 FPS frame; the window's
    # first frames, with its startup messages, take up to about 0.8 ms without any input).
    [double] $MaxReadMilliseconds = 2.0,
    [string] $Map = 'DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED',
    [string] $GameRoot
)

Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot) -ErrorAction Stop
$suffix = switch ($Configuration) { 'Debug' { '_d' } 'DebugValidation' { '_dv' } default { '' } }
$engine = "executables\$Configuration\EAWEngine$suffix.exe"
if (-not (Test-Path -LiteralPath $engine)) {
    Write-Host "$engine is missing; build it with .\build.ps1 -Configuration $Configuration" -ForegroundColor Red
    exit 2
}

Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
public static class InputFlood {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; public long padding; }
    [DllImport("user32.dll", SetLastError = true)]
    static extern uint SendInput(uint count, INPUT[] inputs, int size);
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    const uint INPUT_MOUSE = 0, MOUSEEVENTF_MOVE = 1;
    // Sends `rate` relative moves a second for `seconds`, one at a time on a spinning clock;
    // returns how many were sent.
    public static long Run(int rate, double seconds) {
        var move = new INPUT[1];
        move[0].type = INPUT_MOUSE;
        move[0].mi.dwFlags = MOUSEEVENTF_MOVE;
        var clock = Stopwatch.StartNew();
        long sent = 0;
        long total = (long)(rate * seconds);
        while (sent < total) {
            long due = (long)(clock.Elapsed.TotalSeconds * rate);
            while (sent < due && sent < total) {
                move[0].mi.dx = (sent % 2 == 0) ? 1 : -1;
                if (SendInput(1, move, Marshal.SizeOf(typeof(INPUT))) == 1) { ++sent; } else { return sent; }
            }
        }
        return sent;
    }
}
'@

$arguments = @('sandbox', '--game', 'foc', '--map', $Map, '--panel', 'statistics')
if ($GameRoot) { $arguments += @('--game-root', $GameRoot) }
$output = Join-Path ([System.IO.Path]::GetTempPath()) "eaw-input-load-$PID.txt"
$process = Start-Process -FilePath $engine -ArgumentList $arguments -PassThru -NoNewWindow -RedirectStandardOutput $output

# Wait until the sandbox's window has the focus (the map is loading meanwhile).
$deadline = (Get-Date).AddSeconds(120)
$focused = $false
while ((Get-Date) -lt $deadline -and -not $process.HasExited) {
    $owner = [uint32]0
    [void][InputFlood]::GetWindowThreadProcessId([InputFlood]::GetForegroundWindow(), [ref]$owner)
    if ($owner -eq $process.Id) { $focused = $true; break }
    Start-Sleep -Milliseconds 250
}
if (-not $focused) {
    if (-not $process.HasExited) { $process.Kill() }
    Write-Host "FAIL: the sandbox's window never had the focus" -ForegroundColor Red
    exit 1
}
Start-Sleep -Seconds 3
$sent = [InputFlood]::Run($Rate, $Seconds)
# Close the sandbox (it prints its input load at exit).
[void]$process.CloseMainWindow()
if (-not $process.WaitForExit(30000)) { $process.Kill(); $process.WaitForExit() }
$line = Get-Content -LiteralPath $output | Where-Object { $_ -like 'Input *' } | Select-Object -Last 1
Remove-Item -LiteralPath $output -ErrorAction SilentlyContinue
Write-Host "Sent $sent moves in $Seconds s ($Rate a second)"
if (-not $line) {
    Write-Host 'FAIL: the sandbox printed no input load' -ForegroundColor Red
    exit 1
}
Write-Host $line
if ($line -notmatch 'max (\d+)\) \| read [\d.]+ ms \(max ([\d.]+)\)') {
    Write-Host 'FAIL: the input load line has an unexpected form' -ForegroundColor Red
    exit 1
}
$maxRecords = [int]$Matches[1]
$maxRead = [double]::Parse($Matches[2], [System.Globalization.CultureInfo]::InvariantCulture)
if ($maxRecords -eq 0) {
    Write-Host 'FAIL: no raw input records arrived (did the window keep the focus?)' -ForegroundColor Red
    exit 1
}
if ($maxRead -gt $MaxReadMilliseconds) {
    Write-Host "FAIL: the worst frame's input read took $maxRead ms (limit $MaxReadMilliseconds)" -ForegroundColor Red
    exit 1
}
Write-Host "PASS: worst frame $maxRecords records, read in $maxRead ms" -ForegroundColor Green
exit 0
