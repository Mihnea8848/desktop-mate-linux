/*
 * Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ghost.exe: runs inside Desktop Mate's wineserver and mirrors the real KWin
 * windows as invisible Win32 windows, so the game's window scan finds them.
 * Moves, minimizes and activations the game applies to a mirror are written
 * to cmd/ and applied to the real window by the KWin script.
 *
 * The prefix runs ghost.exe unmanaged (Managed=N) so KWin never touches the
 * mirrors, and each mirror has an empty window region so it is never drawn.
 *
 * The pointer position from KWin is fed to Wine while the pointer is over
 * Wayland windows, where Xwayland gets no motion.
 *
 * Env: GHOST_NO_WRITEBACK=1 disables the command channel.
 *      GHOST_SCALE=<n> Xwayland scale; KWin rects are multiplied by it.
 *      GHOST_GAME_TRANSPARENT=1 keeps WS_EX_TRANSPARENT on the game window.
 *      GHOST_HOOK=0 does not load dmhook.dll (settings section) into the game.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SNAPSHOT_PATH "Z:\\dev\\shm\\ghost-bridge\\windows.tsv"
#define CMD_DIR       "Z:\\dev\\shm\\ghost-bridge\\cmd\\"
#define CURSOR_PATH   "Z:\\dev\\shm\\ghost-bridge\\cursor.txt"
#define LOG_PATH      "Z:\\dev\\shm\\ghost-bridge\\ghost.log"
#define MAX_GHOSTS    128
#define BRIDGE_DIR    "Z:\\dev\\shm\\ghost-bridge"
#define POLL_MS       50    /* while a mirror is on hold, or without change notifications */
#define IDLE_MS       1000  /* otherwise: game and foreground checks */
#define HOLD_MS       400   /* ignore snapshot geometry this long after the game moved a mirror */
#define GAME_WAIT_MS  180000
#define LOG_MAX       (1024 * 1024)
#define MENU_MAX_MS   8000  /* close a game popup menu left open this long */
#define X_LIVE_MS     200   /* X moved the cursor this recently: it is over an X11 window */

#ifndef EVENT_SYSTEM_FOREGROUND
#define EVENT_SYSTEM_FOREGROUND     0x0003
#endif
#ifndef EVENT_SYSTEM_MOVESIZEEND
#define EVENT_SYSTEM_MOVESIZEEND    0x000B
#endif
#ifndef EVENT_OBJECT_LOCATIONCHANGE
#define EVENT_OBJECT_LOCATIONCHANGE 0x800B
#endif

typedef struct {
    char  id[64];
    HWND  hwnd;
    int   x, y, w, h;
    int   dock;
    int   seen;
    DWORD hold_until;
    WCHAR title[256];
} Ghost;

static Ghost    g_ghosts[MAX_GHOSTS];
static int      g_count;
static int      g_self;   /* >0 while ghost.exe changes its own windows */
static int      g_writeback = 1;
static DWORD    g_style, g_exstyle;
static FILE    *g_log;
static unsigned g_cmdseq;
static double   g_scale = 1.0;
static int      g_game_transparent;
static DWORD    g_active_changed;  /* tick count of the last real active-window change */
static long     g_logsize;

static void logf_(const char *fmt, ...)
{
    if (!g_log) return;
    if (g_logsize > LOG_MAX) {
        g_log = freopen(LOG_PATH, "w", g_log);
        g_logsize = 0;
        if (!g_log) return;
    }
    va_list ap;
    va_start(ap, fmt);
    g_logsize += fprintf(g_log, "%lu ", (unsigned long)GetTickCount());
    g_logsize += vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    fflush(g_log);
    va_end(ap);
}

static Ghost *ghost_by_hwnd(HWND h)
{
    for (int i = 0; i < g_count; ++i)
        if (g_ghosts[i].hwnd == h) return &g_ghosts[i];
    return NULL;
}

static Ghost *ghost_by_id(const char *id)
{
    for (int i = 0; i < g_count; ++i)
        if (!strcmp(g_ghosts[i].id, id)) return &g_ghosts[i];
    return NULL;
}

