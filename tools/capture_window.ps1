param(
    [string]$Exe,
    [string]$OutPng,
    [int]$WaitSec = 5,
    [string[]]$ExtraArgs = @()
)

Add-Type -AssemblyName System.Windows.Forms, System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Cap {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
}
"@

if ($ExtraArgs.Count -gt 0) {
    $p = Start-Process -FilePath $Exe -ArgumentList $ExtraArgs -PassThru
} else {
    $p = Start-Process -FilePath $Exe -PassThru
}
Start-Sleep -Seconds $WaitSec
$p.Refresh()
if ($p.HasExited) { Write-Output "PROCESS_EXITED"; exit 1 }

$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Output "NO_WINDOW_HANDLE"; Stop-Process -Id $p.Id -Force; exit 1 }

# 把窗口移到 (0,0) 并置顶, 避免被别的窗口遮挡影响 PrintWindow 结果
[void][Win32Cap]::ShowWindow($h, 9)
[void][Win32Cap]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0001 -bor 0x0040)  # HWND_TOPMOST, NOSIZE|SHOWWINDOW
Start-Sleep -Milliseconds 1000

$r = New-Object Win32Cap+RECT
[void][Win32Cap]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left
$hh = $r.Bottom - $r.Top
Write-Output "WINDOW_RECT=$($r.Left),$($r.Top) ${w}x${hh}"

$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [Win32Cap]::PrintWindow($h, $hdc, 2)   # PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)
$g.Dispose()
Write-Output "PrintWindow=$ok"

if (-not $ok) {
    # 退路: 直接屏幕拷贝
    $g2 = [System.Drawing.Graphics]::FromImage($bmp)
    $g2.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $g2.Dispose()
    Write-Output "fallback=CopyFromScreen"
}

$bmp.Save($OutPng, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "SAVED=$OutPng"

Stop-Process -Id $p.Id -Force
Write-Output "DONE"
