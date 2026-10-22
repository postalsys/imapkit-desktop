# Drives the app window with a list of steps, one per line, for repeatable screenshots (scripts/docs-screenshots.sh).
# Control ids are the ones of the page sources (host-win/src/*_page.cpp), the steps act on the visible page:
#
#   page <n>            shows page n (0 Server, 1 Connections, 2 Messages, 3 Client)
#   text <id> <text>    sets the text of a control
#   click <id>          clicks a button
#   select <id> <n>     selects item n of a combo box, as the user would
#   keys <id> <vk,...>  presses keys (virtual key codes) in a control, e.g. 39 is Right in a tab control
#   wait <ms>           waits
#
#   powershell -File ui-steps.ps1 <steps file>
param([Parameter(Mandatory)][string]$StepsFile)
$ErrorActionPreference = 'Stop'
Add-Type -Namespace Native -Name Win -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
[DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hwnd, uint command);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
[DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
[DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, string lParam);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
'@
$WM_SETTEXT = 0x000C; $WM_KEYDOWN = 0x0100; $WM_KEYUP = 0x0101; $WM_COMMAND = 0x0111; $BM_CLICK = 0x00F5
$CB_SETCURSEL = 0x014E; $CBN_SELCHANGE = 1

$main = [Native.Win]::FindWindow('ImapKitDesktopMain', [NullString]::Value)
if ($main -eq [IntPtr]::Zero) { throw 'ImapKit Desktop is not running' }

# the pages are children of the main window, the visible one takes the steps
function VisiblePage {
    $page = [Native.Win]::GetWindow($main, 5)
    while ($page -ne [IntPtr]::Zero -and -not [Native.Win]::IsWindowVisible($page)) { $page = [Native.Win]::GetWindow($page, 2) }
    return $page
}
function Control([int]$id) {
    $control = [Native.Win]::GetDlgItem((VisiblePage), $id)
    if ($control -eq [IntPtr]::Zero) { throw "No control $id on the visible page" }
    return $control
}

foreach ($line in Get-Content $StepsFile) {
    $line = $line.Trim()
    if (-not $line -or $line.StartsWith('#')) { continue }
    $step, $rest = $line -split '\s+', 2
    switch ($step) {
        'page' { [Native.Win]::SendMessage($main, $WM_COMMAND, [IntPtr](101 + [int]$rest), [IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 300 }
        'text' { $id, $value = $rest -split '\s+', 2; [Native.Win]::SendMessage((Control $id), $WM_SETTEXT, [IntPtr]::Zero, [string]$value) | Out-Null }
        'click' { [Native.Win]::SendMessage((Control $rest), $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }
        'select' {
            $id, $index = $rest -split '\s+'
            $combo = Control $id
            [Native.Win]::SendMessage($combo, $CB_SETCURSEL, [IntPtr][int]$index, [IntPtr]::Zero) | Out-Null
            [Native.Win]::SendMessage((VisiblePage), $WM_COMMAND, [IntPtr](($CBN_SELCHANGE -shl 16) -bor [int]$id), $combo) | Out-Null
        }
        'keys' {
            $id, $keys = $rest -split '\s+'
            $control = Control $id
            foreach ($key in $keys.Split(',')) {
                [Native.Win]::PostMessage($control, $WM_KEYDOWN, [IntPtr][int]$key, [IntPtr]::Zero) | Out-Null
                [Native.Win]::PostMessage($control, $WM_KEYUP, [IntPtr][int]$key, [IntPtr]::Zero) | Out-Null
                Start-Sleep -Milliseconds 150
            }
        }
        'wait' { Start-Sleep -Milliseconds ([int]$rest) }
        default { throw "Unknown step: $line" }
    }
}