/* write to .tmp, then rename, so ghost-bridged only sees complete files */
static void send_cmd(const char *line)
{
    if (!g_writeback) return;
    char tmp[MAX_PATH], dst[MAX_PATH];
    unsigned seq = ++g_cmdseq;
    unsigned long pid = GetCurrentProcessId();
    snprintf(tmp, sizeof tmp, CMD_DIR "%08lx-%08u.tmp", pid, seq);
    snprintf(dst, sizeof dst, CMD_DIR "%08lx-%08u.cmd", pid, seq);
    HANDLE f = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        logf_("cmd: cannot create %s (%lu)", tmp, GetLastError());
        return;
    }
    DWORD put;
    WriteFile(f, line, (DWORD)strlen(line), &put, NULL);
    WriteFile(f, "\n", 1, &put, NULL);
    CloseHandle(f);
    if (!MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING))
        logf_("cmd: rename failed (%lu)", GetLastError());
    logf_("cmd: %s", line);
}

static void send_simple(const char *verb, HWND h)
{
    Ghost *g = ghost_by_hwnd(h);
    if (!g) return;
    char line[96];
    snprintf(line, sizeof line, "%s\t%s", verb, g->id);
    send_cmd(line);
}

static void on_moved(HWND h, const WINDOWPOS *wpos)
{
    Ghost *g = ghost_by_hwnd(h);
    if (!g || IsIconic(h)) return;
    if ((wpos->flags & SWP_NOMOVE) && (wpos->flags & SWP_NOSIZE)) return;

    RECT r;
    GetWindowRect(h, &r);
    int x = r.left, y = r.top, w = r.right - r.left, hh = r.bottom - r.top;
    if (x == g->x && y == g->y && w == g->w && hh == g->h) return;

    g->x = x; g->y = y; g->w = w; g->h = hh;
    g->hold_until = GetTickCount() + HOLD_MS;
    char line[160];
    snprintf(line, sizeof line, "move\t%s\t%d\t%d\t%d\t%d", g->id,
             (int)(x / g_scale + 0.5), (int)(y / g_scale + 0.5),
             (int)(w / g_scale + 0.5), (int)(hh / g_scale + 0.5));
    send_cmd(line);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_NCHITTEST:     return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_CLOSE:         return 0;
    case WM_ERASEBKGND:    return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_WINDOWPOSCHANGED:
        if (!g_self) on_moved(h, (const WINDOWPOS *)lp);
        break;
    case WM_SIZE:
        if (!g_self && wp == SIZE_MINIMIZED) send_simple("minimize", h);
        break;
    case WM_ACTIVATE:
        if (!g_self && LOWORD(wp) != WA_INACTIVE) send_simple("activate", h);
        break;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void to_wide(const char *utf8, WCHAR *out, int cch)
{
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cch - 1) <= 0) out[0] = 0;
    out[cch - 1] = 0;
}

typedef struct {
    char id[64];
    int  x, y, w, h, dock, active;
    char title[512];
} Rec;

/* snapshot lines: id rank x y w h active kind title (tab separated, bottom to top) */
static int parse_snapshot(char *buf, Rec *recs, int max)
{
    int n = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *fs = NULL;
        char *id   = strtok_r(line, "\t", &fs);
        char *rank = strtok_r(NULL, "\t", &fs);
        char *xs   = strtok_r(NULL, "\t", &fs);
        char *ys   = strtok_r(NULL, "\t", &fs);
        char *ws   = strtok_r(NULL, "\t", &fs);
        char *hs   = strtok_r(NULL, "\t", &fs);
        char *as   = strtok_r(NULL, "\t", &fs);
        char *kind = strtok_r(NULL, "\t", &fs);
        (void)rank;
        if (!id || !kind || n >= max) continue;
        Rec *r = &recs[n];
        memset(r, 0, sizeof *r);
        r->x = (int)(atoi(xs) * g_scale + 0.5);
        r->y = (int)(atoi(ys) * g_scale + 0.5);
        r->w = (int)(atoi(ws) * g_scale + 0.5);
        r->h = (int)(atoi(hs) * g_scale + 0.5);
        if (r->w <= 0 || r->h <= 0) continue;
        strncpy(r->id, id, sizeof r->id - 1);
        r->dock = !strcmp(kind, "dock");
        r->active = atoi(as);
        strncpy(r->title, fs ? fs : "", sizeof r->title - 1);
        n++;
    }
    return n;
}

