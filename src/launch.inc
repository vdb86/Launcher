// =============================================================================
// Phase 3: launching apps + tracking the running process.
// =============================================================================
static HANDLE gJob         = nullptr;   // job object: launched app + its descendants
// How we track a launched app so we know when to bring the launcher back to the front.
enum TrackMode { TRACK_NONE, TRACK_PROC, TRACK_WINDOW };
static TrackMode gTrackMode    = TRACK_NONE;
static HANDLE    gRunningProc  = nullptr;   // TRACK_PROC: handle of the process we wait on
// TRACK_WINDOW (single-instance / tray apps): the launched exe hands off to an already-running instance
// and exits at once, so process-exit is useless. Instead we find the app's top-level window and return to
// the launcher when it closes OR hides to the tray/taskbar.
static std::wstring gWatchProc;             // image name to match, e.g. "CouchTube.exe"
static HWND         gWatchWnd      = nullptr;  // the app window, once found
static ULONGLONG    gWatchDeadline = 0;        // GetTickCount64 by which the window must appear (else give up)

// Inhibit (or release) the OS screensaver + display/system sleep. ES_CONTINUOUS makes the request
// persist until changed; ES_DISPLAY_REQUIRED also blocks the OS screensaver. We assert this while
// the launcher is the foreground app and release it (bare ES_CONTINUOUS) while a launched app runs
// or on exit, so the running app / OS can manage power itself. Honours the gInhibitSleep setting.
static void KeepAwake(bool on)
{
    SetThreadExecutionState(on && gInhibitSleep
        ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED)
        : ES_CONTINUOUS);
}

static void CreateJob()
{
    gJob = CreateJobObjectW(nullptr, nullptr);
    // No limits set: the job is only a tracking/grouping handle for now (so a spawn-and-exit
    // launcher's children are grouped under it). We deliberately do NOT set KILL_ON_JOB_CLOSE -
    // closing the launcher should not kill apps the user started. Kiosk mode (Phase 6) may opt in.
}

static std::wstring DirOf(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? std::wstring() : path.substr(0, p);
}

// Reliable foreground grab (SetForegroundWindow alone often fails when another app is active).
// Standard AttachThreadInput dance + restore.
static void ForceForeground(HWND h)
{
    if (!h) return;
    HWND  fg    = GetForegroundWindow();
    DWORD myTid = GetCurrentThreadId();
    DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    if (fgTid && fgTid != myTid) AttachThreadInput(myTid, fgTid, TRUE);
    ShowWindow(h, IsIconic(h) ? SW_RESTORE : SW_SHOW);   // restore if minimised (e.g. after Win+D) so we actually come back, not just re-activate a minimised window
    SetForegroundWindow(h);
    BringWindowToTop(h);
    SetFocus(h);
    if (fgTid && fgTid != myTid) AttachThreadInput(myTid, fgTid, FALSE);
}

// ---------------------------------------------------------------------------
// Phase 6 - Windows integration: autostart, shell-replacement kiosk, and a
// helper to bring the real Windows shell back. Autostart + kiosk state is stored
// in the registry (source of truth), read live so the Settings rows never drift.
// ---------------------------------------------------------------------------
static std::wstring ExePath()
{
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return exe;
}

// Autostart: HKCU\...\Run value "Launcher" = quoted exe path. Per-user, no admin.
static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunVal = L"Launcher";
static bool AutostartEnabled()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, sz = 0;
    LONG r = RegQueryValueExW(k, kRunVal, nullptr, &type, nullptr, &sz);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && type == REG_SZ && sz > sizeof(wchar_t);
}
static void SetAutostart(bool on)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (on) { std::wstring v = L"\"" + ExePath() + L"\"";
              RegSetValueExW(k, kRunVal, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t))); }
    else      RegDeleteValueW(k, kRunVal);
    RegCloseKey(k);
}

