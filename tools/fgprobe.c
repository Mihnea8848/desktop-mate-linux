/*
 * Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fgprobe.exe [fg]: writes Wine's foreground window, mouse button state and
 * cursor position to /dev/shm/ghost-bridge/fg.txt. "fg" makes the game
 * foreground, "desk" gives the foreground to Wine's desktop window, "steal"
 * makes a window of this process foreground and then exits, "wake" posts the
 * activation messages a focused window receives, "wfp X Y" reports which
 * window WindowFromPoint and ChildWindowFromPointEx return at X,Y, "tray l|r"
 * clicks Wine's tray icon window for the game, "closemenu" sends Escape to open
 * popup menus.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned key(int vk) { return (unsigned)(GetAsyncKeyState(vk) & 0xffff); }

int main(int argc, char **argv)
{
    freopen("Z:\\dev\\shm\\ghost-bridge\\fg.txt", "w", stdout);
    HWND game = FindWindowW(L"UnityWndClass", NULL);
    HWND fg = GetForegroundWindow();
    char cls[128] = "";
    if (fg) GetClassNameA(fg, cls, sizeof cls);

    GUITHREADINFO gi = { sizeof gi };
    GetGUIThreadInfo(game ? GetWindowThreadProcessId(game, NULL) : 0, &gi);
    POINT pt;
    GetCursorPos(&pt);

    printf("buttons L=%04x R=%04x M=%04x cursor=%ld,%ld\n",
           key(VK_LBUTTON), key(VK_RBUTTON), key(VK_MBUTTON), pt.x, pt.y);
    printf("game=%p fg=%p fgclass=%s active=%p focus=%p\n",
           (void *)game, (void *)fg, cls, (void *)gi.hwndActive, (void *)gi.hwndFocus);
    if (game) {
        DWORD_PTR res = 0;
        LRESULT ok = SendMessageTimeoutW(game, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 2000, &res);
        printf("hung=%d responds=%d iconic=%d visible=%d\n", IsHungAppWindow(game), ok != 0,
               IsIconic(game), IsWindowVisible(game));
    }
    for (HWND g = FindWindowExW(NULL, NULL, L"KWinGhost", NULL); g; g = FindWindowExW(NULL, g, L"KWinGhost", NULL)) {
        DWORD_PTR res = 0;
        DWORD pid = 0;
        GetWindowThreadProcessId(g, &pid);
        LRESULT ok = SendMessageTimeoutW(g, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 2000, &res);
        printf("ghost %p pid=%lu hung=%d responds=%d\n", (void *)g, (unsigned long)pid, IsHungAppWindow(g), ok != 0);
    }
    if (argc > 1 && !strcmp(argv[1], "fg") && game) {
        BOOL ok = SetForegroundWindow(game);
        printf("SetForegroundWindow=%d fg=%p\n", ok, (void *)GetForegroundWindow());
    }
    if (argc > 1 && !strcmp(argv[1], "desk")) {
        BOOL ok = SetForegroundWindow(GetDesktopWindow());
        printf("SetForegroundWindow(desktop)=%d fg=%p\n", ok, (void *)GetForegroundWindow());
    }
    if (argc > 3 && !strcmp(argv[1], "wfp")) {
        POINT p = { atol(argv[2]), atol(argv[3]) };
        HWND w = WindowFromPoint(p);
        char c[128] = "", t[128] = "";
        if (w) { GetClassNameA(w, c, sizeof c); GetWindowTextA(w, t, sizeof t); }
        printf("WindowFromPoint(%ld,%ld)=%p cls='%s' title='%s'\n", p.x, p.y, (void *)w, c, t);
        HWND d = ChildWindowFromPointEx(GetDesktopWindow(), p, CWP_SKIPINVISIBLE);
        c[0] = t[0] = 0;
        if (d) { GetClassNameA(d, c, sizeof c); GetWindowTextA(d, t, sizeof t); }
        printf("ChildWindowFromPointEx(desktop)=%p cls='%s' title='%s'\n", (void *)d, c, t);
    }
    if (argc > 2 && !strcmp(argv[1], "tray")) {
        HWND icon = FindWindowW(L"__wine_tray_icon", NULL);
        UINT down = argv[2][0] == 'r' ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
        UINT up = argv[2][0] == 'r' ? WM_RBUTTONUP : WM_LBUTTONUP;
        printf("tray icon window %p\n", (void *)icon);
        if (icon) {
            PostMessageW(icon, down, 0, MAKELPARAM(5, 5));
            PostMessageW(icon, up, 0, MAKELPARAM(5, 5));
        }
    }
    if (argc > 1 && !strcmp(argv[1], "closemenu")) {
        for (HWND m = FindWindowExW(NULL, NULL, L"#32768", NULL); m; m = FindWindowExW(NULL, m, L"#32768", NULL)) {
            if (!IsWindowVisible(m)) continue;
            printf("closing menu %p\n", (void *)m);
            PostMessageW(m, WM_KEYDOWN, VK_ESCAPE, 0);
            PostMessageW(m, WM_KEYUP, VK_ESCAPE, 0);
        }
    }
    if (argc > 1 && !strcmp(argv[1], "wake") && game) {
        DWORD tid = GetWindowThreadProcessId(game, NULL);
        PostMessageW(game, WM_ACTIVATEAPP, TRUE, (LPARAM)GetCurrentThreadId());
        PostMessageW(game, WM_ACTIVATE, WA_ACTIVE, 0);
        PostMessageW(game, WM_SETFOCUS, 0, 0);
        printf("wake: posted activation to thread %lu\n", (unsigned long)tid);
    }
    if (argc > 1 && !strcmp(argv[1], "steal")) {
        HWND w = CreateWindowExA(0, "STATIC", "fgprobe", WS_POPUP | WS_VISIBLE,
                                 0, 0, 1, 1, NULL, NULL, NULL, NULL);
        BOOL ok = SetForegroundWindow(w);
        printf("steal: SetForegroundWindow(own)=%d fg=%p\n", ok, (void *)GetForegroundWindow());
        DestroyWindow(w);
        printf("after destroy fg=%p\n", (void *)GetForegroundWindow());
    }
    fflush(stdout);
    return 0;
}