static Ghost *create_ghost(const Rec *rc, const WCHAR *title, int second_dock)
{
    if (g_count >= MAX_GHOSTS) return NULL;
    const WCHAR *cls = L"KWinGhost";
    DWORD style = g_style;
    if (rc->dock) {
        cls = second_dock ? L"Shell_SecondaryTrayWnd" : L"Shell_TrayWnd";
        style = WS_POPUP | WS_CLIPSIBLINGS;
    }
    HWND h = CreateWindowExW(g_exstyle, cls, title, style, rc->x, rc->y, rc->w, rc->h,
                             NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!h) {
        logf_("CreateWindowEx failed for %s (%lu)", rc->id, GetLastError());
        return NULL;
    }
    SetWindowRgn(h, CreateRectRgn(0, 0, 0, 0), FALSE);
    ShowWindow(h, SW_SHOWNOACTIVATE);

    Ghost *g = &g_ghosts[g_count++];
    memset(g, 0, sizeof *g);
    strncpy(g->id, rc->id, sizeof g->id - 1);
    g->hwnd = h;
    g->dock = rc->dock;
    g->x = rc->x; g->y = rc->y; g->w = rc->w; g->h = rc->h;
    wcsncpy(g->title, title, 255);
    logf_("new ghost %p %s %d,%d %dx%d dock=%d '%s'", (void *)h, rc->id,
          rc->x, rc->y, rc->w, rc->h, rc->dock, rc->title);
    return g;
}

/* restack only on change: every restack moves X windows, which KWin reports
 * back as a stacking change */
static void restack(HWND *norm, int nnorm, HWND *dock, int ndock)
{
    static HWND prev_norm[MAX_GHOSTS], prev_dock[MAX_GHOSTS];
    static int prev_nnorm = -1, prev_ndock = -1;
    if (nnorm == prev_nnorm && ndock == prev_ndock &&
        !memcmp(norm, prev_norm, nnorm * sizeof(HWND)) &&
        !memcmp(dock, prev_dock, ndock * sizeof(HWND)))
        return;

    const UINT flags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOSENDCHANGING;
    for (int i = nnorm - 1; i >= 0; --i)   /* top to bottom, each to the bottom */
        SetWindowPos(norm[i], HWND_BOTTOM, 0, 0, 0, 0, flags);
    for (int i = 0; i < ndock; ++i)
        SetWindowPos(dock[i], HWND_TOP, 0, 0, 0, 0, flags);

    memcpy(prev_norm, norm, nnorm * sizeof(HWND));
    memcpy(prev_dock, dock, ndock * sizeof(HWND));
    prev_nnorm = nnorm;
    prev_ndock = ndock;
}

static int holding(void)
{
    DWORD now = GetTickCount();
    for (int i = 0; i < g_count; ++i)
        if ((LONG)(g_ghosts[i].hold_until - now) > 0) return 1;
    return 0;
}

