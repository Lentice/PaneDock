# Opt-in end-to-end smoke of everyday user flows against the real PaneDock.exe.
#
# It opens a visible window, so it is not part of the default ctest run (see
# docs/testing.md, "User flows smoke"). It drives the app only through targeted
# messages to PaneDock's own HWNDs: no SendInput, no cursor movement, no
# keyboard state changes, so a user can keep working while it runs. Keys are
# posted to the target window without modifiers; anything that needs Ctrl/Alt,
# OLE drag and drop, or a native Shell menu is in the manual checklist instead.
#
# Exit codes: 0 every step passed, 1 at least one step failed, 77 skipped
# (another PaneDock.exe is running; ctest maps 77 to "skipped").
param(
    [Parameter(Mandatory = $true)]
    [string] $AppPath,
    [int] $StepTimeoutSeconds = 20,
    [switch] $KeepFixtures
)

$ErrorActionPreference = 'Stop'
$SkipExitCode = 77

$resolvedAppPath = (Resolve-Path -LiteralPath $AppPath).Path
$appName = [IO.Path]::GetFileNameWithoutExtension($resolvedAppPath)
# Never disturb the user's own instance, and never hit the single-instance relay.
$running = @(Get-Process -Name $appName -ErrorAction SilentlyContinue)
if ($running.Count -ne 0) {
    Write-Host "SKIP: $appName is already running (PID $($running.Id -join ', ')); close it and rerun."
    exit $SkipExitCode
}

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class PaneDockFlows {
    public delegate bool EnumProc(IntPtr window, IntPtr lparam);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr lparam);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr lparam);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int length);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr window);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr window, ref POINT point);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr wparam, IntPtr lparam, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageTimeoutW")]
    static extern IntPtr SendTextTimeout(IntPtr window, uint message, IntPtr wparam, string text, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageTimeoutW")]
    static extern IntPtr GetTextTimeout(IntPtr window, uint message, IntPtr wparam, StringBuilder text, uint flags, uint timeout, out IntPtr result);
    [DllImport("shlwapi.dll", CharSet = CharSet.Unicode)] public static extern int StrCmpLogicalW(string a, string b);

    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint type, uint protect);
    [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr process, IntPtr address, UIntPtr size, uint type);
    [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] buffer, UIntPtr size, out UIntPtr written);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, UIntPtr size, out UIntPtr read);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr process, out uint code);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr CreateFileW(string name, uint access, uint share, ref SECURITY_ATTRIBUTES security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcessW(string application, StringBuilder commandLine, IntPtr processAttributes, IntPtr threadAttributes,
        bool inheritHandles, uint flags, IntPtr environment, string directory, ref STARTUPINFOEX startup, out PROCESS_INFORMATION info);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool InitializeProcThreadAttributeList(IntPtr list, int count, int flags, ref IntPtr size);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool UpdateProcThreadAttribute(IntPtr list, uint flags, IntPtr attribute, IntPtr value, IntPtr size, IntPtr previous, IntPtr returnSize);
    [DllImport("kernel32.dll")] static extern void DeleteProcThreadAttributeList(IntPtr list);

    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] struct SECURITY_ATTRIBUTES { public int nLength; public IntPtr lpSecurityDescriptor; public bool bInheritHandle; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO {
        public int cb; public string lpReserved, lpDesktop, lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)] struct STARTUPINFOEX { public STARTUPINFO StartupInfo; public IntPtr lpAttributeList; }
    [StructLayout(LayoutKind.Sequential)] struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }

    const uint SMTO_ABORTIFHUNG = 0x2, SMTO_BLOCK = 0x1;
    const uint Timeout = 10000;

    static void PerMonitor() { SetThreadDpiAwarenessContext(new IntPtr(-4)); }

    // ---- process -------------------------------------------------------
    public static IntPtr ProcessHandle;
    public static int ProcessId;

    // SW_SHOWNOACTIVATE: the window is shown and laid out normally but does not
    // take the foreground from whatever the user is working in.
    public static int Launch(string app, string arguments, string stdoutPath) {
        var security = new SECURITY_ATTRIBUTES { nLength = Marshal.SizeOf(typeof(SECURITY_ATTRIBUTES)), bInheritHandle = true };
        IntPtr output = CreateFileW(stdoutPath, 0x40000000, 0x3, ref security, 2, 0x80, IntPtr.Zero);
        if (output == new IntPtr(-1)) throw new InvalidOperationException("cannot create " + stdoutPath);
        var startup = new STARTUPINFOEX();
        startup.StartupInfo.cb = Marshal.SizeOf(typeof(STARTUPINFOEX));
        startup.StartupInfo.dwFlags = 0x1 | 0x100; // STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES
        startup.StartupInfo.wShowWindow = 4;       // SW_SHOWNOACTIVATE
        startup.StartupInfo.hStdOutput = output; startup.StartupInfo.hStdError = output;
        // Inherit only the output file. Inheriting every inheritable handle
        // would hand the app this script's own stdout pipe, and a caller that
        // reads that pipe would then wait on the app's lifetime.
        IntPtr size = IntPtr.Zero;
        InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref size);
        IntPtr attributes = Marshal.AllocHGlobal(size);
        IntPtr handles = Marshal.AllocHGlobal(IntPtr.Size);
        PROCESS_INFORMATION info;
        bool created = false;
        int error = 0;
        try {
            Marshal.WriteIntPtr(handles, output);
            if (!InitializeProcThreadAttributeList(attributes, 1, 0, ref size) ||
                !UpdateProcThreadAttribute(attributes, 0, new IntPtr(0x20002), handles, new IntPtr(IntPtr.Size), IntPtr.Zero, IntPtr.Zero))
                throw new InvalidOperationException("handle list setup failed: " + Marshal.GetLastWin32Error());
            startup.lpAttributeList = attributes;
            var command = new StringBuilder("\"" + app + "\" " + arguments);
            created = CreateProcessW(app, command, IntPtr.Zero, IntPtr.Zero, true, 0x00080000 /* EXTENDED_STARTUPINFO_PRESENT */,
                IntPtr.Zero, System.IO.Path.GetDirectoryName(app), ref startup, out info);
            error = Marshal.GetLastWin32Error();
            DeleteProcThreadAttributeList(attributes);
        } finally {
            Marshal.FreeHGlobal(handles); Marshal.FreeHGlobal(attributes); CloseHandle(output);
        }
        if (!created) throw new InvalidOperationException("CreateProcess failed: " + error);
        CloseHandle(info.hThread);
        ProcessHandle = info.hProcess;
        ProcessId = info.dwProcessId;
        return info.dwProcessId;
    }
    public static bool HasExited() { return ProcessHandle == IntPtr.Zero || WaitForSingleObject(ProcessHandle, 0) == 0; }
    public static bool WaitExit(uint milliseconds) { return WaitForSingleObject(ProcessHandle, milliseconds) == 0; }
    public static int ExitCode() { uint code; GetExitCodeProcess(ProcessHandle, out code); return unchecked((int)code); }
    public static void ReleaseProcess() { if (ProcessHandle != IntPtr.Zero) CloseHandle(ProcessHandle); ProcessHandle = IntPtr.Zero; }

    // ---- windows -------------------------------------------------------
    public static string ClassOf(IntPtr window) {
        var name = new StringBuilder(256); GetClassName(window, name, name.Capacity); return name.ToString();
    }
    static uint Owner(IntPtr window) { uint pid; GetWindowThreadProcessId(window, out pid); return pid; }

    public static IntPtr FindTopLevel(string className, string title) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, unused) => {
            if (Owner(window) != (uint)ProcessId || ClassOf(window) != className) return true;
            if (!string.IsNullOrEmpty(title) && GetText(window) != title) return true;
            if (className != "PaneDockMainWindow" && !IsWindowVisible(window)) return true;
            found = window; return false;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr[] Descendants(IntPtr parent, string className, bool visibleOnly) {
        var list = new List<IntPtr>();
        EnumChildWindows(parent, (window, unused) => {
            if (ClassOf(window) == className && (!visibleOnly || IsWindowVisible(window))) list.Add(window);
            return true;
        }, IntPtr.Zero);
        return list.ToArray();
    }

    // Visible panes in reading order: top-left first.
    public static IntPtr[] VisiblePanes(IntPtr main) {
        PerMonitor();
        var panes = new List<IntPtr>(Descendants(main, "PaneDock.Pane", true));
        panes.Sort((a, b) => {
            RECT ra, rb; GetWindowRect(a, out ra); GetWindowRect(b, out rb);
            int rowA = ra.Top / 40, rowB = rb.Top / 40;
            return rowA != rowB ? rowA.CompareTo(rowB) : ra.Left.CompareTo(rb.Left);
        });
        return panes.ToArray();
    }

    public static string GetText(IntPtr window) {
        IntPtr length;
        if (SendMessageTimeout(window, 0x000E, IntPtr.Zero, IntPtr.Zero, SMTO_ABORTIFHUNG | SMTO_BLOCK, Timeout, out length) == IntPtr.Zero)
            return null;
        var text = new StringBuilder(length.ToInt32() + 2);
        IntPtr copied;
        if (GetTextTimeout(window, 0x000D, new IntPtr(text.Capacity), text, SMTO_ABORTIFHUNG | SMTO_BLOCK, Timeout, out copied) == IntPtr.Zero)
            return null;
        return text.ToString();
    }
    public static bool SetText(IntPtr window, string text) {
        IntPtr result;
        return SendTextTimeout(window, 0x000C, IntPtr.Zero, text, SMTO_ABORTIFHUNG | SMTO_BLOCK, Timeout, out result) != IntPtr.Zero;
    }
    public static long Send(IntPtr window, uint message, long wparam, long lparam) {
        IntPtr result;
        if (SendMessageTimeout(window, message, new IntPtr(wparam), new IntPtr(lparam), SMTO_ABORTIFHUNG | SMTO_BLOCK, Timeout, out result) == IntPtr.Zero)
            throw new TimeoutException("message 0x" + message.ToString("X") + " timed out");
        return result.ToInt64();
    }
    public static bool Post(IntPtr window, uint message, long wparam, long lparam) {
        return PostMessage(window, message, new IntPtr(wparam), new IntPtr(lparam));
    }
    public static bool Close(IntPtr window) {
        IntPtr result;
        return SendMessageTimeout(window, 0x0010, IntPtr.Zero, IntPtr.Zero, SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000, out result) != IntPtr.Zero;
    }

    public static int[] ClientSize(IntPtr window) {
        PerMonitor(); RECT rect; GetClientRect(window, out rect);
        return new[] { rect.Right - rect.Left, rect.Bottom - rect.Top };
    }
    public static int Scale(IntPtr window, int value) {
        uint dpi = GetDpiForWindow(window); if (dpi == 0) dpi = 96; return (int)(value * dpi / 96);
    }
    public static long ScreenLParam(IntPtr window, int x, int y) {
        PerMonitor(); var point = new POINT { X = x, Y = y }; ClientToScreen(window, ref point);
        return ((long)(ushort)point.Y << 16) | (ushort)point.X;
    }
    public static long ClientLParam(int x, int y) { return ((long)(ushort)y << 16) | (ushort)x; }

    // ---- remote memory: LVM_/HDM_/WM_NOTIFY carry pointers that user32 does
    // not marshal, so the structure must live in the app's address space. ----
    static byte[] RemoteCall(IntPtr window, uint message, long wparam, byte[] block, int pointerFixups, out long result) {
        IntPtr process = OpenProcess(0x0008 | 0x0010 | 0x0020 | 0x0400, false, (uint)ProcessId);
        if (process == IntPtr.Zero) throw new InvalidOperationException("OpenProcess failed");
        IntPtr remote = VirtualAllocEx(process, IntPtr.Zero, new UIntPtr((uint)block.Length), 0x3000, 0x04);
        try {
            if (remote == IntPtr.Zero) throw new InvalidOperationException("VirtualAllocEx failed");
            // Pointer slots were written as offsets from the block start; rebase them.
            for (int i = 0; i < pointerFixups; ++i) {
                int slot = BitConverter.ToInt32(block, block.Length - 4 * (i + 1));
                long offset = BitConverter.ToInt64(block, slot);
                BitConverter.GetBytes(remote.ToInt64() + offset).CopyTo(block, slot);
            }
            UIntPtr io;
            WriteProcessMemory(process, remote, block, new UIntPtr((uint)block.Length), out io);
            result = Send(window, message, wparam, remote.ToInt64());
            var copy = new byte[block.Length];
            ReadProcessMemory(process, remote, copy, new UIntPtr((uint)copy.Length), out io);
            return copy;
        } finally {
            if (remote != IntPtr.Zero) VirtualFreeEx(process, remote, UIntPtr.Zero, 0x8000);
            CloseHandle(process);
        }
    }
    static byte[] Block(int size, params int[] pointerSlots) {
        var block = new byte[size + 4 * pointerSlots.Length];
        for (int i = 0; i < pointerSlots.Length; ++i)
            BitConverter.GetBytes(pointerSlots[i]).CopyTo(block, block.Length - 4 * (i + 1));
        return block;
    }

    static IntPtr[] TopLevels(string className) {
        var list = new List<IntPtr>();
        EnumWindows((window, unused) => {
            if (Owner(window) == (uint)ProcessId && ClassOf(window) == className) list.Add(window);
            return true;
        }, IntPtr.Zero);
        return list.ToArray();
    }
    // The client rectangle of the tooltip tool the app registered for `owner`
    // with an id in [firstId, lastId), via TTM_GETTOOLCOUNT + TTM_ENUMTOOLSW on
    // a TOOLINFOW (72 bytes on x64) whose text pointer gets a remote buffer.
    // pane_tab_strip.cpp registers the "New tab" button as id 1000 + pane, with
    // the same rectangle its hit test uses.
    public static int[] ToolRect(IntPtr owner, long firstId, long lastId) {
        foreach (IntPtr tooltip in TopLevels("tooltips_class32")) {
            int count = (int)Send(tooltip, 0x040D, 0, 0);
            for (int index = 0; index < count; ++index) {
                const int textOffset = 96, chars = 512;
                var block = Block(textOffset + chars * 2, 48);
                BitConverter.GetBytes(72).CopyTo(block, 0);
                BitConverter.GetBytes((long)textOffset).CopyTo(block, 48);
                long ok;
                var copy = RemoteCall(tooltip, 0x043A, index, block, 1, out ok);
                if (ok == 0) continue;
                long hwnd = BitConverter.ToInt64(copy, 8), id = BitConverter.ToInt64(copy, 16);
                if (hwnd != owner.ToInt64() || id < firstId || id >= lastId) continue;
                return new[] { BitConverter.ToInt32(copy, 24), BitConverter.ToInt32(copy, 28),
                               BitConverter.ToInt32(copy, 32), BitConverter.ToInt32(copy, 36) };
            }
        }
        return null;
    }

    public static string ListBoxText(IntPtr list, int index) {
        IntPtr length;
        if (SendMessageTimeout(list, 0x018A, new IntPtr(index), IntPtr.Zero, SMTO_ABORTIFHUNG | SMTO_BLOCK, Timeout, out length) == IntPtr.Zero ||
            length.ToInt64() < 0) return null;
        var text = new StringBuilder(length.ToInt32() + 2);
        IntPtr copied;
        if (GetTextTimeout(list, 0x0189, new IntPtr(index), text, SMTO_ABORTIFHUNG | SMTO_BLOCK, Timeout, out copied) == IntPtr.Zero)
            return null;
        return text.ToString();
    }

    [DllImport("user32.dll", EntryPoint = "GetWindowLongW")] static extern int GetWindowLong(IntPtr window, int index);
    public static IntPtr OwnerDrawStatic(IntPtr parent) {
        foreach (IntPtr child in Descendants(parent, "Static", false))
            if (GetParent(child) == parent && (GetWindowLong(child, -16) & 0x1F) == 0x0D) return child;
        return IntPtr.Zero;
    }
    // A row of an LBS_OWNERDRAWFIXED list box, from pointer-free messages:
    // every row has LB_GETITEMHEIGHT, offset from LB_GETTOPINDEX.
    public static int[] ListBoxRowRect(IntPtr list, int index) {
        int height = (int)Send(list, 0x01A1, 0, 0);
        int top = (int)Send(list, 0x018E, 0, 0);
        int width = ClientSize(list)[0];
        int y = (index - top) * height;
        return new[] { 0, y, width, y + height };
    }

    public static int ItemCount(IntPtr list) { return (int)Send(list, 0x1004, 0, 0); }

    // LVM_GETITEMTEXTW with an LVITEMW (88 bytes on x64) followed by its buffer.
    public static string ItemText(IntPtr list, int item, int subItem) {
        const int textOffset = 128, chars = 520;
        var block = Block(textOffset + chars * 2, 24);
        BitConverter.GetBytes(subItem).CopyTo(block, 8);
        BitConverter.GetBytes((long)textOffset).CopyTo(block, 24);
        BitConverter.GetBytes(chars).CopyTo(block, 32);
        long length;
        var copy = RemoteCall(list, 0x1073, item, block, 1, out length);
        return Encoding.Unicode.GetString(copy, textOffset, (int)Math.Max(0, Math.Min(length, chars)) * 2);
    }
    public static string[] ColumnTexts(IntPtr list, int subItem) {
        int count = ItemCount(list);
        var texts = new string[count];
        for (int i = 0; i < count; ++i) texts[i] = ItemText(list, i, subItem);
        return texts;
    }
    // LVM_SETITEMSTATE: index -1 applies to every row.
    public static void SetItemState(IntPtr list, int item, int state, int mask) {
        var block = Block(88);
        BitConverter.GetBytes(state).CopyTo(block, 12);
        BitConverter.GetBytes(mask).CopyTo(block, 16);
        long result;
        RemoteCall(list, 0x102B, item, block, 0, out result);
    }
    public static void SelectOnly(IntPtr list, int item) {
        SetItemState(list, -1, 0, 0x3);          // clear LVIS_SELECTED | LVIS_FOCUSED
        SetItemState(list, item, 0x3, 0x3);
        Send(list, 0x1013, item, 0);             // LVM_ENSUREVISIBLE
    }
    // Header position whose caption is `caption`, via HDM_GETITEMW (HDITEMW is 72 bytes).
    public static int HeaderIndex(IntPtr list, string caption) {
        IntPtr header = new IntPtr(Send(list, 0x101F, 0, 0));
        int count = (int)Send(header, 0x1200, 0, 0);
        for (int index = 0; index < count; ++index) {
            const int textOffset = 96, chars = 128;
            var block = Block(textOffset + chars * 2, 8);
            BitConverter.GetBytes(0x2).CopyTo(block, 0);               // HDI_TEXT
            BitConverter.GetBytes((long)textOffset).CopyTo(block, 8);
            BitConverter.GetBytes(chars).CopyTo(block, 24);
            long ok;
            var copy = RemoteCall(header, 0x120B, index, block, 1, out ok);
            string text = Encoding.Unicode.GetString(copy, textOffset, chars * 2);
            int end = text.IndexOf('\0'); if (end >= 0) text = text.Substring(0, end);
            if (ok != 0 && text == caption) return index;
        }
        return -1;
    }
    // A press and release on a header item: comctl32 turns it into
    // HDN_ITEMCLICK and then the list's LVN_COLUMNCLICK, exactly as a real
    // click does. HDM_GETITEMRECT supplies the item's header-client rectangle.
    // A narrow pane can leave the item beyond the visible header; the list is
    // scrolled horizontally (LVM_SCROLL) to bring it into view first, then
    // scrolled back.
    public static void ColumnClick(IntPtr list, int headerIndex) {
        IntPtr header = new IntPtr(Send(list, 0x101F, 0, 0));
        int[] item = RemoteRect(header, 0x1207, headerIndex);
        int visible = ClientSize(list)[0];
        int scrolled = 0;
        int x = item[0] + Math.Min((item[2] - item[0]) / 2, 24);
        if (x >= visible - 8) {
            scrolled = item[0] - 16;
            Send(list, 0x1014, scrolled, 0);          // LVM_SCROLL
        }
        long point = ClientLParam(x, (item[1] + item[3]) / 2);
        Send(header, 0x0201, 1, point);   // WM_LBUTTONDOWN, MK_LBUTTON
        Send(header, 0x0202, 0, point);   // WM_LBUTTONUP
        if (scrolled != 0) Send(list, 0x1014, -scrolled, 0);
    }
    // A RECT-returning message (HDM_GETITEMRECT, LB_GETITEMRECT) into a
    // remote RECT. Throws when the control reports failure.
    public static int[] RemoteRect(IntPtr window, uint message, long wparam) {
        var block = Block(16);
        long ok;
        var copy = RemoteCall(window, message, wparam, block, 0, out ok);
        if (ok == 0 || ok == -1) throw new InvalidOperationException("rect query 0x" + message.ToString("X") + " failed");
        return new[] { BitConverter.ToInt32(copy, 0), BitConverter.ToInt32(copy, 4),
                       BitConverter.ToInt32(copy, 8), BitConverter.ToInt32(copy, 12) };
    }
}
'@

