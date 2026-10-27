/*
 * Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * enumtest.exe: writes every top-level Win32 window to /dev/shm/ghost-bridge/enum.txt.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static BOOL CALLBACK print_window(HWND h, LPARAM lp)
{
    (void)lp;
    char cls[128] = "", title[256] = "";
    RECT r = {0};
    GetClassNameA(h, cls, sizeof cls);
    GetWindowTextA(h, title, sizeof title);
    GetWindowRect(h, &r);
    printf("%p vis=%d icon=%d style=%08lx ex=%08lx rect=%ld,%ld %ldx%ld cls='%s' title='%s'\n",
           (void *)h, IsWindowVisible(h), IsIconic(h),
           GetWindowLongA(h, GWL_STYLE), GetWindowLongA(h, GWL_EXSTYLE),
           r.left, r.top, r.right - r.left, r.bottom - r.top, cls, title);
    return TRUE;
}

int main(void)
{
    freopen("Z:\\dev\\shm\\ghost-bridge\\enum.txt", "w", stdout);
    EnumWindows(print_window, 0);
    fflush(stdout);
    return 0;
}