static void tick(void)
{
    static char buf[256 * 1024], last[256 * 1024];
    static DWORD lastlen;
    static Rec recs[MAX_GHOSTS];

    HANDLE f = CreateFileA(SNAPSHOT_PATH, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got = 0;
    BOOL ok = ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    if (!ok) return;
    buf[got] = 0;

    DWORD now = GetTickCount();
    if (got == lastlen && !memcmp(buf, last, got) && !holding()) return;
    memcpy(last, buf, got);
    lastlen = got;

    int nrec = parse_snapshot(buf, recs, MAX_GHOSTS);
    HWND norm[MAX_GHOSTS], dock[MAX_GHOSTS];
    int nnorm = 0, ndock = 0;
    HWND active = NULL;

    for (int i = 0; i < g_count; ++i) g_ghosts[i].seen = 0;
    g_self++;
    for (int i = 0; i < nrec; ++i) {
        Rec *rc = &recs[i];
        WCHAR title[256];
        to_wide(rc->title, title, 256);

        Ghost *g = ghost_by_id(rc->id);
        if (!g) {
            g = create_ghost(rc, title, ndock > 0);
            if (!g) continue;
        } else if (g->x != rc->x || g->y != rc->y || g->w != rc->w || g->h != rc->h) {
            if ((LONG)(g->hold_until - now) <= 0) {
                SetWindowPos(g->hwnd, NULL, rc->x, rc->y, rc->w, rc->h,
                             SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSENDCHANGING);
                g->x = rc->x; g->y = rc->y; g->w = rc->w; g->h = rc->h;
                NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE, g->hwnd, OBJID_WINDOW, CHILDID_SELF);
                NotifyWinEvent(EVENT_SYSTEM_MOVESIZEEND, g->hwnd, OBJID_WINDOW, CHILDID_SELF);
            }
        } else {
            g->hold_until = 0;
        }
        if (wcscmp(g->title, title)) {
            SetWindowTextW(g->hwnd, title);
            wcsncpy(g->title, title, 255);
        }
        g->seen = 1;
        if (g->dock) {
            dock[ndock++] = g->hwnd;
        } else {
            norm[nnorm++] = g->hwnd;
            if (rc->active) active = g->hwnd;
        }
    }

    for (int i = 0; i < g_count; ) {
        if (g_ghosts[i].seen) { ++i; continue; }
        logf_("drop ghost %p %s", (void *)g_ghosts[i].hwnd, g_ghosts[i].id);
        DestroyWindow(g_ghosts[i].hwnd);
        g_ghosts[i] = g_ghosts[--g_count];
    }

    restack(norm, nnorm, dock, ndock);

    static HWND last_active;
    if (active && active != last_active) {
        NotifyWinEvent(EVENT_SYSTEM_FOREGROUND, active, OBJID_WINDOW, CHILDID_SELF);
        last_active = active;
        g_active_changed = GetTickCount();
    }
    g_self--;
}

/* Windows skips layered + WS_EX_TRANSPARENT windows in WindowFromPoint, so
 * the game finds the window under the character. GE-Proton 11-7 and newer
 * take the input shape from the pixels, so the flag no longer stays set, and
 * the game then only finds itself. Input is not affected by the flag. */
static void keep_game_transparent(void)
{
    HWND game = FindWindowW(L"UnityWndClass", NULL);
    if (!game) return;
    LONG ex = GetWindowLongW(game, GWL_EXSTYLE);
    if ((ex & (WS_EX_TRANSPARENT | WS_EX_LAYERED)) == (WS_EX_TRANSPARENT | WS_EX_LAYERED)) return;
    SetWindowLongW(game, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT | WS_EX_LAYERED);
}

/* loads dmhook.dll from this program's folder into the game */
static void inject_hook(DWORD pid)
{
    WCHAR path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    WCHAR *slash = wcsrchr(path, L'\\');
    if (!n || !slash || slash - path + 12 >= MAX_PATH) return;
    wcscpy(slash + 1, L"dmhook.dll");
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                              PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!proc) {
        logf_("hook: cannot open the game process (%lu)", GetLastError());
        return;
    }
    SIZE_T size = (wcslen(path) + 1) * sizeof(WCHAR);
    void *mem = VirtualAllocEx(proc, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    void *load = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW");
    HANDLE t = NULL;
    if (mem && WriteProcessMemory(proc, mem, path, size, NULL))
        t = CreateRemoteThread(proc, NULL, 0, (LPTHREAD_START_ROUTINE)load, mem, 0, NULL);
    if (t) {
        WaitForSingleObject(t, 10000);
        DWORD module = 0;
        GetExitCodeThread(t, &module);
        logf_("hook: loaded dmhook.dll: %s", module ? "yes" : "no");
        CloseHandle(t);
    } else {
        logf_("hook: injection failed (%lu)", GetLastError());
    }
    CloseHandle(proc);
}