# ---------------------------------------------------------------------------
# Result bookkeeping
# ---------------------------------------------------------------------------
$script:results = New-Object System.Collections.Generic.List[object]
function Report([string] $Step, [string] $Status, [string] $Reason) {
    $script:results.Add([pscustomobject]@{ Step = $Step; Status = $Status; Reason = $Reason })
    $line = "{0}: {1}" -f $Status, $Step
    if ($Reason) { $line += " -- $Reason" }
    Write-Host $line
}
function Invoke-Step([string] $Step, [scriptblock] $Body) {
    if (-not (Test-AppAlive)) { Report $Step 'SKIP' 'app is not running'; return }
    try {
        $reason = & $Body
        Report $Step 'PASS' ([string] $reason)
    } catch {
        $message = $_.Exception.Message
        if ($message.StartsWith('SKIP: ')) { Report $Step 'SKIP' $message.Substring(6) }
        else { Report $Step 'FAIL' $message }
    }
}
# File operations (rename, paste, delete) run only while PaneDock is the
# foreground window, a deliberate app guard. The script never takes focus from
# the user, so such steps are skipped unless the app happens to be foreground.
Add-Type -Namespace PaneDockFlowsForeground -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
'@
function Assert-AppForeground {
    if ([PaneDockFlowsForeground.Native]::GetForegroundWindow() -ne $script:main) {
        throw 'SKIP: PaneDock is not the foreground window; file operations require it (the script never steals focus)'
    }
}
# $script:lastSeen is what the condition last observed; it is reported on timeout.
function Wait-Until([scriptblock] $Condition, [string] $What, [int] $TimeoutSeconds = $StepTimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $script:lastSeen = ''
    $lastError = ''
    while ($true) {
        if ([PaneDockFlows]::HasExited()) { throw "app exited while waiting for $What" }
        try { if (& $Condition) { return } } catch { $lastError = $_.Exception.Message }
        if ([DateTime]::UtcNow -ge $deadline) {
            $detail = @($script:lastSeen, $lastError | Where-Object { $_ }) -join '; '
            throw "timed out after ${TimeoutSeconds}s waiting for $What$(if ($detail) { " (last: $detail)" })"
        }
        Start-Sleep -Milliseconds 100
    }
}
function Test-AppAlive { return ([PaneDockFlows]::ProcessHandle -ne [IntPtr]::Zero) -and -not [PaneDockFlows]::HasExited() }

