param([string]$Out, [string]$ProcName = "hml_designer")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W2 { [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; } }
"@
$p = Get-Process -Name $ProcName -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
[W2]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 600
$r = New-Object W2+RECT; [W2]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$w = [Math]::Min($r.R - $r.L, 1920); $h = [Math]::Min($r.B - $r.T, 1080)
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen([Math]::Max($r.L,0), [Math]::Max($r.T,0), 0, 0, $bmp.Size); $g.Dispose()
$bmp.Save($Out); $bmp.Dispose()
"saved $Out ${w}x${h}"