/* Wine's cursor stops where the pointer left the last X11 window. Move it to
 * the KWin position unless X is delivering motion itself or a button is down
 * (a drag that X tracks). */
static void follow_cursor(void)
{
    static POINT seen, sent;
    static DWORD x_moved;
    char buf[64];
    DWORD got = 0;
    HANDLE f = CreateFileA(CURSOR_PATH, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    BOOL ok = ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    if (!ok) return;
    buf[got] = 0;
    POINT to;
    if (sscanf(buf, "%ld %ld", &to.x, &to.y) != 2) return;
    to.x = (LONG)(to.x * g_scale);
    to.y = (LONG)(to.y * g_scale);

    POINT cur;
    DWORD now = GetTickCount();
    if (!GetCursorPos(&cur)) return;
    if ((cur.x != seen.x || cur.y != seen.y) && (cur.x != sent.x || cur.y != sent.y)) x_moved = now;
    seen = cur;
    if (now - x_moved < X_LIVE_MS) return;
    if (GetAsyncKeyState(VK_LBUTTON) < 0 || GetAsyncKeyState(VK_RBUTTON) < 0) return;
    if (to.x == cur.x && to.y == cur.y) return;

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) return;
    INPUT in = { .type = INPUT_MOUSE };
    in.mi.dx = (LONG)((((LONGLONG)(to.x - vx) << 16) + vw - 1) / vw);
    in.mi.dy = (LONG)((((LONGLONG)(to.y - vy) << 16) + vh - 1) / vh);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof in);
    sent = seen = to;
}

/* A popup menu (tray menu) closes on Windows when the user clicks elsewhere.
 * Clicks on Wayland windows never reach Wine, so close the game's open menus
 * when another real window gets activated, or after MENU_MAX_MS. */
static void close_stale_menus(DWORD game_pid)
{
    static HWND open_menu;
    static DWORD opened_at;
    HWND menu = NULL;
    for (HWND m = FindWindowExW(NULL, NULL, L"#32768", NULL); m; m = FindWindowExW(NULL, m, L"#32768", NULL)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(m, &pid);
        if (pid == game_pid && IsWindowVisible(m)) { menu = m; break; }
    }
    if (!menu) { open_menu = NULL; return; }
    DWORD now = GetTickCount();
    if (menu != open_menu) { open_menu = menu; opened_at = now; return; }
    if ((LONG)(g_active_changed - opened_at) > 0 || now - opened_at > MENU_MAX_MS) {
        PostMessageW(menu, WM_KEYDOWN, VK_ESCAPE, 0);
        PostMessageW(menu, WM_KEYUP, VK_ESCAPE, 0);
        logf_("closed game menu %p", (void *)menu);
        open_menu = NULL;
    }
}

/* KWin never activates the unmanaged game window, and the game ignores its
 * right-click menu while another Wine window is foreground */
