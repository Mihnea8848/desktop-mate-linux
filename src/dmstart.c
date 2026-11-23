/*
 * Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * dmstart.exe GAME [ARGS]: starts the game and loads dmhook.dll into it right
 * away. launch.sh puts it in front of the game in Steam's command line.
 *
 * ghost.exe also loads dmhook.dll, but it starts seconds after the game. On a
 * fast machine the game has picked its window by then, and the window fixes
 * in dmhook.dll come too late (issue #34). Unity needs well over a second
 * before any game code runs, so loading the hook here wins that race.
 *
 * The exit code is the game's.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

#define LOG_PATH "Z:\\dev\\shm\\ghost-bridge\\launch.log"

static void logf_(const char *fmt, ...)
{
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("dmstart: ", f);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    va_end(ap);
    fclose(f);
}

/* the rest of a command line after its first argument */
static const WCHAR *after_first(const WCHAR *s)
{
    while (*s == L' ' || *s == L'\t') s++;
    if (*s == L'"') {
        for (s++; *s && *s != L'"'; s++)
            ;
        if (*s) s++;
    } else {
        while (*s && *s != L' ' && *s != L'\t') s++;
    }
    while (*s == L' ' || *s == L'\t') s++;
    return s;
}

/* Proton passes the game as a Unix path. Wine's own conversion picks the
 * drive Proton made for the Steam library (S:), as for a normal start; Z:
 * (the whole file system) is the fallback. */
static void to_windows_path(const WCHAR *in, WCHAR *out, size_t n)
{
    typedef WCHAR *(CDECL *dos_name_fn)(const char *);
    dos_name_fn dos_name = (dos_name_fn)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                       "wine_get_dos_file_name");
    char unix_path[MAX_PATH * 3];
    WCHAR *dos = NULL;
    if (in[0] == L'/' && dos_name &&
        WideCharToMultiByte(CP_UTF8, 0, in, -1, unix_path, sizeof unix_path, NULL, NULL))
        dos = dos_name(unix_path);
    if (dos) {
        _snwprintf(out, n, L"%ls", dos);
        HeapFree(GetProcessHeap(), 0, dos);
    } else if (in[0] == L'/') {
        _snwprintf(out, n, L"Z:%ls", in);
    } else {
        _snwprintf(out, n, L"%ls", in);
    }
    out[n - 1] = 0;
    for (WCHAR *p = out; *p; p++)
        if (*p == L'/') *p = L'\\';
}

static BOOL load_hook(HANDLE proc)
{
    WCHAR path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    WCHAR *slash = wcsrchr(path, L'\\');
    if (!n || !slash || slash - path + 12 >= MAX_PATH) return FALSE;
    wcscpy(slash + 1, L"dmhook.dll");
    SIZE_T size = (wcslen(path) + 1) * sizeof(WCHAR);
    void *mem = VirtualAllocEx(proc, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    void *load = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW");
    if (!mem || !load || !WriteProcessMemory(proc, mem, path, size, NULL)) return FALSE;
    HANDLE t = CreateRemoteThread(proc, NULL, 0, (LPTHREAD_START_ROUTINE)load, mem, 0, NULL);
    if (!t) return FALSE;
    WaitForSingleObject(t, 10000);
    DWORD module = 0;
    GetExitCodeThread(t, &module);
    CloseHandle(t);
    return module != 0;
}

int wmain(int argc, WCHAR **argv)
{
    if (argc < 2) {
        logf_("usage: dmstart.exe GAME [ARGS]");
        return 1;
    }
    WCHAR game[MAX_PATH], dir[MAX_PATH];
    to_windows_path(argv[1], game, MAX_PATH);
    wcscpy(dir, game);
    WCHAR *slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;

    /* the game's own command line: its path, then the arguments as given */
    const WCHAR *args = after_first(after_first(GetCommandLineW()));
    size_t len = wcslen(game) + wcslen(args) + 4;
    WCHAR *cmd = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (!cmd) return 1;
    _snwprintf(cmd, len, L"\"%ls\"%ls%ls", game, *args ? L" " : L"", args);
    cmd[len - 1] = 0;

    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(game, cmd, NULL, NULL, FALSE, 0, NULL, slash ? dir : NULL, &si, &pi)) {
        logf_("cannot start %ls (%lu)", game, GetLastError());
        return 1;
    }

    DWORD start = GetTickCount();
    BOOL ok = FALSE;
    /* the first tries can fail while the new process is still starting up */
    for (int i = 0; i < 100 && !(ok = load_hook(pi.hProcess)); i++)
        if (WaitForSingleObject(pi.hProcess, 20) != WAIT_TIMEOUT) break;
    logf_("dmhook.dll %s, %lu ms after the game started", ok ? "loaded" : "not loaded",
          GetTickCount() - start);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}
