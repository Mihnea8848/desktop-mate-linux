/*
 * Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
 * SPDX-License-Identifier: MIT
 *
 * fgprobe.exe [fg]: writes Wine's foreground window, mouse button state and
 * cursor position to /dev/shm/ghost-bridge/fg.txt. "fg" makes the game
 * foreground, "desk" gives the foreground to Wine's desktop window, "steal"
 * makes a window of this process foreground and then exits.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
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
    if (argc > 1 && !strcmp(argv[1], "fg") && game) {
        BOOL ok = SetForegroundWindow(game);
        printf("SetForegroundWindow=%d fg=%p\n", ok, (void *)GetForegroundWindow());
    }
    if (argc > 1 && !strcmp(argv[1], "desk")) {
        BOOL ok = SetForegroundWindow(GetDesktopWindow());
        printf("SetForegroundWindow(desktop)=%d fg=%p\n", ok, (void *)GetForegroundWindow());
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