# ---------------------------------------------------------------------------
# App model helpers (control ids from src/app_shell/pane_control_id.h)
# ---------------------------------------------------------------------------
$WM_COMMAND = 0x0111; $WM_KEYDOWN = 0x0100; $WM_CONTEXTMENU = 0x007B
$WM_LBUTTONDOWN = 0x0201; $WM_LBUTTONUP = 0x0202; $LB_GETCOUNT = 0x018B
$LB_SETCURSEL = 0x0186; $LB_GETCURSEL = 0x0188; $LB_GETTEXT = 0x0189
$VK_RETURN = 0x0D; $VK_F2 = 0x71; $VK_DOWN = 0x28; $VK_ESCAPE = 0x1B
$IdTabStrip = 200; $IdBack = 300; $IdUp = 320; $IdAddress = 330; $IdRefresh = 340
$IdGroupList = 100; $IdNewGroup = 101; $IdRenameGroup = 103; $IdDeleteGroup = 104
$IdLayoutBase = 400   # index into kLayoutTemplates: 0 single, 1 left_right, 7 four_pane_grid
$LBN_SELCHANGE = 1

function Get-Pane([int] $Index = 0) {
    $panes = [PaneDockFlows]::VisiblePanes($script:main)
    if ($panes.Count -le $Index) { throw "pane $Index is not visible ($($panes.Count) visible)" }
    return Get-PaneByWindow $panes[$Index]
}
function Get-PaneByIndex([int] $Index) { return Get-Pane $Index }
function Get-PaneByWindow([IntPtr] $pane) {
    $lists = [PaneDockFlows]::Descendants($pane, 'PaneDockManagedList', $true)
    [pscustomobject]@{
        Window  = $pane
        Address = [PaneDockFlows]::GetDlgItem($pane, $IdAddress)
        Strip   = [PaneDockFlows]::GetDlgItem($pane, $IdTabStrip)
        List    = if ($lists.Count -gt 0) { $lists[0] } else { [IntPtr]::Zero }
        # pane.cpp: the status bar is the pane's only SS_OWNERDRAW STATIC.
        Status  = [PaneDockFlows]::OwnerDrawStatic($pane)
    }
}
function Get-PaneStatus($Pane) {
    if (-not $Pane.Status -or $Pane.Status -eq [IntPtr]::Zero) { return '' }
    return [PaneDockFlows]::GetText($Pane.Status)
}
function Normalize-Path([string] $Path) { return $Path.TrimEnd('\').ToLowerInvariant() }
function Get-ListNames($Pane) { return @([PaneDockFlows]::ColumnTexts($Pane.List, 0)) }

# Settled: the address bar shows $Path and the list holds exactly $Expected rows
# with the status line reporting that count and no pending work.
function Wait-PaneShows([string] $Path, [string[]] $Expected, [int] $PaneIndex = 0) {
    Wait-Until {
        $pane = Get-Pane $PaneIndex
        if ($pane.List -eq [IntPtr]::Zero) { return $false }
        $address = [PaneDockFlows]::GetText($pane.Address)
        $status = Get-PaneStatus $pane
        $count = [PaneDockFlows]::ItemCount($pane.List)
        $script:lastSeen = "address='$address' status='$status' rows=$count"
        if ((Normalize-Path $address) -ne (Normalize-Path $Path)) { return $false }
        $settled = (Test-StatusSettled $status) -and
                   $status.StartsWith((Count-Text $Expected.Count))
        if (-not $settled -or $count -ne $Expected.Count) { return $false }
        $names = @(Get-ListNames $pane)
        $missing = @($Expected | Where-Object { $names -notcontains $_ })
        return $missing.Count -eq 0
    } "pane $PaneIndex to list $($Expected.Count) item(s) of $Path"
    return Count-Text $Expected.Count
}
# Explorer-style status: "1 item", "N items" (an empty folder reads "0 items"),
# optionally followed by selection or "Automatic refresh unavailable" text.
function Count-Text([int] $Count) {
    if ($Count -eq 1) { return '1 item' }
    return ('{0:N0} items' -f $Count)
}
function Test-StatusSettled([string] $Status) {
    $Status -match '^[\d,]+ items?\b' -and $Status -notmatch 'Loading|Updating|Recovering|Sorting'
}
function Expected-Names([string] $Folder) {
    $items = Get-ChildItem -LiteralPath $Folder -Force
    if (-not $script:showHidden) {
        $items = $items | Where-Object { -not ($_.Attributes -band [IO.FileAttributes]::Hidden) }
    }
    return @($items | ForEach-Object { $_.Name })
}
function Send-PaneButton($Pane, [int] $Id) {
    [void][PaneDockFlows]::Post($Pane.Window, $WM_COMMAND, $Id, 0)   # BN_CLICKED == 0
}
function Wait-PaneIdle([int] $PaneIndex = 0) {
    Wait-Until {
        $pane = Get-PaneByIndex $PaneIndex
        $status = Get-PaneStatus $pane
        $script:lastSeen = "status='$status'"
        Test-StatusSettled $status
    } "pane $PaneIndex to finish loading"
}
# A load that completes after WM_SETTEXT rewrites the address bar before the
# posted Enter is handled, so navigate from a settled pane and retry once.
function Navigate-Address($Pane, [string] $Path) {
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        if (-not [PaneDockFlows]::SetText($Pane.Address, $Path)) { throw 'WM_SETTEXT on the address bar failed' }
        [void][PaneDockFlows]::Post($Pane.Address, $WM_KEYDOWN, $VK_RETURN, 0)
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            Start-Sleep -Milliseconds 100
            $address = [PaneDockFlows]::GetText($Pane.Address)
            $status = Get-PaneStatus (Get-PaneByWindow $Pane.Window)
            if ((Normalize-Path $address) -eq (Normalize-Path $Path) -and $status -notmatch 'Loading') { return }
        } while ([DateTime]::UtcNow -lt $deadline)
    }
    throw "the address bar did not accept $Path"
}
function Select-Group([int] $Index) {
    $list = [PaneDockFlows]::GetDlgItem($script:main, $IdGroupList)
    [void][PaneDockFlows]::Send($list, $LB_SETCURSEL, $Index, 0)
    $wparam = ($LBN_SELCHANGE -shl 16) -bor $IdGroupList
    [void][PaneDockFlows]::Post($script:main, $WM_COMMAND, $wparam, $list.ToInt64())
}
# LB_GETTEXT is marshaled across processes for an LBS_HASSTRINGS list box.
function Get-GroupName([int] $Index) {
    $list = [PaneDockFlows]::GetDlgItem($script:main, $IdGroupList)
    return [PaneDockFlows]::ListBoxText($list, $Index)
}
function Get-GroupCount {
    $list = [PaneDockFlows]::GetDlgItem($script:main, $IdGroupList)
    return [PaneDockFlows]::Send($list, $LB_GETCOUNT, 0, 0)
}
function Get-SelectedGroup {
    $list = [PaneDockFlows]::GetDlgItem($script:main, $IdGroupList)
    return [PaneDockFlows]::Send($list, $LB_GETCURSEL, 0, 0)
}
# Press and release inside the tab strip at client (x, y). A press without
# movement activates the tab under it (or does nothing over empty strip); the
# release ends the drag bookkeeping and the capture the press took.
#
# Tab rectangles span the strip's full height, while each tab's close button is
# a square centred vertically near the tab's right edge (tab_overflow.h:
# tab_close_button_rect). Row 0 is therefore inside every tab and outside every
# close button, so scanning along it can activate tabs but never close one.
function Click-Strip($Pane, [int] $X, [int] $Y = 0) {
    $lparam = [PaneDockFlows]::ClientLParam($X, $Y)
    [void][PaneDockFlows]::Send($Pane.Strip, $WM_LBUTTONDOWN, 1, $lparam)
    [void][PaneDockFlows]::Send($Pane.Strip, $WM_LBUTTONUP, 0, $lparam)
}
# The "+" rectangle exactly as the app hit-tests it: the strip registers it as
# its "New tab" tooltip tool. Falls back to the documented right-edge layout.
function Get-AddRect($Pane) {
    $rect = [PaneDockFlows]::ToolRect($Pane.Strip, 1000, 1010)
    if ($null -ne $rect -and $rect[2] -gt $rect[0]) { return $rect }
    $size = [PaneDockFlows]::ClientSize($Pane.Strip)
    return @(($size[0] - [PaneDockFlows]::Scale($Pane.Strip, 36)), 0, $size[0], $size[1])
}
function Click-NewTab($Pane) {
    $add = Get-AddRect $Pane
    Click-Strip $Pane ([int](($add[0] + $add[2]) / 2)) ([int](($add[1] + $add[3]) / 2))
}
function Test-PaneAddress($Pane, [string] $Path) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds(400)
    do {
        if ((Normalize-Path ([PaneDockFlows]::GetText($Pane.Address))) -eq (Normalize-Path $Path)) { return $true }
        Start-Sleep -Milliseconds 40
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}
# Walk row 0 of the strip left to right until the active tab shows $Path.
# Returns the x that selected it.
function Activate-TabShowing($Pane, [string] $Path) {
    $limit = (Get-AddRect $Pane)[0]
    $step = [PaneDockFlows]::Scale($Pane.Strip, 8)
    for ($x = 2; $x -lt $limit; $x += $step) {
        Click-Strip $Pane $x
        if (Test-PaneAddress $Pane $Path) { return $x }
    }
    throw "no tab in the strip shows $Path"
}
# Close the tab showing $Path with its own close button. The button's position
# is derived from the tab's right edge, found by walking row 0 leftwards from
# the "+" button (empty strip ignores presses) and refining to one pixel.
function Close-TabShowing($Pane, [string] $Path, [string] $OtherPath) {
    $limit = (Get-AddRect $Pane)[0]
    $step = [PaneDockFlows]::Scale($Pane.Strip, 8)
    $hit = -1
    for ($x = $limit - 1; $x -ge 0; $x -= $step) {
        [void](Activate-TabShowing $Pane $OtherPath)
        Click-Strip $Pane $x
        if (Test-PaneAddress $Pane $Path) { $hit = $x; break }
    }
    if ($hit -lt 0) { throw "no tab in the strip shows $Path" }
    $right = $hit
    for ($x = [Math]::Min($limit - 1, $hit + $step - 1); $x -gt $hit; --$x) {
        [void](Activate-TabShowing $Pane $OtherPath)
        Click-Strip $Pane $x
        if (Test-PaneAddress $Pane $Path) { $right = $x; break }
    }
    # tab_close_button_rect: right = tab.right - 4, width 16 (96-DPI units),
    # vertically centred; tab.right is exclusive, so it is $right + 1.
    $height = ([PaneDockFlows]::ClientSize($Pane.Strip))[1]
    $centre = $right + 1 - [PaneDockFlows]::Scale($Pane.Strip, 4) - [PaneDockFlows]::Scale($Pane.Strip, 8)
    Click-Strip $Pane $centre ([int]($height / 2))
}
function Wait-AppWindow([string] $ClassName, [string] $Title, [int] $TimeoutSeconds = 10) {
    $script:found = [IntPtr]::Zero
    Wait-Until {
        $script:found = [PaneDockFlows]::FindTopLevel($ClassName, $Title)
        $script:found -ne [IntPtr]::Zero
    } "$ClassName '$Title'" $TimeoutSeconds
    return $script:found
}