static void keep_game_foreground(void)
{
    static DWORD last_try;
    HWND game = FindWindowW(L"UnityWndClass", NULL);
    if (!game || !IsWindowVisible(game)) return;

    DWORD game_pid = 0, fg_pid = 0;
    GetWindowThreadProcessId(game, &game_pid);
    HWND fg = GetForegroundWindow();
    if (fg) GetWindowThreadProcessId(fg, &fg_pid);
    if (fg && fg_pid == game_pid) return;
    if (GetTickCount() - last_try < 1000) return;
    last_try = GetTickCount();

    /* Wine refuses SetForegroundWindow from a background process; sharing the
     * input state of the foreground thread lifts that restriction */
    DWORD me = GetCurrentThreadId();
    DWORD fg_tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    BOOL attached = fg_tid && fg_tid != me && AttachThreadInput(me, fg_tid, TRUE);
    g_self++;
    BOOL ok = SetForegroundWindow(game);
    g_self--;
    if (attached) AttachThreadInput(me, fg_tid, FALSE);

    static HWND last_fg;
    static BOOL last_ok = -1;
    if (fg != last_fg || ok != last_ok)
        logf_("foreground was %p (pid %lu), restored to game: %d", (void *)fg, (unsigned long)fg_pid, ok);
    last_fg = fg;
    last_ok = ok;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev; (void)cmd; (void)show;
    g_log = fopen(LOG_PATH, "w");
    CreateDirectoryA(CMD_DIR, NULL);
    if (getenv("GHOST_NO_WRITEBACK")) g_writeback = 0;
    if (getenv("GHOST_SCALE") && atof(getenv("GHOST_SCALE")) > 0) g_scale = atof(getenv("GHOST_SCALE"));
    const char *gt = getenv("GHOST_GAME_TRANSPARENT");
    if (gt && *gt && strcmp(gt, "0")) g_game_transparent = 1;

    /* styles of an ordinary application window */
    g_style   = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPSIBLINGS;
    g_exstyle = WS_EX_NOACTIVATE;

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance   = inst;
    wc.lpszClassName = L"KWinGhost";              RegisterClassW(&wc);
    wc.lpszClassName = L"Shell_TrayWnd";          RegisterClassW(&wc);
    wc.lpszClassName = L"Shell_SecondaryTrayWnd"; RegisterClassW(&wc);

    logf_("start pid=%lu writeback=%d scale=%.2f game_transparent=%d", (unsigned long)GetCurrentProcessId(),
          g_writeback, g_scale, g_game_transparent);

    /* wake on changes in the bridge dir instead of polling; fall back to
     * polling if change notifications are not available */
    HANDLE change = FindFirstChangeNotificationA(BRIDGE_DIR, FALSE,
                                                 FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (change == INVALID_HANDLE_VALUE) {
        change = NULL;
        logf_("no change notification (%lu), polling every %d ms", GetLastError(), POLL_MS);
    }

    /* exit with the game, otherwise Proton and Steam consider it still running */
    HANDLE game_proc = NULL;
    DWORD game_pid = 0;
    DWORD started = GetTickCount();
    for (;;) {
        HANDLE handles[2];
        DWORD n = 0;
        if (change) handles[n++] = change;
        if (game_proc) handles[n++] = game_proc;
        DWORD timeout = (!change || holding()) ? POLL_MS : IDLE_MS;
        DWORD r = MsgWaitForMultipleObjects(n, handles, FALSE, timeout, QS_ALLINPUT);
        if (change && r == WAIT_OBJECT_0) FindNextChangeNotification(change);

        MSG msg;
        int quit = 0;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) quit = 1;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit) break;

        if (!game_proc) {
            HWND game = FindWindowW(L"UnityWndClass", NULL);
            DWORD pid = 0;
            if (game) GetWindowThreadProcessId(game, &pid);
            if (pid) {
                game_proc = OpenProcess(SYNCHRONIZE, FALSE, pid);
                game_pid = pid;
                logf_("tracking game pid %lu", (unsigned long)pid);
                const char *hook = getenv("GHOST_HOOK");
                if (!hook || strcmp(hook, "0")) inject_hook(pid);
            } else if (GetTickCount() - started > GAME_WAIT_MS) {
                logf_("game window never appeared, exiting");
                break;
            }
        } else if (WaitForSingleObject(game_proc, 0) == WAIT_OBJECT_0) {
            logf_("game exited, exiting");
            break;
        }
        tick();
        if (game_proc) keep_game_foreground();
        if (game_proc && g_game_transparent) keep_game_transparent();
        if (game_proc) close_stale_menus(game_pid);
        if (game_proc) follow_cursor();
    }
    for (int i = 0; i < g_count; ++i) DestroyWindow(g_ghosts[i].hwnd);
    return 0;
}