// Kiosk / shell replacement: the launcher takes the HKCU Winlogon\Shell value, so Windows boots straight
// into it with no explorer/desktop/taskbar. Per-user (no admin). We back up whatever Shell value was there
// (empty = the machine default) before overwriting, so disabling restores it exactly.
static const wchar_t* kWinlogonKey = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
static const wchar_t* kShellVal    = L"Shell";
static std::wstring GetWinlogonShell()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kWinlogonKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return std::wstring();
    wchar_t buf[1024];
    DWORD type = 0, sz = sizeof(buf) - sizeof(wchar_t);      // reserve room to null-terminate
    LONG r = RegQueryValueExW(k, kShellVal, nullptr, &type, (BYTE*)buf, &sz);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_SZ) return std::wstring();
    buf[sz / sizeof(wchar_t)] = 0;
    return std::wstring(buf);
}
static bool KioskEnabled()
{
    std::wstring cur = GetWinlogonShell();
    return !cur.empty() && _wcsicmp(cur.c_str(), ExePath().c_str()) == 0;
}
static void SetKiosk(bool on)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kWinlogonKey, 0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (on)
    {
        std::wstring cur = GetWinlogonShell(), me = ExePath();
        if (cur.empty() || _wcsicmp(cur.c_str(), me.c_str()) != 0) { gShellBackup = cur; SaveConfig(); }
        RegSetValueExW(k, kShellVal, 0, REG_SZ, (const BYTE*)me.c_str(), (DWORD)((me.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        if (!gShellBackup.empty())
            RegSetValueExW(k, kShellVal, 0, REG_SZ, (const BYTE*)gShellBackup.c_str(), (DWORD)((gShellBackup.size() + 1) * sizeof(wchar_t)));
        else
            RegDeleteValueW(k, kShellVal);   // no prior per-user shell: drop ours so Windows uses the machine default (explorer)
        gShellBackup.clear(); SaveConfig();
    }
    RegCloseKey(k);
}

// Bring back the real Windows shell (explorer) - called on exit when we were launched AS the shell, so the
// user lands on a normal desktop instead of a black screen.
static void LaunchExplorer()
{
    wchar_t win[MAX_PATH] = {};
    GetWindowsDirectoryW(win, MAX_PATH);
    std::wstring exp = std::wstring(win) + L"\\explorer.exe";
    ShellExecuteW(nullptr, nullptr, exp.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// TRACK_WINDOW helpers: find a visible top-level application window owned by a process whose image name
// matches gWatchProc. Used to detect when a single-instance app has shown its window (and later hidden it).
static std::wstring ProcImageName(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return std::wstring();
    wchar_t buf[MAX_PATH]; DWORD n = MAX_PATH; std::wstring out;
    if (QueryFullProcessImageNameW(h, 0, buf, &n))
    {
        std::wstring full(buf, n);
        size_t p = full.find_last_of(L"\\/");
        out = (p == std::wstring::npos) ? full : full.substr(p + 1);
    }
    CloseHandle(h);
    return out;
}
struct FindWndCtx { const std::wstring* name; HWND self; HWND found; };
static BOOL CALLBACK FindAppWndProc(HWND h, LPARAM lp)
{
    FindWndCtx* c = (FindWndCtx*)lp;
    if (h == c->self)                 return TRUE;         // not the launcher itself
    if (!IsWindowVisible(h))          return TRUE;         // only a shown window counts
    if (GetWindow(h, GW_OWNER))       return TRUE;         // skip owned / tool / dialog windows
    RECT r; if (!GetWindowRect(h, &r) || r.right - r.left <= 0 || r.bottom - r.top <= 0) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (!pid)                         return TRUE;
    std::wstring nm = ProcImageName(pid);
    if (!nm.empty() && _wcsicmp(nm.c_str(), c->name->c_str()) == 0) { c->found = h; return FALSE; }
    return TRUE;
}
static HWND FindAppWindow(const std::wstring& imageName, HWND self)
{
    if (imageName.empty()) return nullptr;
    FindWndCtx c{ &imageName, self, nullptr };
    EnumWindows(FindAppWndProc, (LPARAM)&c);
    return c.found;
}
static std::wstring BaseName(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? path : path.substr(p + 1);
}

// Return-to-home watchdog: would the Windows desktop be showing in front right now? Because the launcher
// is a FULLSCREEN window, every other open app sits BEHIND it and still reports IsWindowVisible=true - so
// enumerating all windows always finds a background app and never fires. Instead we look ONLY at the
// FOREGROUND window: home is showing when nothing real is in front - the foreground is null, hidden,
// minimised, or one of the shell's own windows (desktop / taskbar). A real app in front (the user
// switched to another app, or it is still open on top) returns false so we stay behind.
static bool DesktopWouldShow(HWND self)
{
    HWND fg = GetForegroundWindow();
    if (!fg)        return true;                            // nothing focused -> desktop
    if (fg == self) return false;                           // us (already home)
    if (!IsWindowVisible(fg) || IsIconic(fg)) return true;  // the front window hid to tray / minimised
    wchar_t cls[64] = {}; GetClassNameW(fg, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
        !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd"))
        return true;                                        // the desktop / taskbar is in front
    return false;                                           // a real app window is in front -> stay behind
}

// Launch a menu item. Items go through CreateProcess (lpApplicationName = null so the command line
// is PATH-searched); if that fails (URLs, .lnk, documents) we fall back to the shell "open" verb.
// Returns true if something was started. Tracking (hide/wait/focus-regain) is per the item's flags.
static bool LaunchItem(const MenuItem& it)
{
    if (it.target.empty() || gTrackMode != TRACK_NONE) return false;   // nothing to launch / already tracking one

    std::wstring wdir = DirOf(it.target);               // start the process in the target's own folder
    const wchar_t* dirp = wdir.empty() ? nullptr : wdir.c_str();
    HANDLE proc = nullptr;

    {
        std::wstring cmd = L"\"" + it.target + L"\"";
        if (!it.arguments.empty()) cmd += L" " + it.arguments;
        std::vector<wchar_t> cmdbuf(cmd.begin(), cmd.end());
        cmdbuf.push_back(L'\0');

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        BOOL ok = CreateProcessW(nullptr, cmdbuf.data(), nullptr, nullptr, FALSE,
                                 CREATE_SUSPENDED, nullptr, dirp, &si, &pi);
        if (ok)
        {
            if (gJob) AssignProcessToJobObject(gJob, pi.hProcess); // group descendants
            ResumeThread(pi.hThread);
            CloseHandle(pi.hThread);
            proc = pi.hProcess;
        }
        else
        {
            // Not a runnable exe (URL / document / .lnk): let the shell open it.
            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.fMask  = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb = L"open";
            sei.lpFile = it.target.c_str();
            sei.lpParameters = it.arguments.empty() ? nullptr : it.arguments.c_str();
            sei.lpDirectory  = dirp;
            sei.nShow  = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&sei)) return false;
            proc = sei.hProcess;                          // often null (handled by shell)
        }
    }

    AllowSetForegroundWindow(ASFW_ANY);   // let the launched app (or the single-instance it hands off to) take the foreground

    // Set up tracking. Window-watch mode takes priority: for single-instance / tray apps the spawned
    // process hands off and exits immediately, so we do NOT wait on it - we watch the app's window and
    // return to the launcher when it closes or hides.
    if (it.trackWindow)
    {
        if (proc) CloseHandle(proc);                        // the spawned launcher process is not what we track
        gTrackMode     = TRACK_WINDOW;
        gWatchProc     = it.processName.empty() ? BaseName(it.target) : it.processName;
        gWatchWnd      = nullptr;
        gWatchDeadline = GetTickCount64() + 20000;          // up to 20s for the app to show its window
        KeepAwake(false);
    }
    else if (proc && it.waitForExit)
    {
        gRunningProc = proc;             // the app opens on top; the launcher stays behind, waiting for exit
        gTrackMode   = TRACK_PROC;
        KeepAwake(false);                // let the launched app / OS manage power
    }
    else if (proc)
    {
        CloseHandle(proc);
    }
    return true;
}

// Called from the main loop each iteration (even while occluded). Brings the launcher back to the front
// when the tracked app is done: TRACK_PROC = its process exited; TRACK_WINDOW = its window closed or hid.
static void PumpRunningProcess()
{
    if (gTrackMode == TRACK_PROC)
    {
        if (!gRunningProc) { gTrackMode = TRACK_NONE; return; }
        if (WaitForSingleObject(gRunningProc, 0) != WAIT_OBJECT_0) return;  // still running
        CloseHandle(gRunningProc); gRunningProc = nullptr; gTrackMode = TRACK_NONE;
        KeepAwake(true);                                  // launcher is foreground again
        ForceForeground(gHwnd);                           // app exited: bring the launcher back to the front
        return;
    }
    if (gTrackMode == TRACK_WINDOW)
    {
        if (!gWatchWnd)                                   // still looking for the app's window
        {
            HWND w = FindAppWindow(gWatchProc, gHwnd);
            if (w) gWatchWnd = w;                         // locked on: the app has shown its window
            else if (GetTickCount64() > gWatchDeadline)   // never showed one (bad hint / launch failed): give up
            { gTrackMode = TRACK_NONE; KeepAwake(true); }
            return;
        }
        // Watching: the app is up. Return to the launcher once its window is gone (closed) or no longer
        // shown (hidden to the tray, or minimised to the taskbar).
        if (!IsWindow(gWatchWnd) || !IsWindowVisible(gWatchWnd) || IsIconic(gWatchWnd))
        {
            gWatchWnd = nullptr; gTrackMode = TRACK_NONE;
            KeepAwake(true);
            ForceForeground(gHwnd);
        }
        return;
    }
}
