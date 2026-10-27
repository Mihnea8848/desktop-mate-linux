/*
 * Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
 * SPDX-License-Identifier: MIT
 *
 * wbtest.exe move|min "<title>": moves or minimizes a ghost.exe mirror from
 * another process, as the game does. Result in /dev/shm/ghost-bridge/wbtest.txt.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    freopen("Z:\\dev\\shm\\ghost-bridge\\wbtest.txt", "w", stdout);
    if (argc < 3) {
        puts("usage: wbtest.exe move|min \"<window title>\"");
        return 1;
    }
    HWND h = FindWindowA("KWinGhost", argv[2]);
    RECT r = {0};
    if (h) GetWindowRect(h, &r);
    printf("hwnd=%p rect=%ld,%ld %ldx%ld\n", (void *)h, r.left, r.top, r.right - r.left, r.bottom - r.top);
    if (!h) return 1;

    if (!strcmp(argv[1], "move")) {
        BOOL ok = SetWindowPos(h, NULL, r.left - 250, r.top - 120, 0, 0,
                               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        GetWindowRect(h, &r);
        printf("SetWindowPos=%d now %ld,%ld\n", ok, r.left, r.top);
    } else if (!strcmp(argv[1], "min")) {
        printf("ShowWindow(SW_MINIMIZE)=%d\n", ShowWindow(h, SW_MINIMIZE));
    }
    fflush(stdout);
    return 0;
}
