/*
 * Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ghost-bridged: session bus relay between the KWin script and ghost.exe.
 *
 *   Update(s)  KWin window snapshot, written atomically to windows.tsv
 *   Poll() s   long poll; returns queued commands from the cmd/ files, or "" after 20 s
 *   Cursor(ii) KWin pointer position, written atomically to cursor.txt
 *   Diag(s)    writes s to diag.txt (debug scripts)
 */
#define _GNU_SOURCE
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DIR_PATH     "/dev/shm/ghost-bridge"
#define CMD_DIR      DIR_PATH "/cmd"
#define OUT_PATH     DIR_PATH "/windows.tsv"
#define TMP_PATH     DIR_PATH "/windows.tsv.tmp"
#define CURSOR_PATH  DIR_PATH "/cursor.txt"
#define POLL_TIMEOUT (20ULL * 1000000ULL)

static sd_event        *g_event;
static sd_bus_message  *g_pending;
static sd_event_source *g_poll_timer;
static char            *g_queue;
static size_t           g_queue_len;

static void queue_append(const char *s, size_t n)
{
    if (!n) return;
    char *q = realloc(g_queue, g_queue_len + n + 2);
    if (!q) return;
    g_queue = q;
    memcpy(g_queue + g_queue_len, s, n);
    g_queue_len += n;
    if (g_queue[g_queue_len - 1] != '\n') g_queue[g_queue_len++] = '\n';
    g_queue[g_queue_len] = 0;
}

static void reply_pending(int even_if_empty)
{
    if (!g_pending || (!g_queue_len && !even_if_empty)) return;
    sd_bus_reply_method_return(g_pending, "s", g_queue_len ? g_queue : "");
    sd_bus_message_unref(g_pending);
    g_pending = NULL;
    free(g_queue);
    g_queue = NULL;
    g_queue_len = 0;
    sd_event_source_set_enabled(g_poll_timer, SD_EVENT_OFF);
}

static int name_cmp(const struct dirent **a, const struct dirent **b)
{
    return strverscmp((*a)->d_name, (*b)->d_name);
}

static int is_cmd(const struct dirent *d)
{
    size_t n = strlen(d->d_name);
    return n > 4 && !strcmp(d->d_name + n - 4, ".cmd");
}

static void drain_cmd_dir(void)
{
    struct dirent **list;
    int n = scandir(CMD_DIR, &list, is_cmd, name_cmp);
    if (n < 0) return;
    for (int i = 0; i < n; ++i) {
        char path[512], buf[4096];
        snprintf(path, sizeof path, "%s/%s", CMD_DIR, list[i]->d_name);
        FILE *f = fopen(path, "r");
        if (f) {
            queue_append(buf, fread(buf, 1, sizeof buf - 1, f));
            fclose(f);
        }
        unlink(path);
        free(list[i]);
    }
    free(list);
    reply_pending(0);
}

static int on_inotify(sd_event_source *s, const struct inotify_event *ev, void *ud)
{
    (void)s; (void)ev; (void)ud;
    drain_cmd_dir();
    return 0;
}

static int on_poll_timeout(sd_event_source *s, uint64_t usec, void *ud)
{
    (void)s; (void)usec; (void)ud;
    reply_pending(1);
    return 0;
}

static int write_file(const char *path, const char *s)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(s, f);
    fputc('\n', f);
    return fclose(f);
}

static int method_update(sd_bus_message *m, void *ud, sd_bus_error *err)
{
    (void)ud; (void)err;
    const char *s;
    int r = sd_bus_message_read(m, "s", &s);
    if (r < 0) return r;
    if (write_file(TMP_PATH, s) == 0) rename(TMP_PATH, OUT_PATH);
    return sd_bus_reply_method_return(m, "");
}

static int method_cursor(sd_bus_message *m, void *ud, sd_bus_error *err)
{
    (void)ud; (void)err;
    int32_t x, y;
    int r = sd_bus_message_read(m, "ii", &x, &y);
    if (r < 0) return r;
    char s[32];
    snprintf(s, sizeof s, "%d %d", x, y);
    if (write_file(CURSOR_PATH ".tmp", s) == 0) rename(CURSOR_PATH ".tmp", CURSOR_PATH);
    return sd_bus_reply_method_return(m, "");
}

static int method_diag(sd_bus_message *m, void *ud, sd_bus_error *err)
{
    (void)ud; (void)err;
    const char *s;
    int r = sd_bus_message_read(m, "s", &s);
    if (r < 0) return r;
    write_file(DIR_PATH "/diag.txt", s);
    return sd_bus_reply_method_return(m, "");
}

/* one poller at a time: a new Poll() releases the previous one with "" */
static int method_poll(sd_bus_message *m, void *ud, sd_bus_error *err)
{
    (void)ud; (void)err;
    if (g_pending) {
        sd_bus_reply_method_return(g_pending, "s", "");
        sd_bus_message_unref(g_pending);
    }
    g_pending = sd_bus_message_ref(m);
    drain_cmd_dir();
    if (g_pending) {
        uint64_t now;
        sd_event_now(g_event, CLOCK_MONOTONIC, &now);
        sd_event_source_set_time(g_poll_timer, now + POLL_TIMEOUT);
        sd_event_source_set_enabled(g_poll_timer, SD_EVENT_ONESHOT);
    }
    return 1;
}

static const sd_bus_vtable vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Update", "s", "",  method_update, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Cursor", "ii", "", method_cursor, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Diag",   "s", "",  method_diag,   SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Poll",   "",  "s", method_poll,   SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/* a newer instance took over the name: this one is stale */
static int on_name_lost(sd_bus_message *m, void *ud, sd_bus_error *err)
{
    (void)m; (void)ud; (void)err;
    fprintf(stderr, "ghost-bridged: replaced by a newer instance, exiting\n");
    sd_event_exit(g_event, 0);
    return 0;
}

int main(void)
{
    sd_bus *bus = NULL;

    mkdir(DIR_PATH, 0755);
    mkdir(CMD_DIR, 0755);

    if (sd_event_default(&g_event) < 0 || sd_bus_open_user(&bus) < 0) return 1;
    if (sd_bus_add_object_vtable(bus, NULL, "/", "org.ghostbridge", vtable, NULL) < 0) return 1;
    if (sd_bus_match_signal(bus, NULL, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                            "org.freedesktop.DBus", "NameLost", on_name_lost, NULL) < 0) return 1;
    if (sd_bus_request_name(bus, "org.ghostbridge",
                            SD_BUS_NAME_ALLOW_REPLACEMENT | SD_BUS_NAME_REPLACE_EXISTING) < 0) {
        fprintf(stderr, "ghost-bridged: cannot own org.ghostbridge\n");
        return 1;
    }
    if (sd_bus_attach_event(bus, g_event, 0) < 0) return 1;
    if (sd_event_add_inotify(g_event, NULL, CMD_DIR, IN_MOVED_TO | IN_CLOSE_WRITE,
                             on_inotify, NULL) < 0) return 1;
    if (sd_event_add_time(g_event, &g_poll_timer, CLOCK_MONOTONIC, UINT64_MAX, 0,
                          on_poll_timeout, NULL) < 0) return 1;
    sd_event_source_set_enabled(g_poll_timer, SD_EVENT_OFF);
    drain_cmd_dir();
    return sd_event_loop(g_event) < 0;
}
