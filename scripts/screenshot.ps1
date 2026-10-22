# Saves a screenshot as PNG: of the app window (made topmost for a moment, so other windows do not cover it and the
# WebView2 content is on screen), or of the whole desktop with -Desktop. -Page 0, 1 or 2 switches the app to the Server, Connections or Messages page first.
# At full resolution also on scaled displays
# -ListKeys "40,40" posts these virtual keys (40 is Down) to the visible list view of the page, to select a message.
param([string]$Path, [switch]$Desktop, [int]$Page = -1, [string]$ListKeys = '')
$Title = 'ImapKit Desktop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -Namespace Native -Name User32 -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
[DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hwnd, uint command);
public struct RECT { public int Left, Top, Right, Bottom; }
'@
[Native.User32]::SetProcessDPIAware() | Out-Null
if (-not $Desktop) {
    $hwnd = [Native.User32]::FindWindow([NullString]::Value, $Title)
    if ($hwnd -eq [IntPtr]::Zero) { Write-Error "no window titled $Title"; exit 1 }
    if ($Page -ge 0) {
        # WM_COMMAND with IDM_VIEW_SERVER + page, see host-win/src/main.cpp
        [Native.User32]::PostMessage($hwnd, 0x0111, [IntPtr](101 + $Page), [IntPtr]::Zero) | Out-Null
        Start-Sleep -Milliseconds 700
    }
    if ($ListKeys) {
        # the pages are children of the main window, the visible page holds the list view
        $list = [IntPtr]::Zero
        $page = [Native.User32]::GetWindow($hwnd, 5)
        while ($page -ne [IntPtr]::Zero -and $list -eq [IntPtr]::Zero) {
            if ([Native.User32]::IsWindowVisible($page)) {
                $candidate = [Native.User32]::FindWindowEx($page, [IntPtr]::Zero, 'SysListView32', [NullString]::Value)
                if ($candidate -ne [IntPtr]::Zero) { $list = $candidate }
            }
            $page = [Native.User32]::GetWindow($page, 2)
        }
        foreach ($key in $ListKeys.Split(',')) {
            [Native.User32]::PostMessage($list, 0x0100, [IntPtr][int]$key, [IntPtr]::Zero) | Out-Null
            [Native.User32]::PostMessage($list, 0x0101, [IntPtr][int]$key, [IntPtr]::Zero) | Out-Null
            Start-Sleep -Milliseconds 150
        }
        Start-Sleep -Milliseconds 1200
    }
    # HWND_TOPMOST without moving, sizing or activating, then back to HWND_NOTOPMOST
    [Native.User32]::SetWindowPos($hwnd, [IntPtr](-1), 0, 0, 0, 0, 0x13) | Out-Null
    Start-Sleep -Milliseconds 400
    $rect = New-Object Native.User32+RECT
    [Native.User32]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
    $bitmap = New-Object System.Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    [Native.User32]::SetWindowPos($hwnd, [IntPtr](-2), 0, 0, 0, 0, 0x13) | Out-Null
} else {
    $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
}
$bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