# ---------------------------------------------------------------------------
# Fixture and isolated storage
# ---------------------------------------------------------------------------
$work = Join-Path ([IO.Path]::GetTempPath()) ("PaneDockUserFlows-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$fixture = Join-Path $work 'data'
$env:LOCALAPPDATA = Join-Path $work 'localappdata'
New-Item -ItemType Directory -Force -Path $env:LOCALAPPDATA | Out-Null

$alpha = Join-Path $fixture 'Alpha Folder'
$nested = Join-Path $alpha 'Nested Folder'
$beta = Join-Path $fixture 'Beta Folder'
New-Item -ItemType Directory -Force -Path $nested, $beta | Out-Null
$longName = 'Delta ' + ('long name segment ' * 8).Trim() + '.txt'
# Sizes, dates and names each produce a different order, so a sort that
# silently fell back to another key fails.
$files = @(
    @{ Name = 'apple.txt';   Bytes = 3000; Age = 2 },
    @{ Name = 'banana.docx'; Bytes = 10;   Age = 4 },
    @{ Name = 'cherry.txt';  Bytes = 500;  Age = 1 },
    @{ Name = $longName;     Bytes = 1200; Age = 3 }
)
$now = Get-Date
foreach ($file in $files) {
    $path = Join-Path $fixture $file.Name
    [IO.File]::WriteAllBytes($path, (New-Object byte[] $file.Bytes))
    (Get-Item -LiteralPath $path).LastWriteTime = $now.AddDays(-10 * $file.Age)
}
$hiddenFile = Join-Path $fixture 'hidden note.txt'
[IO.File]::WriteAllText($hiddenFile, 'hidden')
(Get-Item -LiteralPath $hiddenFile).Attributes = 'Hidden'
[IO.File]::WriteAllText((Join-Path $alpha 'inside.txt'), 'inside')
[IO.File]::WriteAllText((Join-Path $nested 'deep.txt'), 'deep')
[IO.File]::WriteAllText((Join-Path $beta 'beta.txt'), 'beta')

$advanced = Get-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' -ErrorAction SilentlyContinue
$script:showHidden = $advanced -and $advanced.Hidden -eq 1

$script:main = [IntPtr]::Zero
$failedToClose = $false

function Start-App([string] $Label) {
    $path = Join-Path $work "panedock-$Label.stdout.txt"
    $script:currentStdout = $path
    [void][PaneDockFlows]::Launch($resolvedAppPath, '--diagnostic', $path)
    $script:main = [IntPtr]::Zero
    Wait-Until {
        $script:main = [PaneDockFlows]::FindTopLevel('PaneDockMainWindow', $null)
        $script:main -ne [IntPtr]::Zero
    } 'the main window' 30
    Wait-Until { (Get-Pane 0).List -ne [IntPtr]::Zero } 'the first pane list' 30
    Wait-PaneIdle 0
}
function Stop-App {
    if (-not (Test-AppAlive)) { return $true }
    if ($script:main -eq [IntPtr]::Zero) { $script:main = [PaneDockFlows]::FindTopLevel('PaneDockMainWindow', $null) }
    if (-not [PaneDockFlows]::Close($script:main)) { return $false }
    if (-not [PaneDockFlows]::WaitExit(30000)) { return $false }
    return $true
}
function Assert-CleanExit([string] $Label) {
    $code = [PaneDockFlows]::ExitCode()
    if ($code -ne 0) { throw "exit code $code" }
    $counts = @(Get-Content -LiteralPath $script:currentStdout -ErrorAction SilentlyContinue |
        Where-Object { $_ -match '^panedock\.live_list_count=(\d+)$' } |
        ForEach-Object { [int] ($_ -replace '^.*=', '') })
    if ($counts.Count -eq 0) { throw 'no live_list_count teardown sample' }
    if ($counts[-1] -ne 0) { throw "final live_list_count=$($counts[-1])" }
    return 'exit 0, live_list_count=0'
}
function Read-Session {
    $path = Join-Path $env:LOCALAPPDATA 'PaneDock\session.json'
    return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
}

$rootNames = Expected-Names $fixture
$alphaNames = Expected-Names $alpha
$nestedNames = Expected-Names $nested
$betaNames = Expected-Names $beta

try {
    # =======================================================================
    # Phase 1: first launch
    # =======================================================================
    try {
        Start-App 'first'
        Report 'launch with isolated LOCALAPPDATA' 'PASS' "PID $([PaneDockFlows]::ProcessId), hidden files shown: $script:showHidden"
    } catch {
        Report 'launch with isolated LOCALAPPDATA' 'FAIL' $_.Exception.Message
        throw
    }

    Invoke-Step 'address bar navigates to the fixture; item count matches' {
        Navigate-Address (Get-Pane 0) $fixture
        Wait-PaneShows $fixture $rootNames
    }

    Invoke-Step 'Enter opens a subfolder' {
        $pane = Get-Pane 0
        $index = [array]::IndexOf(@(Get-ListNames $pane), 'Alpha Folder')
        if ($index -lt 0) { throw "'Alpha Folder' is not listed" }
        [PaneDockFlows]::SelectOnly($pane.List, $index)
        [void][PaneDockFlows]::Post($pane.List, $WM_KEYDOWN, $VK_RETURN, 0)
        Wait-PaneShows $alpha $alphaNames
    }

    Invoke-Step 'Enter opens a nested folder' {
        $pane = Get-Pane 0
        $index = [array]::IndexOf(@(Get-ListNames $pane), 'Nested Folder')
        if ($index -lt 0) { throw "'Nested Folder' is not listed" }
        [PaneDockFlows]::SelectOnly($pane.List, $index)
        [void][PaneDockFlows]::Post($pane.List, $WM_KEYDOWN, $VK_RETURN, 0)
        Wait-PaneShows $nested $nestedNames
    }

    Invoke-Step 'Back button returns to the previous folder' {
        Send-PaneButton (Get-Pane 0) $IdBack
        Wait-PaneShows $alpha $alphaNames
    }

    Invoke-Step 'Up button goes to the parent folder' {
        Send-PaneButton (Get-Pane 0) $IdUp
        Wait-PaneShows $fixture $rootNames
    }

    $externalName = 'external new file.txt'
    $externalPath = Join-Path $fixture $externalName
    Invoke-Step 'external create appears without Refresh (auto-refresh)' {
        [IO.File]::WriteAllText($externalPath, 'created outside PaneDock')
        Wait-PaneShows $fixture (Expected-Names $fixture)
    }

    Invoke-Step 'Refresh button keeps the listing consistent' {
        Send-PaneButton (Get-Pane 0) $IdRefresh
        Wait-PaneShows $fixture (Expected-Names $fixture)
    }

    Invoke-Step 'external delete disappears without Refresh (auto-refresh)' {
        Remove-Item -LiteralPath $externalPath -Force
        Wait-PaneShows $fixture $rootNames
    }

    # Sorting: compare the file rows only, so the check does not depend on
    # where folders are placed.
    $fileInfo = @{}
    foreach ($name in $rootNames) {
        $item = Get-Item -LiteralPath (Join-Path $fixture $name) -Force
        if (-not $item.PSIsContainer) { $fileInfo[$name] = $item }
    }
    function Get-FileOrder {
        return @(Get-ListNames (Get-Pane 0) | Where-Object { $fileInfo.ContainsKey($_) })
    }
    function Click-Column([string] $Caption) {
        $pane = Get-Pane 0
        $index = [PaneDockFlows]::HeaderIndex($pane.List, $Caption)
        if ($index -lt 0) { throw "no '$Caption' column" }
        [PaneDockFlows]::ColumnClick($pane.List, $index)
    }
    function Assert-Order([string[]] $Expected, [string] $What) {
        $script:lastOrder = @()
        try {
            Wait-Until {
                $script:lastOrder = Get-FileOrder
                ($script:lastOrder -join '|') -eq ($Expected -join '|')
            } $What 5
        } catch {
            throw "$What expected [$($Expected -join ', ')] got [$($script:lastOrder -join ', ')]"
        }
    }
    $byName = [string[]] @($fileInfo.Keys)
    [Array]::Sort($byName, [Comparison[string]] { param($a, $b) [PaneDockFlows]::StrCmpLogicalW($a, $b) })
    $bySize = @($fileInfo.Values | Sort-Object Length | ForEach-Object Name)
    $byDate = @($fileInfo.Values | Sort-Object LastWriteTimeUtc | ForEach-Object Name)

    Invoke-Step 'default order is by name (logical, ascending)' {
        Assert-Order $byName 'name order'
    }
    # Explorer semantics: a first click on Size or Date modified sorts
    # descending, on Name or Type ascending; a second click on the same
    # column toggles.
    $sizeDescending = @($bySize); [Array]::Reverse($sizeDescending)
    $dateDescending = @($byDate); [Array]::Reverse($dateDescending)
    Invoke-Step 'Size column sorts descending, then ascending' {
        Click-Column 'Size'
        Assert-Order $sizeDescending 'size descending (first click)'
        Click-Column 'Size'
        Assert-Order $bySize 'size ascending (second click)'
    }
    Invoke-Step 'Date modified column sorts descending, then ascending' {
        Click-Column 'Date modified'
        Assert-Order $dateDescending 'date descending (first click)'
        Click-Column 'Date modified'
        Assert-Order $byDate 'date ascending (second click)'
    }
    Invoke-Step 'Name column sorts ascending again' {
        Click-Column 'Name'
        Assert-Order $byName 'name ascending'
    }

    $renameFrom = 'cherry.txt'
    $renameTo = 'cherry renamed.txt'
    Invoke-Step 'F2 inline rename renames the file on disk' {
        $pane = Get-Pane 0
        $index = [array]::IndexOf(@(Get-ListNames $pane), $renameFrom)
        if ($index -lt 0) { throw "'$renameFrom' is not listed" }
        Assert-AppForeground
        # A late property or watch batch cancels an open edit (by design), so
        # start from a settled list and retry if the editor vanishes unused.
        $target = Join-Path $fixture $renameTo
        $attempts = 0
        while ($attempts -lt 3 -and -not (Test-Path -LiteralPath $target)) {
            ++$attempts
            Wait-PaneShows $fixture $rootNames | Out-Null
            $index = [array]::IndexOf(@(Get-ListNames $pane), $renameFrom)
            [PaneDockFlows]::SelectOnly($pane.List, $index)
            [void][PaneDockFlows]::Post($pane.List, $WM_KEYDOWN, $VK_F2, 0)
            $script:editor = [IntPtr]::Zero
            Wait-Until {
                $script:editor = @([PaneDockFlows]::Descendants($pane.List, 'Edit', $true)) | Select-Object -First 1
                $null -ne $script:editor -and $script:editor -ne [IntPtr]::Zero
            } 'the inline rename editor' 5
            if (-not [PaneDockFlows]::SetText($script:editor, $renameTo)) { continue }
            if (-not [PaneDockFlows]::Post($script:editor, $WM_KEYDOWN, $VK_RETURN, 0)) { continue }
            try { Wait-Until { Test-Path -LiteralPath $target } 'the renamed file on disk' 8 } catch { }
        }
        if (-not (Test-Path -LiteralPath $target)) {
            throw "not renamed after $attempts attempt(s) (status: '$(Get-PaneStatus (Get-Pane 0))')"
        }
        if (Test-Path -LiteralPath (Join-Path $fixture $renameFrom)) { throw 'the old name still exists' }
        $script:rootNames = Expected-Names $fixture
        $shown = Wait-PaneShows $fixture $script:rootNames
        "$shown, renamed on attempt $attempts"
    }
    $rootNames = Expected-Names $fixture

    # Tabs: tab 1 -> Beta Folder, tab 2 opened and closed again.
    Invoke-Step 'new tab opens and can be navigated' {
        $pane = Get-Pane 0
        $before = [PaneDockFlows]::GetText($pane.Address)
        Click-NewTab $pane
        Wait-Until { (Normalize-Path ([PaneDockFlows]::GetText($pane.Address))) -ne (Normalize-Path $before) } 'the new tab to become active'
        Navigate-Address $pane $beta
        Wait-PaneShows $beta $betaNames
    }
    Invoke-Step 'switching tabs restores each tab''s folder' {
        $pane = Get-Pane 0
        [void](Activate-TabShowing $pane $fixture)
        Wait-PaneShows $fixture $rootNames | Out-Null
        [void](Activate-TabShowing $pane $beta)
        Wait-PaneShows $beta $betaNames | Out-Null
        [void](Activate-TabShowing $pane $fixture)
        Wait-PaneShows $fixture $rootNames
    }
    Invoke-Step 'close tab with its close button' {
        $pane = Get-Pane 0
        Click-NewTab $pane
        Wait-Until { (Normalize-Path ([PaneDockFlows]::GetText($pane.Address))) -ne (Normalize-Path $fixture) } 'a third tab'
        Navigate-Address $pane $nested
        Wait-PaneShows $nested $nestedNames | Out-Null
        Close-TabShowing $pane $nested $fixture
        Wait-Until { (Normalize-Path ([PaneDockFlows]::GetText($pane.Address))) -ne (Normalize-Path $nested) } 'the closed tab to leave the pane' 5
        $stillThere = $true
        try { [void](Activate-TabShowing $pane $nested) } catch { $stillThere = $false }
        if ($stillThere) { throw 'the closed tab is still in the strip' }
        [void](Activate-TabShowing $pane $beta)
        [void](Activate-TabShowing $pane $fixture)
        Wait-PaneShows $fixture $rootNames
    }

    # Groups: Group 1 (pane 0 -> fixture), "Smoke B" (pane 0 -> Alpha), Group 3.
    Invoke-Step 'create a Group and rename it' {
        [void][PaneDockFlows]::Post($script:main, $WM_COMMAND, $IdNewGroup, 0)
        Wait-Until { (Get-GroupCount) -eq 2 -and (Get-SelectedGroup) -eq 1 } 'a second, active Group'
        [void][PaneDockFlows]::Post($script:main, $WM_COMMAND, $IdRenameGroup, 0)
        $groupList = [PaneDockFlows]::GetDlgItem($script:main, $IdGroupList)
        $script:groupEditor = [IntPtr]::Zero
        Wait-Until {
            $script:groupEditor = @([PaneDockFlows]::Descendants($groupList, 'Edit', $true)) | Select-Object -First 1
            $null -ne $script:groupEditor -and $script:groupEditor -ne [IntPtr]::Zero
        } 'the Group rename editor' 5
        [void][PaneDockFlows]::SetText($script:groupEditor, 'Smoke B')
        [void][PaneDockFlows]::Post($script:groupEditor, $WM_KEYDOWN, $VK_RETURN, 0)
        Wait-Until { -not [PaneDockFlows]::IsWindow($script:groupEditor) } 'the Group rename to commit' 5
        Navigate-Address (Get-Pane 0) $alpha
        Wait-PaneShows $alpha $alphaNames
    }
    Invoke-Step 'switching Groups restores each Group''s panes' {
        [void][PaneDockFlows]::Post($script:main, $WM_COMMAND, $IdNewGroup, 0)
        Wait-Until { (Get-GroupCount) -eq 3 -and (Get-SelectedGroup) -eq 2 } 'a third, active Group'
        Select-Group 0
        Wait-PaneShows $fixture $rootNames | Out-Null
        Select-Group 1
        Wait-PaneShows $alpha $alphaNames | Out-Null
        Select-Group 0
        Wait-PaneShows $fixture $rootNames
    }
    Invoke-Step 'deleting a non-active Group keeps the visible pane listing' {
        Wait-PaneShows $fixture $rootNames | Out-Null
        $groupList = [PaneDockFlows]::GetDlgItem($script:main, $IdGroupList)
        # The sidebar buttons act on the active Group; only the row context
        # menu targets another one. Open it at the third row's centre and pick
        # its third item, "Delete Group".
        $row = [PaneDockFlows]::ListBoxRowRect($groupList, 2)
        $screen = [PaneDockFlows]::ScreenLParam($groupList, [int](($row[0] + $row[2]) / 2), [int](($row[1] + $row[3]) / 2))
        [void][PaneDockFlows]::Post($script:main, $WM_CONTEXTMENU, $groupList.ToInt64(), $screen)
        $menu = Wait-AppWindow '#32768' $null 5
        foreach ($key in @($VK_DOWN, $VK_DOWN, $VK_DOWN, $VK_RETURN)) {
            [void][PaneDockFlows]::Post($menu, $WM_KEYDOWN, $key, 0)
        }
        try {
            $box = Wait-AppWindow '#32770' 'Delete Group' 10
        } catch {
            [void][PaneDockFlows]::Post($menu, $WM_KEYDOWN, $VK_ESCAPE, 0)
            throw
        }
        [void][PaneDockFlows]::Post($box, $WM_COMMAND, 6, 0)   # IDYES
        Wait-Until { (Get-GroupCount) -eq 2 } 'two Groups to remain'
        if ((Get-SelectedGroup) -ne 0) { throw "Group $(Get-SelectedGroup) is active after the delete" }
        $groupNames = @(Get-GroupName 0; Get-GroupName 1)
        if ($groupNames -notcontains 'Group 1' -or $groupNames -notcontains 'Smoke B') {
            throw "remaining Groups: $($groupNames -join ', ')"
        }
        Wait-PaneShows $fixture $rootNames
    }

    Invoke-Step 'layout change to two panes and back to four' {
        [void][PaneDockFlows]::Post($script:main, $WM_COMMAND, $IdLayoutBase + 1, 0)
        Wait-Until { ([PaneDockFlows]::VisiblePanes($script:main)).Count -eq 2 } 'two visible panes'
        Wait-PaneShows $fixture $rootNames | Out-Null
        [void][PaneDockFlows]::Post($script:main, $WM_COMMAND, $IdLayoutBase + 7, 0)
        Wait-Until { ([PaneDockFlows]::VisiblePanes($script:main)).Count -eq 4 } 'four visible panes'
        Wait-PaneShows $fixture $rootNames
    }

    Invoke-Step 'normal close exits cleanly' {
        if (-not (Stop-App)) { $script:failedToClose = $true; throw 'the app did not exit after WM_CLOSE' }
        Assert-CleanExit 'first'
    }
    if ($script:failedToClose) { throw 'stopping: first instance did not close' }
    [PaneDockFlows]::ReleaseProcess()

    $session = $null
    try {
        $session = Read-Session
        $groups = @($session.groups)
        $first = $groups | Where-Object { $_.name -eq 'Group 1' }
        $second = $groups | Where-Object { $_.name -eq 'Smoke B' }
        if ($groups.Count -ne 2 -or -not $first -or -not $second) {
            throw "groups saved: $(@($groups | ForEach-Object name) -join ', ')"
        }
        if ($session.active_group_id -ne $first.id) { throw "active group is $($session.active_group_id)" }
        $pane0 = @($first.panes)[0]
        $tabs = @($pane0.tabs | ForEach-Object { $_.shell_location.parsing_name })
        if ($tabs.Count -ne 2) { throw "pane 0 saved $($tabs.Count) tabs: $($tabs -join ', ')" }
        if (-not ($tabs | Where-Object { (Normalize-Path $_) -eq (Normalize-Path $beta) })) { throw 'Beta Folder tab not saved' }
        $active = @($pane0.tabs | Where-Object { $_.id -eq $pane0.active_tab_id })[0]
        if ((Normalize-Path $active.shell_location.parsing_name) -ne (Normalize-Path $fixture)) {
            throw "active tab saved as $($active.shell_location.parsing_name)"
        }
        if ($first.layout_template -ne 'four_pane_grid') { throw "layout saved as $($first.layout_template)" }
        if (-not $session.clean_shutdown) { throw 'clean_shutdown is false' }
        Report 'session file records Groups, tabs, layout and locations' 'PASS' '2 Groups, pane 0 has 2 tabs'
    } catch {
        Report 'session file records Groups, tabs, layout and locations' 'FAIL' $_.Exception.Message
    }

    # =======================================================================
    # Phase 2: relaunch and verify the restore through the UI
    # =======================================================================
    try {
        Start-App 'second'
        Report 'relaunch' 'PASS' "PID $([PaneDockFlows]::ProcessId)"
    } catch {
        Report 'relaunch' 'FAIL' $_.Exception.Message
        throw
    }
    Invoke-Step 'restored Groups and active pane location' {
        if ((Get-GroupCount) -ne 2) { throw "$(Get-GroupCount) Groups restored" }
        if ((Get-SelectedGroup) -ne 0) { throw "Group $(Get-SelectedGroup) is active" }
        if (([PaneDockFlows]::VisiblePanes($script:main)).Count -ne 4) { throw 'layout is not four panes' }
        Wait-PaneShows $fixture $rootNames
    }
    Invoke-Step 'restored second tab' {
        $pane = Get-Pane 0
        [void](Activate-TabShowing $pane $beta)
        Wait-PaneShows $beta $betaNames | Out-Null
        [void](Activate-TabShowing $pane $fixture)
        Wait-PaneShows $fixture $rootNames
    }
    Invoke-Step 'restored second Group' {
        Select-Group 1
        Wait-PaneShows $alpha $alphaNames | Out-Null
        Select-Group 0
        Wait-PaneShows $fixture $rootNames
    }
    Invoke-Step 'second normal close exits cleanly' {
        if (-not (Stop-App)) { $script:failedToClose = $true; throw 'the app did not exit after WM_CLOSE' }
        Assert-CleanExit 'second'
    }
} catch {
    Write-Host "ABORT: $($_.Exception.Message)"
    $script:results.Add([pscustomobject]@{ Step = 'run'; Status = 'FAIL'; Reason = $_.Exception.Message })
} finally {
    if (Test-AppAlive) {
        if (-not (Stop-App)) {
            $failedToClose = $true
            Write-Warning "$appName (PID $([PaneDockFlows]::ProcessId)) did not exit after WM_CLOSE. Close it normally; it was not force-killed."
        }
    }
    if ($script:failedToClose) { $failedToClose = $true }
    [PaneDockFlows]::ReleaseProcess()
    if ($failedToClose -or $KeepFixtures) {
        Write-Host "Fixtures kept at $work"
    } else {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$failed = @($script:results | Where-Object Status -eq 'FAIL')
$passed = @($script:results | Where-Object Status -eq 'PASS')
$skipped = @($script:results | Where-Object Status -eq 'SKIP')
Write-Host ("Summary: {0} passed, {1} failed, {2} skipped" -f $passed.Count, $failed.Count, $skipped.Count)
if ($failed.Count -ne 0) { exit 1 }
exit 0
