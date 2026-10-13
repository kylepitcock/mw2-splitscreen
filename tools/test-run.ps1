# Runs IW4x with the split screen mod for a fixed time and collects results.
# The user's players\ folder is snapshotted before and restored after, because
# test launches change archived dvars (window mode, resolution, ...).
param(
    [int]$Players = 2,
    [int]$Seconds = 75,
    [string]$Map = "mp_rust",
    [string]$Extra = "",
    [int[]]$ShotsAt = @(45, 70),
    [switch]$NoBuild,
    [switch]$NoMod,
    [string]$Launch = "+set sv_securityLevel 0 +set sv_lanOnly 1"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$game = (Get-Content "$root\game-path.txt").Trim()
$out = "$root\build\test"
New-Item -ItemType Directory -Force $out | Out-Null
Remove-Item "$out\*" -Force -ErrorAction SilentlyContinue

if (-not $NoBuild) { & "$root\build.ps1" | Out-Null }
if ($NoMod) { Remove-Item "$game\dsound.dll" -ErrorAction SilentlyContinue } else { Copy-Item "$root\build\Release\dsound.dll" "$game\dsound.dll" -Force }

if (Get-Process iw4x -ErrorAction SilentlyContinue) { throw "iw4x is already running" }
$snapshot = "$root\.backup\players-run"
Remove-Item -Recurse -Force $snapshot -ErrorAction SilentlyContinue
Copy-Item "$game\players" $snapshot -Recurse

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class TW {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  // "Set Optimal Settings?" appears randomly (sys_configSum includes a measured CPU GHz); answer No.
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  public static void Focus(IntPtr h) { if (h == IntPtr.Zero) return; keybd_event(0x12, 0, 0, IntPtr.Zero); keybd_event(0x12, 0, 2, IntPtr.Zero); SetForegroundWindow(h); }
  public static void DismissDialogs() { IntPtr d = FindWindow("#32770", "Set Optimal Settings?"); if (d != IntPtr.Zero) PostMessage(d, 0x111, (IntPtr)7, IntPtr.Zero); }
  public struct RECT { public int L, T, R, B; }
}
"@
[TW]::SetProcessDPIAware() | Out-Null

$gameArgs = "-stdout +set r_fullscreen 0 +set r_mode 1280x720 +set vid_xpos 40 +set vid_ypos 40 $Launch -ss_autotest $Players $Extra" + $(if ($Map) { " +map $Map" } else { "" })
$p = Start-Process "$game\iw4x.exe" -ArgumentList $gameArgs -WorkingDirectory $game -PassThru `
    -RedirectStandardOutput "$out\stdout.txt" -RedirectStandardError "$out\stderr.txt"
try {
    $start = Get-Date
    $shot = 0
    $focused = $false
    while (((Get-Date) - $start).TotalSeconds -lt $Seconds -and -not $p.HasExited) {
        Start-Sleep -Milliseconds 500
        [TW]::DismissDialogs()
        if (-not $focused) { $p.Refresh(); if ($p.MainWindowHandle -ne [IntPtr]::Zero) { [TW]::Focus($p.MainWindowHandle); $focused = $true } }
        $t = ((Get-Date) - $start).TotalSeconds
        if ($shot -lt $ShotsAt.Count -and $t -ge $ShotsAt[$shot]) {
            $p.Refresh()
            $r = New-Object TW+RECT
            if ([TW]::GetWindowRect($p.MainWindowHandle, [ref]$r) -and $r.R -gt $r.L) {
                $bmp = New-Object Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
                $g = [Drawing.Graphics]::FromImage($bmp)
                $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
                $bmp.Save("$out\shot$shot.png")
                $g.Dispose(); $bmp.Dispose()
            }
            $shot++
        }
    }
    $exited = $p.HasExited
    "exited early: $exited" + $(if ($exited) { " code $($p.ExitCode)" })
}
finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Start-Sleep 2 }
    Remove-Item -Recurse -Force "$game\players"
    Copy-Item $snapshot "$game\players" -Recurse
    Copy-Item "$game\splitscreen.log" "$out\splitscreen.log" -ErrorAction SilentlyContinue
}
