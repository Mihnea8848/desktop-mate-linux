/*
 * Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * dmhook.dll: loaded into DesktopMate.exe by ghost.exe. Adds a "Linux" section
 * to the game's settings page and saves it to game.conf for launch.sh, and a
 * per-character brightness slider to the character settings.
 *
 * The game renames its il2cpp API exports and crashes callers outside
 * GameAssembly.dll and UnityPlayer.dll. il2cpp-map.txt (scripts/il2cpp-map.py)
 * gives the real names, and the caller check is widened to accept us. The game's
 * own classes are renamed too, so only Unity classes and the UI element names
 * from the game's layout files are used.
 *
 * Debugging: write an element name to /dev/shm/ghost-bridge/dmhook.dump and the
 * subtree is written to dmhook.log.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUN_DIR   "Z:\\dev\\shm\\ghost-bridge\\"
#define MAP_PATH  RUN_DIR "il2cpp-map.txt"
#define LOG_PATH  RUN_DIR "dmhook.log"
#define DUMP_PATH RUN_DIR "dmhook.dump"
#define CONF_DIR  RUN_DIR "config-dir"
#define TICK_MS   500
#define WM_DMHOOK (WM_APP + 0x444d)
#define DRAG_POLL 10  /* ms between mouse button checks */

static FILE *g_log;
static HMODULE g_ga;

static void logf_(const char *fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(g_log, "%lu ", (unsigned long)GetTickCount());
    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    fflush(g_log);
    va_end(ap);
}

/* renamed export -> il2cpp name */

static struct { char renamed[64], real[64]; } g_map[300];
static int g_nmap;

static int load_map(void)
{
    FILE *f = fopen(MAP_PATH, "r");
    if (!f) return 0;
    while (g_nmap < 300 && fscanf(f, "%63s %63s", g_map[g_nmap].renamed, g_map[g_nmap].real) == 2) g_nmap++;
    fclose(f);
    return g_nmap;
}

static void *api(const char *real)
{
    for (int i = 0; i < g_nmap; ++i)
        if (!strcmp(g_map[i].real, real)) return (void *)GetProcAddress(g_ga, g_map[i].renamed);
    return NULL;
}

/* target of a rip-relative operand: displacement at p+off, instruction length len */
static uint8_t *rip_target(uint8_t *p, int off, int len)
{
    return p + len + *(int32_t *)(p + off);
}

/* A protected export starts with InitOnceBeginInitialize(&once, ...), which
 * fills the GameAssembly and UnityPlayer address ranges, then compares its
 * return address against them with cmp rax,[rip+x]. Mark the init done and
 * make the GameAssembly range cover everything. */
static int widen_caller_check(uint8_t *fn)
{
    void *begin = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "InitOnceBeginInitialize");
    uint64_t *once = NULL, *ranges[4];
    int nr = 0;
    for (uint8_t *p = fn; p < fn + 0x400 && nr < 4; ++p) {
        /* lea r64,[rip+x] followed by call [rip+iat] with *iat == InitOnceBeginInitialize */
        if (!once && (p[0] == 0x48 || p[0] == 0x4c) && p[1] == 0x8d && (p[2] & 0xc7) == 0x05) {
            for (uint8_t *q = p + 7; q < p + 24; ++q)
                if (q[0] == 0xff && q[1] == 0x15 && *(void **)rip_target(q, 2, 6) == begin) {
                    once = (uint64_t *)rip_target(p, 3, 7);
                    break;
                }
        }
        /* cmp rax,[rip+x] */
        if (once && p[0] == 0x48 && p[1] == 0x3b && p[2] == 0x05)
            ranges[nr++] = (uint64_t *)rip_target(p, 3, 7);
    }
    if (!once || nr < 4) {
        logf_("caller check not found (%d compares)", nr);
        return 0;
    }
    if ((*once & 3) != 2) *once = 2;
    *ranges[0] = 0;
    *ranges[1] = UINT64_MAX;
    return 1;
}

/* il2cpp API */

static void *(*il_domain_get)(void);
static void **(*il_domain_get_assemblies)(void *, size_t *);
static void *(*il_assembly_get_image)(void *);
static void *(*il_class_from_name)(void *, const char *, const char *);
static void *(*il_class_get_method_from_name)(void *, const char *, int);
static const char *(*il_class_get_name)(void *);
static void *(*il_class_get_type)(void *);
static void *(*il_type_get_object)(void *);
static void *(*il_object_get_class)(void *);
static void *(*il_object_unbox)(void *);
static void *(*il_object_new)(void *);
static void *(*il_runtime_invoke)(void *, void *, void **, void **);
static void *(*il_string_new)(const char *);
static uint16_t *(*il_string_chars)(void *);
static int32_t (*il_string_length)(void *);
static void *(*il_class_get_field_from_name)(void *, const char *);
static void (*il_field_get_value)(void *, void *, void *);
static uint32_t (*il_gchandle_new)(void *, int);
static void (*il_gchandle_free)(uint32_t);
static void *(*il_class_get_methods)(void *, void **);
static const char *(*il_method_get_name)(void *);
static uint32_t (*il_method_get_param_count)(void *);
static void *(*il_method_get_param)(void *, uint32_t);
static void *(*il_method_get_return_type)(void *);
static uint32_t (*il_method_get_flags)(void *, uint32_t *);
static char *(*il_type_get_name)(void *);
static void *(*il_class_get_parent)(void *);
static void *(*il_class_get_fields)(void *, void **);
static const char *(*il_field_get_name)(void *);
static void *(*il_field_get_type)(void *);
static void (*il_field_set_value)(void *, void *, void *);

static int resolve_api(void)
{
#define R(f) if (!(il_##f = api("il2cpp_" #f))) { logf_("missing il2cpp_" #f); return 0; }
    R(domain_get) R(domain_get_assemblies) R(assembly_get_image) R(class_from_name)
    R(class_get_method_from_name) R(class_get_name) R(class_get_type) R(type_get_object)
    R(object_get_class) R(object_unbox) R(object_new) R(runtime_invoke) R(string_new)
    R(string_chars) R(string_length) R(class_get_field_from_name) R(field_get_value)
    R(gchandle_new) R(gchandle_free) R(class_get_methods) R(method_get_name) R(method_get_param_count)
    R(method_get_param) R(method_get_return_type) R(method_get_flags) R(type_get_name) R(class_get_parent)
    R(class_get_fields) R(field_get_name) R(field_get_type) R(field_set_value)
#undef R
    return 1;
}

static void *find_class(const char *ns, const char *name)
{
    size_t n = 0;
    void **asms = il_domain_get_assemblies(il_domain_get(), &n);
    for (size_t i = 0; i < n; ++i) {
        void *k = il_class_from_name(il_assembly_get_image(asms[i]), ns, name);
        if (k) return k;
    }
    logf_("class %s.%s not found", ns, name);
    return NULL;
}

/* method of klass or a base class */
static void *method(void *klass, const char *name, int argc)
{
    void *m = klass ? il_class_get_method_from_name(klass, name, argc) : NULL;
    if (!m) logf_("method %s/%d not found", name, argc);
    return m;
}

static void *invoke(void *m, void *obj, void **args)
{
    void *exc = NULL;
    if (!m) return NULL;
    void *r = il_runtime_invoke(m, obj, args, &exc);
    if (exc) {
        logf_("managed exception");
        return NULL;
    }
    return r;
}

static int unbox_int(void *boxed)
{
    return boxed ? *(int32_t *)il_object_unbox(boxed) : -1;
}

static void to_ascii(void *str, char *out, int size)
{
    int n = str ? il_string_length(str) : 0, o = 0;
    uint16_t *c = str ? il_string_chars(str) : NULL;
    for (int i = 0; i < n && o < size - 1; ++i) out[o++] = c[i] < 128 ? (char)c[i] : '?';
    out[o] = 0;
}

/* reference-type field of obj, looked up on its class and base classes */
static void *field_obj(void *obj, const char *name)
{
    void *f = il_class_get_field_from_name(il_object_get_class(obj), name), *v = NULL;
    if (f) il_field_get_value(obj, f, &v);
    return v;
}

/* UI Toolkit */

static struct {
    void *element, *document, *label, *toggle, *slider, *application, *prefs, *light, *object;
    void *find_objects, *root, *child_count, *element_at, *get_name, *set_name;
    void *add, *add_class, *get_parent, *get_style;
} ui;

static int resolve_ui(void)
{
    void *object = find_class("UnityEngine", "Object");
    ui.document = find_class("UnityEngine.UIElements", "UIDocument");
    ui.element = find_class("UnityEngine.UIElements", "VisualElement");
    ui.label = find_class("UnityEngine.UIElements", "Label");
    ui.toggle = find_class("UnityEngine.UIElements", "Toggle");
    ui.slider = find_class("UnityEngine.UIElements", "Slider");
    ui.application = find_class("UnityEngine", "Application");
    ui.prefs = find_class("UnityEngine", "PlayerPrefs");
    ui.light = find_class("UnityEngine", "Light");
    ui.object = object;
    if (!object || !ui.document || !ui.element || !ui.label || !ui.toggle || !ui.slider || !ui.application ||
        !ui.prefs || !ui.light) return 0;
    ui.find_objects = method(object, "FindObjectsByType", 2);
    ui.root = method(ui.document, "get_rootVisualElement", 0);
    ui.child_count = method(ui.element, "get_childCount", 0);
    ui.element_at = method(ui.element, "ElementAt", 1);
    ui.get_name = method(ui.element, "get_name", 0);
    ui.set_name = method(ui.element, "set_name", 1);
    ui.add = method(ui.element, "Add", 1);
    ui.add_class = method(ui.element, "AddToClassList", 1);
    ui.get_parent = method(ui.element, "get_parent", 0);
    ui.get_style = method(ui.element, "get_style", 0);
    return ui.find_objects && ui.root && ui.child_count && ui.element_at && ui.get_name &&
           ui.set_name && ui.add && ui.add_class && ui.get_parent && ui.get_style;
}

static void *element_at(void *e, int i)
{
    void *args[] = { &i };
    return invoke(ui.element_at, e, args);
}

static void *find_element(void *e, const char *name, int depth)
{
    char n[128];
    to_ascii(invoke(ui.get_name, e, NULL), n, sizeof n);
    if (!strcmp(n, name)) return e;
    if (depth > 40) return NULL;
    for (int i = 0, c = unbox_int(invoke(ui.child_count, e, NULL)); i < c; ++i) {
        void *f = find_element(element_at(e, i), name, depth + 1);
        if (f) return f;
    }
    return NULL;
}

static void *find_in_documents(const char *name)
{
    int none = 0;
    void *args[] = { il_type_get_object(il_class_get_type(ui.document)), &none };
    void *docs = invoke(ui.find_objects, NULL, args);
    /* Il2CppArray: object header, bounds, length at 0x18, elements from 0x20 */
    uintptr_t len = docs ? *(uintptr_t *)((char *)docs + 0x18) : 0;
    for (uintptr_t i = 0; i < len; ++i) {
        void *root = invoke(ui.root, ((void **)((char *)docs + 0x20))[i], NULL);
        void *e = root ? find_element(root, name, 0) : NULL;
        if (e) return e;
    }
    return NULL;
}

static int g_dump_depth = 12;
static void dump(void *e, int depth)
{
    char name[128], cls[512] = "", text[160] = "";
    void *k = il_object_get_class(e), *list = field_obj(e, "m_ClassList");
    to_ascii(invoke(ui.get_name, e, NULL), name, sizeof name);
    if (list) {
        void *lk = il_object_get_class(list);
        int n = unbox_int(invoke(method(lk, "get_Count", 0), list, NULL)), o = 0;
        for (int i = 0; i < n && o < (int)sizeof cls - 2; ++i) {
            void *args[] = { &i };
            char c[96];
            to_ascii(invoke(method(lk, "get_Item", 1), list, args), c, sizeof c);
            o += snprintf(cls + o, sizeof cls - o, "%s%s", o ? " " : "", c);
        }
    }
    if (il_class_get_field_from_name(k, "m_Text")) to_ascii(field_obj(e, "m_Text"), text, sizeof text);
    logf_("%*s%s [%s] .%s \"%s\"", depth * 2, "", name, il_class_get_name(k), cls, text);
    if (depth < g_dump_depth)
        for (int i = 0, c = unbox_int(invoke(ui.child_count, e, NULL)); i < c; ++i) dump(element_at(e, i), depth + 1);
}

static void dump_request(void)
{
    char name[128];
    FILE *f = fopen(DUMP_PATH, "r");
    if (!f) return;
    /* "NAME [DEPTH]"; NAME * dumps every document */
    int ok = fscanf(f, "%127s", name) == 1;
    if (fscanf(f, "%d", &g_dump_depth) != 1) g_dump_depth = 12;
    fclose(f);
    DeleteFileA(DUMP_PATH);
    if (ok && !strcmp(name, "*")) {
        int none = 0;
        void *args[] = { il_type_get_object(il_class_get_type(ui.document)), &none };
        void *docs = invoke(ui.find_objects, NULL, args);
        uintptr_t len = docs ? *(uintptr_t *)((char *)docs + 0x18) : 0;
        for (uintptr_t i = 0; i < len; ++i) {
            void *d = ((void **)((char *)docs + 0x20))[i];
            char dn[96];
            to_ascii(invoke(method(ui.object, "get_name", 0), d, NULL), dn, sizeof dn);
            logf_("document %s", dn);
            void *root = invoke(ui.root, d, NULL);
            if (root) dump(root, 1);
        }
        return;
    }
    void *e = ok ? find_in_documents(name) : NULL;
    if (e) dump(e, 0);
    else logf_("dump: %s not found", name);
}

static void *new_element(void *klass, const char *text, const char *classes, const char *name)
{
    void *e = il_object_new(klass);
    void *args[] = { text ? il_string_new(text) : NULL };
    invoke(method(klass, ".ctor", text ? 1 : 0), e, args);
    char buf[256];
    snprintf(buf, sizeof buf, "%s", classes ? classes : "");
    for (char *c = strtok(buf, " "); c; c = strtok(NULL, " ")) {
        void *a[] = { il_string_new(c) };
        invoke(ui.add_class, e, a);
    }
    if (name) {
        void *a[] = { il_string_new(name) };
        invoke(ui.set_name, e, a);
    }
    return e;
}

static void add_child(void *parent, void *child)
{
    void *a[] = { child };
    invoke(ui.add, parent, a);
}

/* inline style; IStyle is implemented explicitly, so try both names */
static void style_set(void *e, const char *prop, void *value)
{
    void *st = invoke(ui.get_style, e, NULL);
    if (!st) return;
    char name[128];
    snprintf(name, sizeof name, "set_%s", prop);
    void *m = il_class_get_method_from_name(il_object_get_class(st), name, 1);
    if (!m) {
        snprintf(name, sizeof name, "UnityEngine.UIElements.IStyle.set_%s", prop);
        m = method(il_object_get_class(st), name, 1);
    }
    void *a[] = { value };
    invoke(m, st, a);
}

/* StyleLength, StyleFloat and StyleColor, keyword Undefined */
static void style_px(void *e, const char *prop, float px)
{
    struct { float v; int32_t unit, keyword; } l = { px, 0, 0 };
    style_set(e, prop, &l);
}

static void style_float(void *e, const char *prop, float v)
{
    struct { float v; int32_t keyword; } f = { v, 0 };
    style_set(e, prop, &f);
}

static void style_color(void *e, const char *prop, float r, float g, float b, float a)
{
    struct { float r, g, b, a; int32_t keyword; } c = { r, g, b, a, 0 };
    style_set(e, prop, &c);
}

/* StyleEnum: DisplayStyle, Visibility, TextAnchor, FontStyle */
static void style_enum(void *e, const char *prop, int v)
{
    struct { int32_t v, keyword; } x = { v, 0 };
    style_set(e, prop, &x);
}

static void label_style(void *label, float size, int bold, float alpha)
{
    style_color(label, "color", 1, 1, 1, alpha);
    style_px(label, "fontSize", size);
    style_enum(label, "unityFontStyleAndWeight", bold);
}

/* The game's checkboxes are per-toggle images with the caption baked in. Draw
 * a box that matches them and put a real label next to it. */
static void *add_toggle(void *parent, const char *name, const char *caption, int on)
{
    static const char *sides[] = { "Top", "Right", "Bottom", "Left" };
    static const char *corners[] = { "TopLeft", "TopRight", "BottomRight", "BottomLeft" };
    void *t = new_element(ui.toggle, NULL, "unity-base-field--no-label", name);
    void *box = find_element(t, "unity-checkmark", 0);
    if (box) {
        char n[64];
        style_px(box, "width", 22);
        style_px(box, "height", 22);
        for (int i = 0; i < 4; ++i) {
            snprintf(n, sizeof n, "border%sWidth", sides[i]);
            style_float(box, n, 2.5f);
            snprintf(n, sizeof n, "border%sColor", sides[i]);
            style_color(box, n, 1, 1, 1, 1);
            snprintf(n, sizeof n, "border%sRadius", corners[i]);
            style_px(box, n, 4);
        }
        style_color(box, "backgroundColor", 0, 0, 0, 0);
        style_color(box, "unityBackgroundImageTintColor", 1, 1, 1, 1);
    }
    void *label = new_element(ui.label, caption, "setting-version-label", NULL);
    style_px(label, "marginLeft", 10);
    add_child(t, label);
    uint8_t v = (uint8_t)on;
    void *a[] = { &v };
    invoke(method(ui.toggle, "SetValueWithoutNotify", 1), t, a);
    add_child(parent, t);
    il_gchandle_new(t, 0); /* we keep pointers to it between ticks */
    return t;
}

/* settings, as shell variables in game.conf */

enum { FPS30, BRIDGE, DIRECT, NSETTINGS };
static const char *const g_captions[NSETTINGS] = { "Battery saver (30 FPS)", "Sit on windows", "Smooth animation" };
static int g_conf[NSETTINGS] = { 0, 1, 1 }, g_saved[NSETTINGS];
static void *g_toggles[NSETTINGS], *g_section;

/* Wine hides HOME from Windows programs; launch.sh writes the folder here */
static void conf_path(char *out, int size)
{
    char dir[400] = "";
    FILE *f = fopen(CONF_DIR, "r");
    if (f) {
        if (!fgets(dir, sizeof dir, f)) dir[0] = 0;
        fclose(f);
    }
    dir[strcspn(dir, "\r\n")] = 0;
    snprintf(out, size, "Z:%s/game.conf", dir);
}

static void load_conf(void)
{
    char path[512], line[128];
    conf_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "DM_FPS=30", 9)) g_conf[FPS30] = 1;
            if (!strncmp(line, "DM_NO_BRIDGE=1", 14)) g_conf[BRIDGE] = 0;
            if (!strncmp(line, "DM_DIRECT=0", 11)) g_conf[DIRECT] = 0;
        }
        fclose(f);
    }
    memcpy(g_saved, g_conf, sizeof g_conf);
}

static void save_conf(void)
{
    char path[512], dir[512];
    conf_path(path, sizeof path);
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        CreateDirectoryA(dir, NULL);
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        logf_("cannot write %s", path);
        return;
    }
    fprintf(f, "# Written by the Linux section of Desktop Mate's settings page.\n");
    if (g_conf[FPS30]) fprintf(f, "DM_FPS=30\n");
    if (!g_conf[BRIDGE]) fprintf(f, "DM_NO_BRIDGE=1\n");
    if (!g_conf[DIRECT]) fprintf(f, "DM_DIRECT=0\n");
    fclose(f);
    memcpy(g_saved, g_conf, sizeof g_conf);
    logf_("saved: fps30 %d bridge %d direct %d", g_conf[FPS30], g_conf[BRIDGE], g_conf[DIRECT]);
}

static int g_dragging, g_drag_fps;

/* 60 FPS, 30 with the battery saver, the screen's refresh rate while the
   character is dragged so she keeps up with the pointer */
static void apply_fps(void)
{
    int fps = g_conf[FPS30] ? 30 : g_dragging ? g_drag_fps : 60;
    void *a[] = { &fps };
    invoke(method(ui.application, "set_targetFrameRate", 1), NULL, a);
}

static void build_section(void *common)
{
    void *box = new_element(ui.element, NULL, "setting-section", "linux-section");
    add_child(box, new_element(ui.label, "Linux Patch Settings", "setting-version-label", NULL));
    for (int i = 0; i < NSETTINGS; ++i) {
        char name[32];
        snprintf(name, sizeof name, "linux-toggle-%d", i);
        g_toggles[i] = add_toggle(box, name, g_captions[i], g_conf[i]);
    }
    add_child(box, new_element(ui.label, "The last two apply on the next start.", "setting-support-id-label", NULL));
    add_child(box, new_element(ui.element, NULL, "setting-separator", NULL));
    add_child(common, box);
    il_gchandle_new(box, 0);
    g_section = box;
    logf_("settings section added");
}

/* Brightness, per character: the game lights the character with one
 * directional light, scaled here. Saved as "<character> <percent>" lines. */

#define BRIGHT_MIN 25
#define BRIGHT_MAX 150
static char g_character[128];
static int g_bright = 100;
static void *g_bright_slider, *g_bright_caption, *g_bright_section;
static struct { void *light; float base; } g_lights[8];
static int g_nlights;

static void bright_path(char *out, int size)
{
    conf_path(out, size);
    char *slash = strrchr(out, '/');
    if (slash) snprintf(slash + 1, size - (slash + 1 - out), "brightness.conf");
}

static int load_bright(const char *character)
{
    char path[512], id[128];
    int pct, found = 100;
    bright_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return found;
    while (fscanf(f, "%127s %d", id, &pct) == 2)
        if (!strcmp(id, character)) found = pct;
    fclose(f);
    return found < BRIGHT_MIN || found > BRIGHT_MAX ? 100 : found;
}

static void save_bright(const char *character, int value)
{
    char path[512], tmp[520], id[128], lines[64][160];
    int pct, n = 0;
    bright_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (f) {
        while (n < 63 && fscanf(f, "%127s %d", id, &pct) == 2)
            if (strcmp(id, character)) snprintf(lines[n++], sizeof lines[0], "%s %d", id, pct);
        fclose(f);
    }
    snprintf(lines[n++], sizeof lines[0], "%s %d", character, value);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!(f = fopen(tmp, "w"))) {
        logf_("cannot write %s", tmp);
        return;
    }
    for (int i = 0; i < n; ++i) fprintf(f, "%s\n", lines[i]);
    fclose(f);
    MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

/* the character in the first slot, for example "Release/miku" */
static void current_character(char *out, int size)
{
    void *a[] = { il_string_new("CharacterSlot/0") };
    to_ascii(invoke(method(ui.prefs, "GetString", 1), NULL, a), out, size);
}

static void apply_bright(int find)
{
    if (find) {
        int none = 0;
        void *args[] = { il_type_get_object(il_class_get_type(ui.light)), &none };
        void *arr = invoke(ui.find_objects, NULL, args);
        uintptr_t n = arr ? *(uintptr_t *)((char *)arr + 0x18) : 0;
        for (uintptr_t i = 0; i < n; ++i) {
            void *l = ((void **)((char *)arr + 0x20))[i];
            int known = 0;
            for (int j = 0; j < g_nlights; ++j) known |= g_lights[j].light == l;
            if (known || g_nlights == 8) continue;
            void *r = invoke(method(ui.light, "get_intensity", 0), l, NULL);
            if (!r) continue;
            g_lights[g_nlights].light = l;
            g_lights[g_nlights].base = *(float *)il_object_unbox(r);
            il_gchandle_new(l, 0);
            g_nlights++;
        }
    }
    for (int j = 0; j < g_nlights; ++j) {
        float v = g_lights[j].base * g_bright / 100.0f;
        void *a[] = { &v };
        invoke(method(ui.light, "set_intensity", 1), g_lights[j].light, a);
    }
}

static void show_bright(void)
{
    char text[64];
    snprintf(text, sizeof text, "Brightness %d%%", g_bright);
    void *a[] = { il_string_new(text) };
    invoke(method(ui.label, "set_text", 1), g_bright_caption, a);
    float v = (float)g_bright;
    void *b[] = { &v };
    invoke(method(ui.slider, "SetValueWithoutNotify", 1), g_bright_slider, b);
}

/* the Linux Patch Settings section of Character Settings */
static void build_bright(void *individual)
{
    void *box = new_element(ui.element, NULL, "setting-section", "linux-character-section");
    add_child(box, new_element(ui.label, "Linux Patch Settings", "setting-version-label", NULL));
    /* one caption with the value: the game's slider-row places its value label
       over a centred caption */
    g_bright_caption = new_element(ui.label, "Brightness", "setting-version-label", NULL);
    add_child(box, g_bright_caption);
    g_bright_slider = new_element(ui.slider, NULL, "unity-base-field--no-label menu-slider", "linux-brightness");
    float lo = BRIGHT_MIN, hi = BRIGHT_MAX;
    void *a[] = { &lo }, *b[] = { &hi };
    invoke(method(ui.slider, "set_lowValue", 1), g_bright_slider, a);
    invoke(method(ui.slider, "set_highValue", 1), g_bright_slider, b);
    add_child(box, g_bright_slider);
    add_child(box, new_element(ui.element, NULL, "setting-separator", NULL));
    add_child(individual, box);
    il_gchandle_new(box, 0);
    il_gchandle_new(g_bright_slider, 0);
    il_gchandle_new(g_bright_caption, 0);
    g_bright_section = box;
    show_bright();
    logf_("brightness slider added");
}

static void tick_bright(void)
{
    static int ticks;
    char character[128];
    current_character(character, sizeof character);
    if (character[0] && strcmp(character, g_character)) {
        snprintf(g_character, sizeof g_character, "%s", character);
        g_bright = load_bright(character);
        logf_("character %s, brightness %d%%", character, g_bright);
        if (g_bright_slider) show_bright();
        apply_bright(1);
    } else if (++ticks % 4 == 0) {
        apply_bright(1);  /* picks up a light the game created since */
    }
    if (!g_bright_section || !invoke(ui.get_parent, g_bright_section, NULL)) {
        void *individual = find_in_documents("individual-section");
        if (individual) build_bright(individual);
        return;
    }
    void *r = invoke(method(ui.slider, "get_value", 0), g_bright_slider, NULL);
    if (!r) return;
    int v = (int)(*(float *)il_object_unbox(r) + 0.5f);
    if (v != g_bright && g_character[0]) {
        g_bright = v;
        show_bright();
        apply_bright(0);
        save_bright(g_character, v);
    }
}

/* method by name and exact parameter types, for overloaded methods; walks
 * the base classes */
static void *method_typed(void *klass, const char *name, int argc, const char *const types[])
{
    for (void *k = klass; k; k = il_class_get_parent(k)) {
        void *iter = NULL, *m;
        while ((m = il_class_get_methods(k, &iter))) {
            if (strcmp(il_method_get_name(m), name) || (int)il_method_get_param_count(m) != argc) continue;
            int ok = 1;
            for (int i = 0; i < argc && ok; ++i) ok = !strcmp(il_type_get_name(il_method_get_param(m, i)), types[i]);
            if (ok) return m;
        }
    }
    logf_("method %s(%s...) not found", name, argc ? types[0] : "");
    return NULL;
}

/* all components of type cls under comp, including inactive ones */
static void *children_of_type(void *comp, void *cls)
{
    static const char *const sig[] = { "System.Type", "System.Boolean" };
    uint8_t yes = 1;
    void *a[] = { il_type_get_object(il_class_get_type(cls)), &yes };
    return invoke(method_typed(find_class("UnityEngine", "Component"), "GetComponentsInChildren", 2, sig), comp, a);
}

/* Inspection for the custom character work: write anything to
 * /dev/shm/ghost-bridge/dmhook.inspect. Lists the VRM runtime's classes and
 * methods and the structure of the characters in the scene. */
static void inspect_class(const char *ns, const char *name)
{
    size_t n = 0;
    void **asms = il_domain_get_assemblies(il_domain_get(), &n), *k = NULL;
    for (size_t i = 0; i < n && !k; ++i) k = il_class_from_name(il_assembly_get_image(asms[i]), ns, name);
    if (!k) {
        logf_("class %s.%s: missing", ns, name);
        return;
    }
    logf_("class %s.%s (base %s)", ns, name, il_class_get_parent(k) ? il_class_get_name(il_class_get_parent(k)) : "-");
    void *fiter = NULL, *f;
    while ((f = il_class_get_fields(k, &fiter)))
        logf_("  field %s %s", il_type_get_name(il_field_get_type(f)), il_field_get_name(f));
    void *iter = NULL, *m;
    while ((m = il_class_get_methods(k, &iter))) {
        char sig[600];
        int o = 0;
        uint32_t iflags = 0, flags = il_method_get_flags(m, &iflags);
        for (uint32_t i = 0, c = il_method_get_param_count(m); i < c && o < 500; ++i)
            o += snprintf(sig + o, sizeof sig - o, "%s%s", i ? ", " : "", il_type_get_name(il_method_get_param(m, i)));
        sig[o] = 0;
        logf_("  %s%s %s(%s)", flags & 0x10 ? "static " : "", il_type_get_name(il_method_get_return_type(m)),
              il_method_get_name(m), sig);
    }
}

static void inspect_characters(void)
{
    void *animator = find_class("UnityEngine", "Animator"), *component = find_class("UnityEngine", "Component");
    void *transform = find_class("UnityEngine", "Transform"), *smr = find_class("UnityEngine", "SkinnedMeshRenderer");
    void *go = find_class("UnityEngine", "GameObject");
    int none = 0;
    void *args[] = { il_type_get_object(il_class_get_type(animator)), &none };
    void *arr = invoke(ui.find_objects, NULL, args);
    uintptr_t n = arr ? *(uintptr_t *)((char *)arr + 0x18) : 0;
    logf_("animators: %lu (class %p, array %p)", (unsigned long)n, animator, arr);
    for (uintptr_t i = 0; i < n; ++i) {
        void *a = ((void **)((char *)arr + 0x20))[i];
        char path[512] = "", name[96];
        void *t = invoke(method(component, "get_transform", 0), a, NULL);
        for (int d = 0; t && d < 12; ++d) {
            to_ascii(invoke(method(ui.object, "get_name", 0), t, NULL), name, sizeof name);
            char tmp[512];
            snprintf(tmp, sizeof tmp, "/%s%s", name, path);
            snprintf(path, sizeof path, "%s", tmp);
            t = invoke(method(transform, "get_parent", 0), t, NULL);
        }
        void *ctl = invoke(method(animator, "get_runtimeAnimatorController", 0), a, NULL);
        char ctlname[96] = "-";
        if (ctl) to_ascii(invoke(method(ui.object, "get_name", 0), ctl, NULL), ctlname, sizeof ctlname);
        void *human = invoke(method(animator, "get_isHuman", 0), a, NULL);
        void *en = invoke(method(find_class("UnityEngine", "Behaviour"), "get_enabled", 0), a, NULL);
        void *g = invoke(method(component, "get_gameObject", 0), a, NULL);
        void *layer = g ? invoke(method(go, "get_layer", 0), g, NULL) : NULL;
        void *meshes = children_of_type(a, smr);
        logf_("  %s: controller %s, human %d, enabled %d, layer %d, skinned meshes %lu", path, ctlname,
              human ? *(uint8_t *)il_object_unbox(human) : -1, en ? *(uint8_t *)il_object_unbox(en) : -1,
              layer ? *(int32_t *)il_object_unbox(layer) : -1,
              meshes ? (unsigned long)*(uintptr_t *)((char *)meshes + 0x18) : 0UL);
    }
}

static void inspect_request(void)
{
    FILE *f = fopen(RUN_DIR "dmhook.inspect", "r");
    if (!f) return;
    /* lines "Namespace Class" pick the classes; an empty file lists the defaults */
    char ns[96], cn[96];
    int listed = 0;
    while (fscanf(f, "%95s %95s", ns, cn) == 2) {
        listed++;
        if (strcmp(ns, "go")) {
            inspect_class(ns, cn);
            continue;
        }
        /* "go NAME": the classes of the components on that GameObject */
        void *go_class = find_class("UnityEngine", "GameObject"), *component = find_class("UnityEngine", "Component");
        void *na[] = { il_string_new(cn) };
        void *go = invoke(method(go_class, "Find", 1), NULL, na);
        static const char *const tsig[] = { "System.Type" };
        void *ta[] = { il_type_get_object(il_class_get_type(component)) };
        void *cs = go ? invoke(method_typed(go_class, "GetComponents", 1, tsig), go, ta) : NULL;
        uintptr_t n = cs ? *(uintptr_t *)((char *)cs + 0x18) : 0;
        logf_("gameobject %s: %p, %lu components", cn, go, (unsigned long)n);
        for (uintptr_t i = 0; i < n; ++i) {
            void *k = il_object_get_class(((void **)((char *)cs + 0x20))[i]);
            logf_("component %s", il_class_get_name(k));
            void *iter = NULL, *m;
            while ((m = il_class_get_methods(k, &iter))) logf_("  %s/%u", il_method_get_name(m), il_method_get_param_count(m));
        }
    }
    fclose(f);
    DeleteFileA(RUN_DIR "dmhook.inspect");
    if (listed) return;
    static const char *classes[][2] = {
        { "UniVRM10", "Vrm10" }, { "UniVRM10", "Vrm10Data" }, { "UniVRM10", "Vrm10Importer" },
        { "UniVRM10", "Vrm10Instance" }, { "UniVRM10", "Vrm10Runtime" }, { "UniVRM10", "MigrationVrm" },
        { "UniVRM10", "UrpVrm10MaterialDescriptorGenerator" }, { "UniGLTF", "GlbFileParser" },
        { "UniGLTF", "GltfData" }, { "UniGLTF", "RuntimeGltfInstance" }, { "UniGLTF", "ImporterContextExtensions" },
        { "VRMShaders", "ImmediateCaller" }, { "VRMShaders", "RuntimeOnlyAwaitCaller" },
        { "UniHumanoid", "HumanPoseTransfer" }, { "UniVRM10", "ControlRigGenerationOption" },
        { "UniVRM10", "Vrm10RuntimeExpression" }, { "UniVRM10", "ExpressionKey" }, { "UniVRM10", "ExpressionPreset" },
    };
    for (size_t i = 0; i < sizeof classes / sizeof classes[0]; ++i) inspect_class(classes[i][0], classes[i][1]);
    inspect_characters();
}

/* Her face drives the VRM's expressions: each VRM preset follows the
 * strongest of her matching blendshapes (0..100 -> 0..1). */
static const struct { const char *preset; const char *shapes[6]; } g_face_map[] = {
    { "get_BlinkLeft", { "facial_bs.eye_close_geo_L", "facial_bs.eyeBlinkLeft" } },
    { "get_BlinkRight", { "facial_bs.eye_close_geo_R", "facial_bs.eyeBlinkRight" } },
    { "get_Aa", { "facial_bs.mouth_a_geo_C" } },
    { "get_Ih", { "facial_bs.mouth_i_geo_C" } },
    { "get_Ou", { "facial_bs.mouth_u_geo_C" } },
    { "get_Ee", { "facial_bs.mouth_e_geo_C" } },
    { "get_Oh", { "facial_bs.mouth_o_geo_C", "facial_bs.mouth_oh_geo_C" } },
    { "get_Happy", { "facial_bs.eye_wink_geo_L", "facial_bs.eye_wink_geo_R" } },
    { "get_Angry", { "facial_bs.eye_angry_geo_L", "facial_bs.eye_angry_geo_R", "facial_bs.brow_angry_geo_L",
                     "facial_bs.brow_angry_geo_R" } },
    { "get_Sad", { "facial_bs.eye_sad_geo_L", "facial_bs.eye_sad_geo_R", "facial_bs.brow_troubled_geo_L",
                   "facial_bs.brow_troubled_geo_R" } },
    { "get_Surprised", { "facial_bs.eye_surprise_geo_L", "facial_bs.eye_surprise_geo_R", "facial_bs.brow_surprise_geo_L",
                         "facial_bs.brow_surprise_geo_R", "facial_bs.mouth_surprise_geo_C" } },
    { "get_Relaxed", { "facial_bs.eye_downcast_geo_L", "facial_bs.eye_downcast_geo_R" } },
};
#define FACE_KEYS (int)(sizeof g_face_map / sizeof g_face_map[0])
static void *g_face_keys[FACE_KEYS];

/* A VRM worn by one of the characters out. Her skeleton keeps running the
 * game's animation, IK and colliders; her meshes are hidden and the VRM
 * follows her pose and face. */
#define MAX_WORN 6
struct worn {
    char character[128], file[260];
    void *her, *task, *instance, *go, *src, *face_mesh, *face_expression;
    int shapes[FACE_KEYS][6], nhidden, nhandles;
    int waiting; /* loaded, hidden, until her model exists */
    int leaving; /* she was dismissed; the VRM stays until she is gone */
    DWORD since;
    struct { void *renderer; uint8_t enabled; } hidden[64];
    uint32_t handles[256]; /* GC handles of everything above, freed with it */
};
static struct worn g_worn[MAX_WORN];
static int g_watching; /* worn VRMs waiting for her or just put on: tick fast */
static void bring_out(const char *id);
static int g_faces; /* worn VRMs with a face to drive */

static void *keep(struct worn *w, void *obj)
{
    if (obj && w->nhandles < 256) w->handles[w->nhandles++] = il_gchandle_new(obj, 0);
    return obj;
}

/* false once Unity destroyed the object behind obj */
static int alive(void *obj)
{
    if (!obj) return 0;
    void *a[] = { obj };
    void *r = invoke(method(ui.object, "op_Implicit", 1), NULL, a);
    return r && *(uint8_t *)il_object_unbox(r);
}

static void destroy(void *obj)
{
    if (!alive(obj)) return;
    void *a[] = { obj };
    invoke(method(ui.object, "Destroy", 1), NULL, a);
}

static void set_field(void *obj, const char *name, void *value)
{
    void *f = il_class_get_field_from_name(il_object_get_class(obj), name);
    if (f) il_field_set_value(obj, f, value);
    else logf_("vrm: no field %s", name);
}

/* the humanoid Animators of the characters out, under /MascotRoot/VRMRoot,
 * not counting our VRMs */
static int character_animators(void *out[], int max)
{
    /* asked several times per tick */
    static void *cache[MAX_WORN + 2];
    static int cached = -1;
    static DWORD at;
    if (cached >= 0 && GetTickCount() - at < 5) {
        int n = cached < max ? cached : max;
        memcpy(out, cache, sizeof cache[0] * n);
        return n;
    }
    void *animator = find_class("UnityEngine", "Animator"), *component = find_class("UnityEngine", "Component");
    void *transform = find_class("UnityEngine", "Transform");
    int none = 0, n = 0;
    void *args[] = { il_type_get_object(il_class_get_type(animator)), &none };
    void *arr = invoke(ui.find_objects, NULL, args);
    uintptr_t count = arr ? *(uintptr_t *)((char *)arr + 0x18) : 0;
    for (uintptr_t i = 0; i < count && n < max; ++i) {
        void *a = ((void **)((char *)arr + 0x20))[i];
        void *human = invoke(method(animator, "get_isHuman", 0), a, NULL);
        if (!human || !*(uint8_t *)il_object_unbox(human)) continue;
        void *t = invoke(method(component, "get_transform", 0), a, NULL);
        void *parent = t ? invoke(method(transform, "get_parent", 0), t, NULL) : NULL;
        char name[64] = "";
        if (parent) to_ascii(invoke(method(ui.object, "get_name", 0), parent, NULL), name, sizeof name);
        if (strcmp(name, "VRMRoot")) continue;
        void *go = invoke(method(component, "get_gameObject", 0), a, NULL);
        int ours = 0;
        for (int w = 0; w < MAX_WORN; ++w) ours |= g_worn[w].go == go;
        if (!ours) out[n++] = a;
    }
    if (max >= MAX_WORN + 2) {
        memcpy(cache, out, sizeof cache[0] * n);
        cached = n;
        at = GetTickCount();
    }
    return n;
}

/* letters only, lower case: "Cha_TurumakiMaki" -> "chaturumakimaki" */
static void letters(const char *in, char *out, int size)
{
    int o = 0;
    for (; *in && o < size - 1; ++in)
        if (isalpha((unsigned char)*in)) out[o++] = (char)tolower((unsigned char)*in);
    out[o] = 0;
}

/* How alike two names are, 0..1: the longest common subsequence over the
 * longer name ("tsurumakimaki", "turumakimaki"), or the longest common run
 * over the shorter one when it has 4 letters or more ("miyamaimoca", "moca") */
static float alike(const char *a, const char *b)
{
    int la = (int)strlen(a), lb = (int)strlen(b), seq[65][65] = { { 0 } }, run[65][65] = { { 0 } }, longest = 0;
    if (!la || !lb || la > 64 || lb > 64) return 0;
    for (int i = 1; i <= la; ++i)
        for (int j = 1; j <= lb; ++j) {
            int same = a[i - 1] == b[j - 1];
            seq[i][j] = same ? seq[i - 1][j - 1] + 1 : seq[i - 1][j] > seq[i][j - 1] ? seq[i - 1][j] : seq[i][j - 1];
            run[i][j] = same ? run[i - 1][j - 1] + 1 : 0;
            if (run[i][j] > longest) longest = run[i][j];
        }
    int lo = la < lb ? la : lb, hi = la < lb ? lb : la;
    float by_seq = (float)seq[la][lb] / (float)hi, by_run = lo >= 4 ? (float)longest / (float)lo : 0;
    return by_seq > by_run ? by_seq : by_run;
}

/* Her Animator. Its controller is named after her, but not exactly like the
 * game's id: "Cha_IA_Animator" for "Release/1stPLACE_IA", "Cha_TurumakiMaki"
 * for "Release/AHS_TsurumakiMaki". The closest name wins, if close enough. */
static void *find_animator_for(const char *id)
{
    void *as[MAX_WORN + 2], *animator = find_class("UnityEngine", "Animator"), *best = NULL;
    int n = character_animators(as, MAX_WORN + 2);
    const char *tail = strrchr(id, '/');
    tail = tail ? tail + 1 : id;
    char want[96];
    letters(strchr(tail, '_') ? strchr(tail, '_') + 1 : tail, want, sizeof want);
    float best_score = 0.75f;
    for (int i = 0; i < n; ++i) {
        void *c = invoke(method(animator, "get_runtimeAnimatorController", 0), as[i], NULL);
        char ctl[96] = "", name[96];
        if (c) to_ascii(invoke(method(ui.object, "get_name", 0), c, NULL), ctl, sizeof ctl);
        char *core = strncmp(ctl, "Cha_", 4) ? ctl : ctl + 4, *end = strstr(core, "_Animator");
        if (end) *end = 0;
        letters(core, name, sizeof name);
        float score = alike(want, name);
        if (score > best_score) {
            best_score = score;
            best = as[i];
        }
    }
    return best;
}

static void face_setup(struct worn *w)
{
    void *smr = find_class("UnityEngine", "SkinnedMeshRenderer"), *mesh_class = find_class("UnityEngine", "Mesh");
    void *rs = children_of_type(w->her, smr), *mesh = NULL;
    uintptr_t n = rs ? *(uintptr_t *)((char *)rs + 0x18) : 0;
    for (uintptr_t i = 0; i < n && !mesh; ++i) {
        void *r = ((void **)((char *)rs + 0x20))[i];
        void *m = invoke(method(smr, "get_sharedMesh", 0), r, NULL);
        if (m && unbox_int(invoke(method(mesh_class, "get_blendShapeCount", 0), m, NULL)) > 0) {
            w->face_mesh = r;
            mesh = m;
        }
    }
    void *vrm_instance = find_class("UniVRM10", "Vrm10Instance"), *runtime_class = find_class("UniVRM10", "Vrm10Runtime");
    void *keys = find_class("UniVRM10", "ExpressionKey");
    void *runtime = invoke(method(vrm_instance, "get_Runtime", 0), w->instance, NULL);
    w->face_expression = runtime ? invoke(method(runtime_class, "get_Expression", 0), runtime, NULL) : NULL;
    if (!mesh || !w->face_expression || !keys) {
        logf_("face: mesh %p, expression %p", mesh, w->face_expression);
        w->face_mesh = NULL;
        return;
    }
    keep(w, w->face_mesh);
    keep(w, w->face_expression);
    int found = 0;
    for (int k = 0; k < FACE_KEYS; ++k) {
        if (!g_face_keys[k] && (g_face_keys[k] = invoke(method(keys, g_face_map[k].preset, 0), NULL, NULL)))
            il_gchandle_new(g_face_keys[k], 0);
        for (int j = 0; j < 6; ++j) {
            w->shapes[k][j] = -1;
            if (!g_face_map[k].shapes[j]) continue;
            void *a[] = { il_string_new(g_face_map[k].shapes[j]) };
            w->shapes[k][j] = unbox_int(invoke(method(mesh_class, "GetBlendShapeIndex", 1), mesh, a));
            found += w->shapes[k][j] >= 0;
        }
    }
    logf_("face: mirroring %d of her blendshapes onto %d expressions", found, FACE_KEYS);
}

static void face_tick(void)
{
    void *smr = find_class("UnityEngine", "SkinnedMeshRenderer");
    void *expr = find_class("UniVRM10", "Vrm10RuntimeExpression");
    static void *get_weight, *set_weight;
    if (!get_weight) get_weight = method(smr, "GetBlendShapeWeight", 1);
    if (!set_weight) set_weight = method(expr, "SetWeight", 2);
    g_faces = 0;
    for (int n = 0; n < MAX_WORN; ++n) {
        struct worn *w = &g_worn[n];
        if (!w->face_mesh || !alive(w->face_mesh)) continue;
        g_faces++;
        for (int k = 0; k < FACE_KEYS; ++k) {
            if (!g_face_keys[k]) continue;
            float v = 0;
            for (int j = 0; j < 6; ++j) {
                int i = w->shapes[k][j];
                if (i < 0) continue;
                void *a[] = { &i };
                void *r = invoke(get_weight, w->face_mesh, a);
                float s = r ? *(float *)il_object_unbox(r) / 100.0f : 0;
                if (s > v) v = s;
            }
            if (v > 1) v = 1;
            void *b[] = { il_object_unbox(g_face_keys[k]), &v };
            invoke(set_weight, w->face_expression, b);
        }
    }
}

/* Shading like the game's characters. Their lilToon materials put the shadow
 * edge at half-lambert 0.36 with blur 0.14 and add a 30% rim light; in MToon
 * terms that is shift 0.28 and toony 0.86. The author's colors, textures and
 * outline stay; transparent parts (eyes) are left alone. */
static void vrm_game_look(void *m)
{
    void *material = find_class("UnityEngine", "Material");
    static const char *const s1[] = { "System.String" }, *const sf[] = { "System.String", "System.Single" };
    static const char *const sc[] = { "System.String", "UnityEngine.Color" };
    static const char *const st[] = { "System.String", "UnityEngine.Texture" };
    void *gf = method_typed(material, "GetFloat", 1, s1), *setf = method_typed(material, "SetFloat", 2, sf);
    void *gc = method_typed(material, "GetColor", 1, s1), *setc = method_typed(material, "SetColor", 2, sc);
    void *gt = method_typed(material, "GetTexture", 1, s1), *sett = method_typed(material, "SetTexture", 2, st);
    void *has = method_typed(material, "HasProperty", 1, s1);
    if (!gf || !setf || !gc || !setc || !has) return;
    void *am[] = { il_string_new("_AlphaMode") };
    void *h = invoke(has, m, am), *r;
    if (!h || !*(uint8_t *)il_object_unbox(h)) return;
    if ((r = invoke(gf, m, am)) && *(float *)il_object_unbox(r) >= 2) return;

    static const struct { const char *name; float value; } floats[] = {
        { "_ShadingShiftFactor", 0.28f }, { "_ShadingToonyFactor", 0.86f },
        { "_RimFresnelPower", 5 }, { "_RimLift", 0 }, { "_RimLightingMix", 1 },
    };
    for (size_t i = 0; i < sizeof floats / sizeof floats[0]; ++i) {
        float v = floats[i].value;
        void *a[] = { il_string_new(floats[i].name), &v };
        invoke(setf, m, a);
    }
    float rim[4] = { 0.3f, 0.3f, 0.3f, 1 };
    void *ra[] = { il_string_new("_RimColor"), rim };
    invoke(setc, m, ra);

    /* MToon shades with _ShadeColor times _ShadeTex; without a shade texture a
     * white shade color means no visible shadow at all */
    void *tn[] = { il_string_new("_ShadeTex") };
    void *tex = gt ? invoke(gt, m, tn) : NULL;
    void *sn[] = { il_string_new("_ShadeColor") };
    r = invoke(gc, m, sn);
    float *shade = r ? il_object_unbox(r) : NULL;
    if (!tex && sett) {
        void *main = invoke(method(material, "get_mainTexture", 0), m, NULL);
        void *a[] = { tn[0], main };
        if (main) invoke(sett, m, a);
    }
    if (shade && shade[0] > 0.99f && shade[1] > 0.99f && shade[2] > 0.99f) {
        float tint[4] = { 0.82f, 0.76f, 0.85f, 1 };
        void *a[] = { sn[0], tint };
        invoke(setc, m, a);
    }
}

/* the game's own MToon for VRM models ("Hidden/DesktopMate MToon"); it keeps
 * all MToon settings, while UniVRM's MToon only casts a shadow in its renderer */
static void *game_mtoon(void)
{
    static void *found;
    if (found) return found;
    void *shader = find_class("UnityEngine", "Shader"), *res = find_class("UnityEngine", "Resources");
    static const char *const tsig[] = { "System.Type" };
    void *ta[] = { il_type_get_object(il_class_get_type(shader)) };
    void *all = invoke(method_typed(res, "FindObjectsOfTypeAll", 1, tsig), NULL, ta);
    uintptr_t n = all ? *(uintptr_t *)((char *)all + 0x18) : 0;
    for (uintptr_t i = 0; i < n && !found; ++i) {
        void *sh = ((void **)((char *)all + 0x20))[i];
        char name[128];
        to_ascii(invoke(method(shader, "get_name", 0), sh, NULL), name, sizeof name);
        if (!strcmp(name, "Hidden/DesktopMate MToon")) found = sh;
    }
    if (found) il_gchandle_new(found, 0);
    else logf_("vrm: Hidden/DesktopMate MToon not found");
    return found;
}

/* puts w->instance where her model is and hands it her pose */
static void vrm_attach(struct worn *w)
{
    void *component = find_class("UnityEngine", "Component"), *transform = find_class("UnityEngine", "Transform");
    void *go_class = find_class("UnityEngine", "GameObject"), *renderer = find_class("UnityEngine", "Renderer");
    void *animator = find_class("UnityEngine", "Animator"), *material = find_class("UnityEngine", "Material");
    void *pose = find_class("UniHumanoid", "HumanPoseTransfer");
    void *ot = invoke(method(component, "get_transform", 0), w->her, NULL);
    void *og = invoke(method(component, "get_gameObject", 0), w->her, NULL);
    void *vt = invoke(method(component, "get_transform", 0), w->instance, NULL);
    w->go = keep(w, invoke(method(component, "get_gameObject", 0), w->instance, NULL));
    if (!pose || !ot || !vt || !w->go) return;

    /* same parent and local placement as her model */
    void *parent = invoke(method(transform, "get_parent", 0), ot, NULL);
    uint8_t world = 0;
    void *sp[] = { parent, &world };
    invoke(method(transform, "SetParent", 2), vt, sp);
    const char *props[][2] = { { "get_localPosition", "set_localPosition" }, { "get_localRotation", "set_localRotation" },
                               { "get_localScale", "set_localScale" } };
    float scale[3] = { 1, 1, 1 };
    for (int i = 0; i < 3; ++i) {
        void *v = invoke(method(transform, props[i][0], 0), ot, NULL);
        if (!v) continue;
        float *q = il_object_unbox(v);
        if (i == 1) {
            /* VRM 1.0 models face +Z, the game's characters the other way: q * 180 degrees about Y */
            float x = q[0], y = q[1], z = q[2], s = q[3];
            q[0] = -z;
            q[1] = s;
            q[2] = x;
            q[3] = -y;
        }
        if (i == 2) memcpy(scale, q, sizeof scale);
        void *a[] = { q };
        invoke(method(transform, props[i][1], 1), vt, a);
    }

    /* same hip height as hers, so sitting on a window lines up; humanScale is
     * the hip height of the avatar's rest pose */
    static const char *const tsig[] = { "System.Type" };
    void *ta[] = { il_type_get_object(il_class_get_type(animator)) };
    void *va = invoke(method_typed(component, "GetComponent", 1, tsig), w->instance, ta);
    void *hs = invoke(method(animator, "get_humanScale", 0), w->her, NULL);
    void *vs = va ? invoke(method(animator, "get_humanScale", 0), va, NULL) : NULL;
    float hers = hs ? *(float *)il_object_unbox(hs) : 0, ours = vs ? *(float *)il_object_unbox(vs) : 0;
    if (hers > 0 && ours > 0) {
        for (int i = 0; i < 3; ++i) scale[i] *= hers / ours;
        void *a[] = { scale };
        invoke(method(transform, "set_localScale", 1), vt, a);
    }

    /* her meshes are on the layer the character camera draws; use it for ours */
    void *rs = children_of_type(w->her, renderer);
    uintptr_t nr = rs ? *(uintptr_t *)((char *)rs + 0x18) : 0;
    int layer = 0, cast = 1;
    if (nr) {
        void *r0 = ((void **)((char *)rs + 0x20))[0];
        layer = unbox_int(invoke(method(go_class, "get_layer", 0), invoke(method(component, "get_gameObject", 0), r0, NULL), NULL));
        void *c = invoke(method(renderer, "get_shadowCastingMode", 0), r0, NULL);
        if (c) cast = *(int32_t *)il_object_unbox(c);
    }
    void *ts = children_of_type(w->instance, transform);
    uintptr_t nt = ts ? *(uintptr_t *)((char *)ts + 0x18) : 0;
    for (uintptr_t i = 0; i < nt; ++i) {
        void *a[] = { &layer };
        invoke(method(go_class, "set_layer", 1), invoke(method(component, "get_gameObject", 0), ((void **)((char *)ts + 0x20))[i], NULL), a);
    }

    /* the game's MToon with shading like hers; shadows like hers too: the
     * game's characters receive no real-time shadows, so a lying VRM had its
     * face darkened by its own hair */
    void *mtoon = game_mtoon();
    void *vrs = children_of_type(w->instance, renderer);
    uintptr_t nv = vrs ? *(uintptr_t *)((char *)vrs + 0x18) : 0;
    uint8_t receive = 0;
    for (uintptr_t i = 0; i < nv; ++i) {
        void *r = ((void **)((char *)vrs + 0x20))[i];
        void *a[] = { &receive }, *b[] = { &cast };
        invoke(method(renderer, "set_receiveShadows", 1), r, a);
        invoke(method(renderer, "set_shadowCastingMode", 1), r, b);
        void *mats = mtoon ? invoke(method(renderer, "get_sharedMaterials", 0), r, NULL) : NULL;
        uintptr_t nm = mats ? *(uintptr_t *)((char *)mats + 0x18) : 0;
        for (uintptr_t j = 0; j < nm; ++j) {
            void *m = ((void **)((char *)mats + 0x20))[j];
            int queue = unbox_int(invoke(method(material, "get_renderQueue", 0), m, NULL));
            void *sa[] = { mtoon }, *qa[] = { &queue };
            invoke(method(material, "set_shader", 1), m, sa);
            invoke(method(material, "set_renderQueue", 1), m, qa);
            vrm_game_look(m);
        }
        /* loaded hidden; shown together with hiding her */
        uint8_t on = 1;
        void *ea[] = { &on };
        invoke(method(renderer, "set_enabled", 1), r, ea);
    }

    /* hide her meshes; her skeleton keeps running the game's animation */
    uint8_t off = 0;
    w->nhidden = 0;
    for (uintptr_t i = 0; i < nr; ++i) {
        void *r = ((void **)((char *)rs + 0x20))[i];
        void *was = invoke(method(renderer, "get_enabled", 0), r, NULL);
        if (w->nhidden < 64) {
            w->hidden[w->nhidden].renderer = keep(w, r);
            w->hidden[w->nhidden++].enabled = was ? *(uint8_t *)il_object_unbox(was) : 1;
        }
        void *a[] = { &off };
        invoke(method(renderer, "set_enabled", 1), r, a);
    }

    /* her pose -> the VRM, every frame */
    void *add = method_typed(go_class, "AddComponent", 1, tsig);
    void *pa[] = { il_type_get_object(il_class_get_type(pose)) };
    w->src = keep(w, invoke(add, og, pa));
    void *dst = keep(w, invoke(add, w->go, pa));
    if (!w->src || !dst) {
        logf_("vrm: AddComponent failed (%p, %p)", w->src, dst);
        return;
    }
    int from_transfer = 1; /* HumanPoseTransferSourceType: None, HumanPoseTransfer, HumanPoseClip */
    set_field(dst, "Source", w->src); /* reference fields take the object itself */
    set_field(dst, "SourceType", &from_transfer);
    face_setup(w);
    w->since = GetTickCount();
    logf_("vrm: %s on %s, hip height %.3f (hers %.3f), %lu meshes hidden", w->file, w->character, ours, hers,
          (unsigned long)nr);
}

static void describe_renderers(const char *what, void *comp)
{
    void *component = find_class("UnityEngine", "Component"), *renderer = find_class("UnityEngine", "Renderer");
    void *go = find_class("UnityEngine", "GameObject"), *material = find_class("UnityEngine", "Material");
    void *shader = find_class("UnityEngine", "Shader");
    void *rs = children_of_type(comp, renderer);
    uintptr_t n = rs ? *(uintptr_t *)((char *)rs + 0x18) : 0;
    logf_("%s: %lu renderers", what, (unsigned long)n);
    for (uintptr_t i = 0; i < n && i < 6; ++i) {
        void *r = ((void **)((char *)rs + 0x20))[i];
        void *g = invoke(method(component, "get_gameObject", 0), r, NULL);
        void *layer = invoke(method(go, "get_layer", 0), g, NULL);
        void *b = invoke(method(renderer, "get_bounds", 0), r, NULL);
        float *bv = b ? il_object_unbox(b) : NULL;
        void *m = invoke(method(renderer, "get_sharedMaterial", 0), r, NULL);
        void *sh = m ? invoke(method(material, "get_shader", 0), m, NULL) : NULL;
        char name[64] = "", sname[96] = "";
        to_ascii(invoke(method(ui.object, "get_name", 0), g, NULL), name, sizeof name);
        if (sh) to_ascii(invoke(method(shader, "get_name", 0), sh, NULL), sname, sizeof sname);
        logf_("  %s layer %d center (%.2f %.2f %.2f) size (%.2f %.2f %.2f) shader %s", name,
              layer ? *(int *)il_object_unbox(layer) : -1, bv ? bv[0] : 0, bv ? bv[1] : 0, bv ? bv[2] : 0,
              bv ? bv[3] * 2 : 0, bv ? bv[4] * 2 : 0, bv ? bv[5] * 2 : 0, sname);
    }
}

/* the lilToon and MToon properties a material has, with their values; the
 * game strips property enumeration, so the names are listed here */
static const char *const g_look_props[] = {
    "_Color", "_AsUnlit", "_LightMinLimit", "_LightMaxLimit", "_BeforeExposureLimit", "_MonochromeLighting",
    "_lilDirectionalLightStrength", "_UseShadow", "_ShadowStrength", "_ShadowColor", "_ShadowBorder", "_ShadowBlur",
    "_ShadowNormalStrength", "_ShadowReceive", "_Shadow2ndColor", "_Shadow2ndBorder", "_Shadow2ndBlur",
    "_Shadow3rdColor", "_ShadowBorderColor", "_ShadowBorderRange", "_ShadowMainStrength", "_ShadowEnvStrength",
    "_ShadowAOShift", "_UseRim", "_RimColor", "_RimBorder", "_RimBlur", "_RimFresnelPower", "_RimEnableLighting",
    "_RimShadowMask", "_UseRimShade", "_RimShadeColor", "_RimShadeBorder", "_RimShadeBlur", "_RimShadeFresnelPower",
    "_UseBacklight", "_BacklightColor", "_UseMatCap", "_MatCapColor", "_MatCapBlend", "_UseEmission",
    "_EmissionColor", "_OutlineColor", "_OutlineWidth", "_OutlineLitColor", "_OutlineEnableLighting",
    "_OutlineFixWidth", "_OutlineVertexR2Width", "_Cull", "_Cutoff", "_ShadeColor", "_ShadingShiftFactor",
    "_ShadingToonyFactor", "_GiEqualization", "_MatcapColor", "_RimLift", "_RimLightingMix", "_OutlineWidthMode",
    "_OutlineLightingMix", "_M_CullMode", "_AlphaMode", "_ShadeShift", "_ShadeToony", "_LightColorAttenuation",
    "_IndirectLightIntensity", "_OutlineScaledMaxDistance", "_OutlineColorMode", "_Brightness",
};

static void dump_material(const char *what, void *m)
{
    void *material = find_class("UnityEngine", "Material"), *shader = find_class("UnityEngine", "Shader");
    void *sh = invoke(method(material, "get_shader", 0), m, NULL);
    if (!sh) return;
    char mname[64], sname[96];
    to_ascii(invoke(method(ui.object, "get_name", 0), m, NULL), mname, sizeof mname);
    to_ascii(invoke(method(shader, "get_name", 0), sh, NULL), sname, sizeof sname);
    static const char *const s1[] = { "System.String" }, *const i1[] = { "System.Int32" };
    void *has = method_typed(material, "HasProperty", 1, s1), *hasf = method_typed(material, "HasFloat", 1, i1);
    void *gf = method_typed(material, "GetFloat", 1, s1), *gv = method_typed(material, "GetVector", 1, s1);
    void *id = method_typed(shader, "PropertyToID", 1, s1);
    void *kw = invoke(method(material, "get_shaderKeywords", 0), m, NULL);
    char kws[400] = "";
    uintptr_t nk = kw ? *(uintptr_t *)((char *)kw + 0x18) : 0;
    for (uintptr_t i = 0, o = 0; i < nk && o < 380; ++i) {
        char k[64];
        to_ascii(((void **)((char *)kw + 0x20))[i], k, sizeof k);
        o += snprintf(kws + o, sizeof kws - o, "%s ", k);
    }
    logf_("%s material %s, shader %s, %d passes, queue %d, keywords %s", what, mname, sname,
          unbox_int(invoke(method(shader, "get_passCount", 0), sh, NULL)),
          unbox_int(invoke(method(material, "get_renderQueue", 0), m, NULL)), kws);
    char line[900];
    int o = 0;
    for (size_t i = 0; i < sizeof g_look_props / sizeof g_look_props[0]; ++i) {
        void *a[] = { il_string_new(g_look_props[i]) };
        void *h = invoke(has, m, a);
        if (!h || !*(uint8_t *)il_object_unbox(h)) continue;
        int pid = unbox_int(invoke(id, NULL, a));
        void *ia[] = { &pid };
        void *isf = invoke(hasf, m, ia);
        if (isf && *(uint8_t *)il_object_unbox(isf)) {
            void *r = invoke(gf, m, a);
            o += snprintf(line + o, sizeof line - o, " %s=%.3g", g_look_props[i], r ? *(float *)il_object_unbox(r) : -1);
        } else {
            void *r = invoke(gv, m, a);
            float *v = r ? il_object_unbox(r) : NULL;
            if (v) o += snprintf(line + o, sizeof line - o, " %s=(%.2f %.2f %.2f %.2f)", g_look_props[i], v[0], v[1], v[2], v[3]);
        }
        if (o > 700) {
            logf_(" %s", line);
            o = 0;
        }
    }
    if (o) logf_(" %s", line);
}

/* each distinct material under comp; one per shader when by_shader is set */
static void dump_materials(const char *what, void *comp, int by_shader)
{
    void *renderer = find_class("UnityEngine", "Renderer"), *material = find_class("UnityEngine", "Material");
    void *rs = children_of_type(comp, renderer);
    uintptr_t n = rs ? *(uintptr_t *)((char *)rs + 0x18) : 0;
    void *seen[24];
    int nseen = 0;
    for (uintptr_t i = 0; i < n && nseen < 24; ++i) {
        void *ms = invoke(method(renderer, "get_sharedMaterials", 0), ((void **)((char *)rs + 0x20))[i], NULL);
        uintptr_t nm = ms ? *(uintptr_t *)((char *)ms + 0x18) : 0;
        for (uintptr_t j = 0; j < nm && nseen < 24; ++j) {
            void *m = ((void **)((char *)ms + 0x20))[j];
            void *key = m && by_shader ? invoke(method(material, "get_shader", 0), m, NULL) : m;
            int dup = !key;
            for (int k = 0; k < nseen && !dup; ++k) dup = seen[k] == key;
            if (dup) continue;
            seen[nseen++] = key;
            char rn[64];
            void *go = invoke(method(find_class("UnityEngine", "Component"), "get_gameObject", 0), ((void **)((char *)rs + 0x20))[i], NULL);
            to_ascii(invoke(method(ui.object, "get_name", 0), go, NULL), rn, sizeof rn);
            logf_("%s renderer %s:", what, rn);
            dump_material(what, m);
        }
    }
}

/* touch dmhook.vrmdiag: renderers and look properties of the first worn VRM
 * and of the character wearing it */
static void vrm_diag(void)
{
    if (GetFileAttributesA(RUN_DIR "dmhook.vrmdiag") == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileA(RUN_DIR "dmhook.vrmdiag");
    for (int i = 0; i < MAX_WORN; ++i) {
        struct worn *w = &g_worn[i];
        if (!w->instance || !alive(w->her)) continue;
        describe_renderers("character", w->her);
        describe_renderers("vrm", w->instance);
        dump_materials("character", w->her, 0);
        dump_materials("vrm", w->instance, 0);
        return;
    }
    logf_("vrmdiag: no VRM worn");
}

/* Mods: custom characters. Every .vrm in the characters folder is a mod with
 * a name and a base character, whose animations and voice it uses. A mod
 * that is on is worn by its base character whenever that character is out,
 * so it works with Multi-Character too. Saved in characters.conf as
 * "<file>\t<name>\t<base>\t<on>" lines. */
#define MAX_MODS 32
struct mod {
    char file[260], name[128], base[128];
    int on, calling; /* calling: turned on while her character is not out */
};
static struct mod g_mods[MAX_MODS];
static int g_nmods;

static void utf8_to_wide(const char *s, WCHAR *out, int n)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n)) out[0] = 0;
}

static void string_utf8(void *str, char *out, int size)
{
    int n = str ? WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)il_string_chars(str), il_string_length(str), out, size - 1,
                                      NULL, NULL)
                : 0;
    out[n > 0 ? n : 0] = 0;
}

/* launch.sh writes the folder (Linux path) to characters-dir */
static int mods_dir(char *out, int size)
{
    char dir[400] = "";
    FILE *f = fopen(RUN_DIR "characters-dir", "r");
    if (!f) return 0;
    if (!fgets(dir, sizeof dir, f)) dir[0] = 0;
    fclose(f);
    dir[strcspn(dir, "\r\n")] = 0;
    snprintf(out, size, "Z:%s", dir);
    for (char *c = out; *c; ++c)
        if (*c == '/') *c = '\\';
    return dir[0] != 0;
}

static int mod_path(const struct mod *m, char *out, int size)
{
    char dir[512];
    if (!mods_dir(dir, sizeof dir)) return 0;
    snprintf(out, size, "%s\\%s", dir, m->file);
    return 1;
}

/* "Release/AHS_MiyamaiMoca" -> "Miyamai Moca" */
static void pretty_character(const char *id, char *out, int size)
{
    const char *tail = strrchr(id, '/');
    tail = tail ? tail + 1 : id;
    if (strchr(tail, '_')) tail = strchr(tail, '_') + 1;
    int o = 0;
    for (const char *c = tail; *c && o < size - 2; ++c) {
        if (c > tail && isupper((unsigned char)*c) && islower((unsigned char)c[-1])) out[o++] = ' ';
        out[o++] = *c;
    }
    out[o] = 0;
}

/* JSON string at p (after the opening quote) to UTF-8 */
static void json_string(const char *p, char *out, int size)
{
    int o = 0;
    while (*p && *p != '"' && o < size - 5) {
        unsigned c = (unsigned char)*p++;
        if (c == '\\') {
            char e = *p++;
            if (e == 'u') {
                unsigned u = 0;
                if (sscanf(p, "%4x", &u) != 1) break;
                p += 4;
                if (u >= 0xd800 && u < 0xdc00 && p[0] == '\\' && p[1] == 'u') {
                    unsigned lo = 0;
                    if (sscanf(p + 2, "%4x", &lo) == 1) {
                        u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
                        p += 6;
                    }
                }
                if (u < 0x80) out[o++] = (char)u;
                else if (u < 0x800) {
                    out[o++] = (char)(0xc0 | u >> 6);
                    out[o++] = (char)(0x80 | (u & 0x3f));
                } else if (u < 0x10000) {
                    out[o++] = (char)(0xe0 | u >> 12);
                    out[o++] = (char)(0x80 | ((u >> 6) & 0x3f));
                    out[o++] = (char)(0x80 | (u & 0x3f));
                } else {
                    out[o++] = (char)(0xf0 | u >> 18);
                    out[o++] = (char)(0x80 | ((u >> 12) & 0x3f));
                    out[o++] = (char)(0x80 | ((u >> 6) & 0x3f));
                    out[o++] = (char)(0x80 | (u & 0x3f));
                }
                continue;
            }
            c = e == 'n' || e == 't' || e == 'r' ? ' ' : (unsigned char)e;
        }
        out[o++] = (char)c;
    }
    out[o] = 0;
}

/* The model's own name from the glTF JSON of the .vrm: VRM 1.0 keeps it in
 * VRMC_vrm.meta.name, VRM 0.x in VRM.meta.title. */
static void vrm_meta_name(const char *path, char *out, int size)
{
    WCHAR wpath[600];
    utf8_to_wide(path, wpath, 600);
    FILE *f = _wfopen(wpath, L"rb");
    if (!f) return;
    uint32_t h[5];
    char *json = NULL;
    /* header "glTF", version, length; first chunk: length, "JSON" */
    if (fread(h, 4, 5, f) == 5 && h[0] == 0x46546c67 && h[4] == 0x4e4f534a && h[3] < (16u << 20) &&
        (json = malloc(h[3] + 1))) {
        if (fread(json, 1, h[3], f) == h[3]) {
            json[h[3]] = 0;
        } else {
            free(json);
            json = NULL;
        }
    }
    fclose(f);
    if (!json) return;
    const char *key = "\"name\"", *ext = strstr(json, "\"VRMC_vrm\":");
    if (!ext) {
        ext = strstr(json, "\"VRM\":");
        key = "\"title\"";
    }
    const char *meta = ext ? strstr(ext, "\"meta\"") : NULL, *p = meta ? strstr(meta, key) : NULL;
    if (p) {
        p += strlen(key);
        while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p == '"') json_string(p + 1, out, size);
    }
    free(json);
}

static void mods_conf_path(char *out, int size)
{
    conf_path(out, size);
    char *slash = strrchr(out, '/');
    if (slash) snprintf(slash + 1, size - (slash + 1 - out), "characters.conf");
}

static void mods_save(void)
{
    char path[512], tmp[520];
    mods_conf_path(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        logf_("cannot write %s", tmp);
        return;
    }
    for (int i = 0; i < g_nmods; ++i)
        fprintf(f, "%s\t%s\t%s\t%d\n", g_mods[i].file, g_mods[i].name, g_mods[i].base, g_mods[i].on);
    fclose(f);
    MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

static void mods_load(void)
{
    char path[512], line[700];
    mods_conf_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (g_nmods < MAX_MODS && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *fields[4] = { line };
        int n = 1;
        for (char *c = line; *c && n < 4; ++c)
            if (*c == '\t') {
                *c = 0;
                fields[n++] = c + 1;
            }
        if (n < 4) continue;
        struct mod *m = &g_mods[g_nmods++];
        snprintf(m->file, sizeof m->file, "%s", fields[0]);
        snprintf(m->name, sizeof m->name, "%s", fields[1]);
        snprintf(m->base, sizeof m->base, "%s", fields[2]);
        m->on = atoi(fields[3]);
    }
    fclose(f);
}

static int mod_cmp(const void *a, const void *b)
{
    return _stricmp(((const struct mod *)a)->name, ((const struct mod *)b)->name);
}

/* matches the records to the .vrm files in the folder; 1 if anything changed */
static int mods_sync(void)
{
    static char files[MAX_MODS][260];
    char dir[512], pattern[540];
    WCHAR wpattern[600];
    WIN32_FIND_DATAW fd;
    int n = 0;
    if (mods_dir(dir, sizeof dir)) {
        snprintf(pattern, sizeof pattern, "%s\\*.vrm", dir);
        utf8_to_wide(pattern, wpattern, 600);
        HANDLE h = FindFirstFileW(wpattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n < MAX_MODS &&
                    WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, files[n], 260, NULL, NULL))
                    n++;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    static struct mod next[MAX_MODS];
    int changed = n != g_nmods;
    for (int i = 0; i < n; ++i) {
        int found = -1;
        for (int j = 0; j < g_nmods && found < 0; ++j)
            if (!strcmp(g_mods[j].file, files[i])) found = j;
        if (found >= 0) {
            next[i] = g_mods[found];
            continue;
        }
        changed = 1;
        struct mod *m = &next[i];
        memset(m, 0, sizeof *m);
        snprintf(m->file, sizeof m->file, "%s", files[i]);
        char path[800];
        if (mod_path(m, path, sizeof path)) vrm_meta_name(path, m->name, sizeof m->name);
        if (!m->name[0]) {
            snprintf(m->name, sizeof m->name, "%s", files[i]);
            char *dot = strrchr(m->name, '.');
            if (dot) *dot = 0;
        }
        current_character(m->base, sizeof m->base);
        logf_("mods: new %s, \"%s\", on %s", m->file, m->name, m->base);
    }
    if (!changed) return 0;
    memcpy(g_mods, next, sizeof next[0] * n);
    g_nmods = n;
    qsort(g_mods, n, sizeof g_mods[0], mod_cmp);
    return 1;
}

static struct mod *mod_on_for(const char *id)
{
    for (int i = 0; i < g_nmods; ++i)
        if (g_mods[i].on && !strcmp(g_mods[i].base, id)) return &g_mods[i];
    return NULL;
}

/* The game's saved slots (CharacterSlot/N) go stale when a character is
 * dismissed; the scene is what counts. */
static int is_out(const char *id)
{
    return find_animator_for(id) != NULL;
}

/* Worn VRMs follow the mods that are on and the characters out. One load runs
 * at a time; UniVRM loads on the main thread in steps. */
static void worn_free(struct worn *w)
{
    destroy(w->go);
    destroy(w->src);
    void *renderer = find_class("UnityEngine", "Renderer");
    for (int i = 0; i < w->nhidden; ++i) {
        if (!alive(w->hidden[i].renderer)) continue;
        void *a[] = { &w->hidden[i].enabled };
        invoke(method(renderer, "set_enabled", 1), w->hidden[i].renderer, a);
    }
    for (int i = 0; i < w->nhandles; ++i) il_gchandle_free(w->handles[i]);
    logf_("vrm: %s off %s", w->file, w->character);
    memset(w, 0, sizeof *w);
}

static void worn_load(struct worn *w, const struct mod *m)
{
    char path[800];
    void *vrm10 = find_class("UniVRM10", "Vrm10");
    if (!vrm10 || !mod_path(m, path, sizeof path)) return;
    /* path, canLoadVrm0X, controlRig None, showMeshes, awaitCaller, material
       generator (null: UniVRM's URP one throws here), metaCallback, ct */
    uint8_t vrm0x = 1, show = 0;
    int rig = 0;
    uint64_t token = 0;
    void *args[] = { il_string_new(path), &vrm0x, &rig, &show, NULL, NULL, NULL, NULL, &token };
    w->task = keep(w, invoke(method(vrm10, "LoadPathAsync", 9), NULL, args));
    logf_("vrm: loading %s for %s", m->file, w->character);
}

/* 1 while the load runs */
static int worn_finish(struct worn *w)
{
    void *k = il_object_get_class(w->task);
    void *done = invoke(method(k, "get_IsCompleted", 0), w->task, NULL);
    if (!done || !*(uint8_t *)il_object_unbox(done)) return 1;
    void *faulted = invoke(method(k, "get_IsFaulted", 0), w->task, NULL);
    void *instance = NULL;
    if (faulted && *(uint8_t *)il_object_unbox(faulted)) {
        char text[1024];
        void *ex = invoke(method(k, "get_Exception", 0), w->task, NULL);
        to_ascii(ex ? invoke(method(il_object_get_class(ex), "ToString", 0), ex, NULL) : NULL, text, sizeof text);
        logf_("vrm: %s failed to load: %s", w->file, text);
    } else {
        instance = invoke(method(k, "get_Result", 0), w->task, NULL);
    }
    w->task = NULL;
    if (!instance) return 0; /* stays as a failed entry, so it is not retried */
    w->instance = keep(w, instance);
    if (!w->her) {
        /* loaded before her: bring her out now, put it on her when she exists */
        w->go = keep(w, invoke(method(find_class("UnityEngine", "Component"), "get_gameObject", 0), instance, NULL));
        w->waiting = 1;
        w->since = GetTickCount();
        if (!is_out(w->character)) bring_out(w->character);
        return 0;
    }
    if (!alive(w->her)) {
        w->go = keep(w, invoke(method(find_class("UnityEngine", "Component"), "get_gameObject", 0), instance, NULL));
        worn_free(w);
        return 0;
    }
    vrm_attach(w);
    return 0;
}

/* the game can turn her meshes back on while she comes out */
static void rehide(struct worn *w)
{
    void *renderer = find_class("UnityEngine", "Renderer");
    uint8_t off = 0;
    for (int i = 0; i < w->nhidden; ++i) {
        void *r = invoke(method(renderer, "get_enabled", 0), w->hidden[i].renderer, NULL);
        void *a[] = { &off };
        if (r && *(uint8_t *)il_object_unbox(r)) invoke(method(renderer, "set_enabled", 1), w->hidden[i].renderer, a);
    }
}

static void worn_tick(void)
{
    DWORD now = GetTickCount();
    int loading = 0;
    g_watching = 0;
    for (int i = 0; i < MAX_WORN; ++i) {
        struct worn *w = &g_worn[i];
        if (!w->character[0]) continue;
        const struct mod *m = mod_on_for(w->character);
        int on = m && !strcmp(m->file, w->file);
        if (w->task && worn_finish(w)) {
            loading = 1; /* finishes even when no longer wanted, then goes */
            continue;
        }
        if (!w->character[0]) continue;
        if (w->leaving) {
            if (!alive(w->her) || now - w->since > 8000) worn_free(w);
            continue;
        }
        if (w->waiting) {
            void *her = on ? find_animator_for(w->character) : NULL;
            static DWORD said;
            if (!her && now - said > 1000) {
                said = now;
                void *as[8], *animator = find_class("UnityEngine", "Animator");
                int n = character_animators(as, 8);
                char line[400] = "";
                for (int k = 0, o = 0; k < n; ++k) {
                    void *c = invoke(method(animator, "get_runtimeAnimatorController", 0), as[k], NULL);
                    char ctl[96] = "-";
                    if (c) to_ascii(invoke(method(ui.object, "get_name", 0), c, NULL), ctl, sizeof ctl);
                    o += snprintf(line + o, sizeof line - o, " %s", ctl);
                }
                logf_("vrm: waiting for %s, characters:%s", w->character, line);
            }
            if (her) {
                w->her = keep(w, her);
                w->waiting = 0;
                vrm_attach(w);
            } else if (!on || now - w->since > 20000) {
                logf_("vrm: %s did not come out", w->character);
                worn_free(w);
                continue;
            }
            g_watching++;
            continue;
        }
        if (!w->instance) {
            if (!on) worn_free(w); /* a failed load stays until the mod is off */
            continue;
        }
        if (!on || !is_out(w->character) || !alive(w->her)) {
            worn_free(w);
            continue;
        }
        if (now - w->since < 5000) {
            rehide(w);
            g_watching++;
        }
    }
    if (loading) return;
    for (int n = 0; n < g_nmods; ++n) {
        struct mod *m = &g_mods[n];
        int out = m->on && is_out(m->base);
        if (!out && !(m->on && m->calling)) continue;
        int have = 0;
        for (int i = 0; i < MAX_WORN; ++i) have |= !strcmp(g_worn[i].character, m->base) && !strcmp(g_worn[i].file, m->file);
        if (have) continue;
        void *her = out ? find_animator_for(m->base) : NULL;
        if (out && !her) continue; /* her model is not there yet */
        for (int i = 0; i < MAX_WORN; ++i) {
            struct worn *w = &g_worn[i];
            if (w->character[0]) continue;
            snprintf(w->character, sizeof w->character, "%s", m->base);
            snprintf(w->file, sizeof w->file, "%s", m->file);
            w->her = her ? keep(w, her) : NULL;
            m->calling = 0;
            worn_load(w, m);
            return;
        }
    }
}

/* The game's character list: the banner of each slot is named after the
 * character ("1stPLACE_IA"), its button brings the character out. */
#define MAX_CHARS 64
static char g_chars[MAX_CHARS][128];
static void *g_char_buttons[MAX_CHARS];
static uint32_t g_char_handles[MAX_CHARS];
static int g_nchars;

/* the banner texture's name: from the inline style the game sets in code,
 * or from the resolved style once the menu has been shown */
static void banner_name(void *e, char *out, int size)
{
    static void *resolved_bg;
    if (!resolved_bg)
        resolved_bg = il_class_get_method_from_name(ui.element, "UnityEngine.UIElements.IResolvedStyle.get_backgroundImage", 0);
    out[0] = 0;
    void *st = invoke(ui.get_style, e, NULL);
    void *m = st ? il_class_get_method_from_name(il_object_get_class(st), "get_backgroundImage", 0) : NULL;
    if (st && !m) m = il_class_get_method_from_name(il_object_get_class(st), "UnityEngine.UIElements.IStyle.get_backgroundImage", 0);
    for (int pass = 0; pass < 2 && !out[0]; ++pass) {
        void *r = pass ? (resolved_bg ? invoke(resolved_bg, e, NULL) : NULL) : (m ? invoke(m, st, NULL) : NULL);
        /* Background: texture, sprite, render texture, vector image (StyleBackground starts with it) */
        void **bg = r ? il_object_unbox(r) : NULL;
        for (int j = 0; bg && j < 4 && !out[0]; ++j)
            if (bg[j]) to_ascii(invoke(method(ui.object, "get_name", 0), bg[j], NULL), out, size);
    }
}

static void scan_characters(void *slots)
{
    int n = 0;
    for (int i = 0, c = unbox_int(invoke(ui.child_count, slots, NULL)); i < c && n < MAX_CHARS; ++i) {
        void *slot = element_at(slots, i);
        void *main = unbox_int(invoke(ui.child_count, slot, NULL)) ? element_at(slot, 0) : NULL;
        char name[96] = "";
        if (main) banner_name(main, name, sizeof name);
        if (!name[0]) continue;
        if (g_char_handles[n]) il_gchandle_free(g_char_handles[n]);
        snprintf(g_chars[n], sizeof g_chars[0], "Release/%s", name);
        g_char_handles[n] = il_gchandle_new(main, 0);
        g_char_buttons[n++] = main;
    }
    g_nchars = n;
}

static void bring_out(const char *id)
{
    void *button = find_class("UnityEngine.UIElements", "Button");
    void *clickable = find_class("UnityEngine.UIElements", "Clickable");
    for (int i = 0; i < g_nchars; ++i) {
        if (strcmp(g_chars[i], id)) continue;
        void *c = invoke(method(button, "get_clickable", 0), g_char_buttons[i], NULL);
        void *a[] = { NULL };
        if (c) invoke(method(clickable, "Invoke", 1), c, a);
        logf_("mods: brought out %s", id);
        return;
    }
    logf_("mods: no slot for %s", id);
}

/* The Mods block above the game's list: a card per mod (the game's green
 * check while it is worn, a gear for its editor), "Add a VRM model" and the
 * editor of one mod at a time. */
static void *g_mods_box, *g_add_card, *g_edit_name, *g_edit_base, *g_edit_delete, *g_edit_delete_label;
static void *g_cards[MAX_MODS], *g_card_names[MAX_MODS], *g_card_subs[MAX_MODS], *g_checks[MAX_MODS], *g_gears[MAX_MODS];
static int g_ncards, g_shown[MAX_MODS], g_editing = -1, g_delete_armed;

static void hide_input(void *toggle)
{
    void *mark = find_element(toggle, "unity-checkmark", 0);
    void *box = mark ? invoke(ui.get_parent, mark, NULL) : NULL;
    if (box) style_enum(box, "display", 1);
}

static void card_frame(void *e, float r, float g, float b)
{
    static const char *corners[] = { "TopLeft", "TopRight", "BottomRight", "BottomLeft" };
    static const char *sides[] = { "Top", "Right", "Bottom", "Left" };
    char n[64];
    style_color(e, "backgroundColor", r, g, b, 1);
    for (int i = 0; i < 4; ++i) {
        snprintf(n, sizeof n, "border%sWidth", sides[i]);
        style_float(e, n, 2);
        snprintf(n, sizeof n, "border%sColor", sides[i]);
        style_color(e, n, 0.85f, 0.85f, 0.85f, 1);
        snprintf(n, sizeof n, "border%sRadius", corners[i]);
        style_px(e, n, 3);
    }
}

static void no_spacing(void *e)
{
    static const char *sides[] = { "Top", "Right", "Bottom", "Left" };
    char n[32];
    for (int i = 0; i < 4; ++i) {
        snprintf(n, sizeof n, "margin%s", sides[i]);
        style_px(e, n, 0);
        snprintf(n, sizeof n, "padding%s", sides[i]);
        style_px(e, n, 0);
    }
}

static void *new_label(const char *text, float size, int bold, float alpha)
{
    void *l = new_element(ui.label, text, NULL, NULL);
    label_style(l, size, bold, alpha);
    no_spacing(l);
    style_enum(l, "unityTextAlign", 4); /* MiddleCenter */
    return l;
}

/* A toggle that looks like a button; *sub gets a second, smaller line. In a
 * character-slot it takes the game's slot size (the class fills its parent). */
static void *add_card(void *parent, int in_slot, const char *name, const char *caption, void **label, void **sub)
{
    void *t = new_element(ui.toggle, NULL,
                          in_slot ? "unity-base-field--no-label character-slot__main" : "unity-base-field--no-label", name);
    hide_input(t);
    style_px(t, "paddingLeft", 28);
    style_px(t, "paddingRight", 28);
    card_frame(t, 0.17f, 0.17f, 0.19f);
    style_enum(t, "flexDirection", 0); /* Column */
    style_enum(t, "justifyContent", 1); /* Center */
    style_enum(t, "alignItems", 2);     /* Center */
    void *l = new_label(caption, sub ? 15 : 16, 1, 1);
    add_child(t, l);
    if (label) *label = l;
    if (sub) {
        *sub = new_label("", 11, 0, 0.75f);
        add_child(t, *sub);
    }
    add_child(parent, t);
    il_gchandle_new(t, 0);
    return t;
}

static void *add_heading(void *parent, const char *text, float size, int bold, float alpha)
{
    void *l = new_element(ui.label, text, NULL, NULL);
    label_style(l, size, bold, alpha);
    style_px(l, "marginTop", 6);
    style_px(l, "marginBottom", 6);
    style_enum(l, "whiteSpace", 0); /* Normal: wraps */
    add_child(parent, l);
    return l;
}

static void set_text(void *label, const char *text)
{
    void *a[] = { il_string_new(text) };
    invoke(method(ui.label, "set_text", 1), label, a);
}

static void card_sub(int i)
{
    char who[128], text[160];
    pretty_character(g_mods[i].base, who, sizeof who);
    snprintf(text, sizeof text, "as %s", who);
    set_text(g_card_subs[i], text);
}

static void build_editor(void *parent, int i)
{
    void *box = new_element(ui.element, NULL, NULL, "linux-mod-editor");
    card_frame(box, 0.12f, 0.12f, 0.14f);
    style_px(box, "paddingLeft", 8);
    style_px(box, "paddingRight", 8);
    style_px(box, "paddingBottom", 8);
    style_px(box, "marginBottom", 8);
    add_heading(box, "Name", 13, 1, 0.9f);
    void *tf = find_class("UnityEngine.UIElements", "TextField");
    g_edit_name = il_object_new(tf);
    invoke(method(tf, ".ctor", 0), g_edit_name, NULL);
    void *na[] = { il_string_new(g_mods[i].name) };
    invoke(method(tf, "SetValueWithoutNotify", 1), g_edit_name, na);
    void *input = find_element(g_edit_name, "unity-text-input", 0);
    if (input) {
        style_color(input, "backgroundColor", 0.08f, 0.08f, 0.09f, 1);
        style_color(input, "color", 1, 1, 1, 1);
        style_px(input, "fontSize", 15);
    }
    add_child(box, g_edit_name);
    il_gchandle_new(g_edit_name, 0);

    add_heading(box, "Animations and voice of", 13, 1, 0.9f);
    void *dd = find_class("UnityEngine.UIElements", "DropdownField");
    g_edit_base = new_element(dd, NULL, "unity-base-field--no-label menu-dropdown", NULL);
    void *list = invoke(method(dd, "get_choices", 0), g_edit_base, NULL);
    if (!list) {
        /* a new List<string>, made from the class of the game's own dropdown list */
        void *other = find_in_documents("voice-pattern-dropdown");
        void *ol = other ? invoke(method(dd, "get_choices", 0), other, NULL) : NULL;
        if (ol && (list = il_object_new(il_object_get_class(ol)))) invoke(method(il_object_get_class(ol), ".ctor", 0), list, NULL);
    }
    char who[128], current[128];
    pretty_character(g_mods[i].base, current, sizeof current);
    for (int c = 0; list && c < g_nchars; ++c) {
        pretty_character(g_chars[c], who, sizeof who);
        void *a[] = { il_string_new(who) };
        invoke(method(il_object_get_class(list), "Add", 1), list, a);
    }
    if (list) {
        void *a[] = { list };
        invoke(method(dd, "set_choices", 1), g_edit_base, a);
    }
    void *va[] = { il_string_new(current) };
    invoke(method(dd, "SetValueWithoutNotify", 1), g_edit_base, va);
    add_child(box, g_edit_base);
    il_gchandle_new(g_edit_base, 0);

    g_edit_delete = add_card(box, 0, "linux-mod-delete", "Delete", &g_edit_delete_label, NULL);
    card_frame(g_edit_delete, 0.45f, 0.12f, 0.12f);
    style_px(g_edit_delete, "height", 30);
    style_px(g_edit_delete, "marginLeft", 0);
    style_px(g_edit_delete, "marginRight", 0);
    g_delete_armed = 0;
    add_child(parent, box);
}

static void build_mods(void *slots)
{
    void *parent = invoke(ui.get_parent, slots, NULL);
    if (!parent) return;
    scan_characters(slots);
    void *ia[] = { slots };
    int at = unbox_int(invoke(method(ui.element, "IndexOf", 1), parent, ia));
    /* the game's list keeps 16 px on the left for the check icon */
    void *box = new_element(ui.element, NULL, NULL, "linux-mods");
    style_px(box, "paddingLeft", 16);
    style_px(box, "paddingRight", 15);
    add_heading(box, "Mods", 20, 1, 1);
    for (int i = 0; i < g_nmods; ++i) {
        char name[32];
        void *slot = new_element(ui.element, NULL, "character-slot", NULL);
        snprintf(name, sizeof name, "linux-mod-%d", i);
        g_cards[i] = add_card(slot, 1, name, g_mods[i].name, &g_card_names[i], &g_card_subs[i]);
        card_sub(i);
        g_checks[i] = new_element(ui.element, NULL, "character-slot__choice-icon", NULL);
        add_child(slot, g_checks[i]);
        il_gchandle_new(g_checks[i], 0);
        g_gears[i] = new_element(ui.toggle, NULL, "unity-base-field--no-label character-slot__setting-button", NULL);
        hide_input(g_gears[i]);
        style_enum(g_gears[i], "display", 0);
        style_enum(g_gears[i], "position", 1); /* Absolute */
        style_px(g_gears[i], "right", 6);
        style_px(g_gears[i], "top", 6);
        style_px(g_gears[i], "width", 26);
        style_px(g_gears[i], "height", 26);
        add_child(slot, g_gears[i]);
        il_gchandle_new(g_gears[i], 0);
        g_shown[i] = -1;
        add_child(box, slot);
        if (i == g_editing) build_editor(box, i);
    }
    g_ncards = g_nmods;
    void *slot = new_element(ui.element, NULL, "character-slot", NULL);
    g_add_card = add_card(slot, 1, "linux-mods-add", "+  Add a VRM model", NULL, NULL);
    add_child(box, slot);
    add_heading(box, "A mod is worn by the character chosen with its gear and uses that character's animations and voice.",
                12, 0, 0.8f);
    add_heading(box, "Characters", 20, 1, 1);
    void *a[] = { &at, box };
    invoke(method(ui.element, "Insert", 2), parent, a);
    il_gchandle_new(box, 0);
    g_mods_box = box;
}

static void rebuild_mods(void)
{
    if (g_mods_box) invoke(method(ui.element, "RemoveFromHierarchy", 0), g_mods_box, NULL);
    g_mods_box = NULL;
    g_ncards = 0;
}

static int toggled(void *t)
{
    void *r = invoke(method(ui.toggle, "get_value", 0), t, NULL);
    return r && *(uint8_t *)il_object_unbox(r);
}

static void set_toggle(void *t, int v)
{
    uint8_t b = (uint8_t)v;
    void *a[] = { &b };
    invoke(method(ui.toggle, "SetValueWithoutNotify", 1), t, a);
}

/* a click on a mod card: wear it (bringing its character out) or take it off */
static void click_mod(int i)
{
    struct mod *m = &g_mods[i];
    void *others[MAX_WORN + 2];
    if (m->on && is_out(m->base)) {
        m->on = 0;
        /* with others out, a click on her slot dismisses her; the VRM leaves with her */
        if (character_animators(others, MAX_WORN + 2) > 1) {
            for (int j = 0; j < MAX_WORN; ++j)
                if (!strcmp(g_worn[j].character, m->base) && g_worn[j].instance && !g_worn[j].waiting) {
                    g_worn[j].leaving = 1;
                    g_worn[j].since = GetTickCount();
                }
            bring_out(m->base);
        }
    } else {
        for (int j = 0; j < g_nmods; ++j)
            if (j != i && !strcmp(g_mods[j].base, m->base)) g_mods[j].on = 0;
        m->on = 1;
        /* loaded first; worn_finish brings her out */
        m->calling = !is_out(m->base);
    }
    logf_("mods: %s %s", m->name, m->on ? "on" : "off");
    mods_save();
}

static void edit_tick(void)
{
    struct mod *m = &g_mods[g_editing];
    char text[128];
    void *tf = find_class("UnityEngine.UIElements", "TextField"), *dd = find_class("UnityEngine.UIElements", "DropdownField");
    string_utf8(invoke(method(tf, "get_value", 0), g_edit_name, NULL), text, sizeof text);
    for (char *c = text; *c; ++c)
        if (*c == '\t') *c = ' ';
    if (text[0] && strcmp(text, m->name)) {
        snprintf(m->name, sizeof m->name, "%s", text);
        set_text(g_card_names[g_editing], m->name);
        mods_save();
    }
    char who[128];
    to_ascii(invoke(method(dd, "get_value", 0), g_edit_base, NULL), text, sizeof text);
    for (int c = 0; c < g_nchars; ++c) {
        pretty_character(g_chars[c], who, sizeof who);
        if (strcmp(who, text) || !strcmp(g_chars[c], m->base)) continue;
        snprintf(m->base, sizeof m->base, "%s", g_chars[c]);
        if (m->on)
            for (int j = 0; j < g_nmods; ++j)
                if (&g_mods[j] != m && !strcmp(g_mods[j].base, m->base)) g_mods[j].on = 0;
        card_sub(g_editing);
        mods_save();
        logf_("mods: %s now on %s", m->name, m->base);
        break;
    }
    if (!toggled(g_edit_delete)) return;
    set_toggle(g_edit_delete, 0);
    if (!g_delete_armed) {
        g_delete_armed = 1;
        set_text(g_edit_delete_label, "Click again to delete");
        return;
    }
    char path[800];
    WCHAR wpath[800];
    if (mod_path(m, path, sizeof path)) {
        utf8_to_wide(path, wpath, 800);
        DeleteFileW(wpath);
    }
    logf_("mods: deleted %s", m->file);
    memmove(m, m + 1, (size_t)(g_nmods - g_editing - 1) * sizeof *m);
    g_nmods--;
    g_editing = -1;
    mods_save();
    rebuild_mods();
}

static void click_mod(int i);

static void mods_tick(void)
{
    /* development: dmhook.click with a mod index clicks its card */
    FILE *dev = fopen(RUN_DIR "dmhook.click", "r");
    if (dev) {
        int i = -1;
        if (fscanf(dev, "%d", &i) != 1) i = -1;
        fclose(dev);
        DeleteFileA(RUN_DIR "dmhook.click");
        if (i >= 0 && i < g_nmods) click_mod(i);
        /* -1: the game's Multi-Character switch */
        void *t = i == -1 ? find_in_documents("multi-chara-toggle") : NULL;
        void *c = t ? invoke(method(find_class("UnityEngine.UIElements", "Button"), "get_clickable", 0), t, NULL) : NULL;
        void *na[] = { NULL };
        if (c) invoke(method(find_class("UnityEngine.UIElements", "Clickable"), "Invoke", 1), c, na);
    }
    static DWORD last_scan;
    static int loaded;
    char picked[260] = "";
    if (!loaded) {
        mods_load();
        loaded = 1;
    }
    /* launch.sh answers "Add a VRM model" with the copied file's name */
    FILE *f = fopen(RUN_DIR "pick-vrm.done", "r");
    if (f) {
        if (!fgets(picked, sizeof picked, f)) picked[0] = 0;
        fclose(f);
        DeleteFileA(RUN_DIR "pick-vrm.done");
        picked[strcspn(picked, "\r\n")] = 0;
    }
    if (picked[0] || GetTickCount() - last_scan > 2000) {
        last_scan = GetTickCount();
        if (mods_sync()) {
            g_editing = -1;
            mods_save();
            rebuild_mods();
        }
        for (int i = 0; picked[0] && i < g_nmods; ++i)
            if (!strcmp(g_mods[i].file, picked)) {
                current_character(g_mods[i].base, sizeof g_mods[i].base);
                g_mods[i].on = 0;
                click_mod(i);
                rebuild_mods();
            }
    }
    if (!g_mods_box || !invoke(ui.get_parent, g_mods_box, NULL)) {
        void *slots = find_in_documents("character-slots");
        if (slots) build_mods(slots);
        return;
    }
    if (!g_nchars) scan_characters(find_in_documents("character-slots"));

    for (int i = 0; i < g_ncards; ++i) {
        int worn = g_mods[i].on && is_out(g_mods[i].base);
        if (toggled(g_cards[i]) != (g_shown[i] == 1)) {
            click_mod(i);
            worn = g_mods[i].on && is_out(g_mods[i].base);
            g_shown[i] = -1;
        }
        if (worn != g_shown[i]) {
            g_shown[i] = worn;
            set_toggle(g_cards[i], worn);
            style_enum(g_checks[i], "display", worn ? 0 : 1);
        }
        if (toggled(g_gears[i])) {
            set_toggle(g_gears[i], 0);
            g_editing = g_editing == i ? -1 : i;
            rebuild_mods();
            return;
        }
    }
    if (g_editing >= 0 && g_editing < g_ncards) edit_tick();
    if (g_add_card && toggled(g_add_card)) {
        set_toggle(g_add_card, 0);
        if ((f = fopen(RUN_DIR "pick-vrm", "w"))) fclose(f);
        logf_("mods: asked for a file");
    }
}

/* Character menu diagnostics: touch dmhook.slots. Layout, banner texture and
 * check icon state of the first slots, the saved slots and the characters in
 * the scene. */
static float *rect_of(void *e, float out[4])
{
    void *r = invoke(method(ui.element, "get_worldBound", 0), e, NULL);
    if (r) memcpy(out, il_object_unbox(r), 4 * sizeof(float));
    else out[0] = out[1] = out[2] = out[3] = -1;
    return out;
}

static void resolved(void *e, const char *prop, char *out, int size)
{
    char name[128];
    snprintf(name, sizeof name, "UnityEngine.UIElements.IResolvedStyle.get_%s", prop);
    void *m = il_class_get_method_from_name(ui.element, name, 0);
    void *r = m ? invoke(m, e, NULL) : NULL;
    if (!r) {
        snprintf(out, size, "?");
        return;
    }
    if (!strcmp(prop, "backgroundImage")) {
        void **bg = il_object_unbox(r);
        out[0] = 0;
        for (int i = 0; i < 4 && !out[0]; ++i)
            if (bg[i]) to_ascii(invoke(method(ui.object, "get_name", 0), bg[i], NULL), out, size);
        if (!out[0]) snprintf(out, size, "none");
    } else if (!strcmp(prop, "opacity")) {
        snprintf(out, size, "%.2f", *(float *)il_object_unbox(r));
    } else {
        snprintf(out, size, "%d", *(int32_t *)il_object_unbox(r));
    }
}

static void slots_diag(void)
{
    if (GetFileAttributesA(RUN_DIR "dmhook.slots") == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileA(RUN_DIR "dmhook.slots");
    for (int i = 0; i < 6; ++i) {
        char key[32], id[128];
        snprintf(key, sizeof key, "CharacterSlot/%d", i);
        void *a[] = { il_string_new(key) };
        to_ascii(invoke(method(ui.prefs, "GetString", 1), NULL, a), id, sizeof id);
        logf_("prefs %s = %s", key, id);
    }
    void *slots = find_in_documents("character-slots");
    void *parent = slots ? invoke(ui.get_parent, slots, NULL) : NULL;
    void *lists[] = { parent ? element_at(parent, 0) : NULL, slots };
    for (int l = 0; l < 2; ++l) {
        if (!lists[l]) continue;
        float r[4];
        rect_of(lists[l], r);
        logf_("%s list at %.0f,%.0f %.0fx%.0f", l ? "game" : "first", r[0], r[1], r[2], r[3]);
        for (int i = 0, c = unbox_int(invoke(ui.child_count, lists[l], NULL)); i < c && i < 8; ++i) {
            void *slot = element_at(lists[l], i);
            rect_of(slot, r);
            logf_(" child %d at %.0f,%.0f %.0fx%.0f", i, r[0], r[1], r[2], r[3]);
            for (int j = 0, cc = unbox_int(invoke(ui.child_count, slot, NULL)); j < cc; ++j) {
                void *e = element_at(slot, j);
                char cls[64] = "", bg[96], disp[16], vis[16], op[16];
                void *list = field_obj(e, "m_ClassList");
                if (list) {
                    int zero = 0;
                    void *za[] = { &zero };
                    to_ascii(invoke(method(il_object_get_class(list), "get_Item", 1), list, za), cls, sizeof cls);
                }
                rect_of(e, r);
                resolved(e, "backgroundImage", bg, sizeof bg);
                resolved(e, "display", disp, sizeof disp);
                resolved(e, "visibility", vis, sizeof vis);
                resolved(e, "opacity", op, sizeof op);
                logf_("   %s at %.0f,%.0f %.0fx%.0f bg %s display %s visibility %s opacity %s", cls, r[0], r[1], r[2],
                      r[3], bg, disp, vis, op);
            }
        }
    }
    inspect_characters();
}

/* runs on the game's main thread */
static void tick(void)
{
    static int ready;
    if (!ready) {
        ready = resolve_ui() ? 1 : -1;
        logf_("ui %s", ready > 0 ? "ready" : "not available, giving up");
        if (ready > 0) {
            load_conf();
            if (g_conf[FPS30]) apply_fps();
        }
    }
    if (ready < 0) return;
    dump_request();
    inspect_request();
    tick_bright();
    slots_diag();
    mods_tick();
    worn_tick();
    face_tick();
    vrm_diag();

    /* the game builds every page at start; rebuild ours if it ever goes away */
    if (!g_section || !invoke(ui.get_parent, g_section, NULL)) {
        void *common = find_in_documents("common-section");
        if (common) build_section(common);
        return;
    }
    for (int i = 0; i < NSETTINGS; ++i) {
        void *r = invoke(method(ui.toggle, "get_value", 0), g_toggles[i], NULL);
        if (r) g_conf[i] = *(uint8_t *)il_object_unbox(r);
    }
    if (memcmp(g_conf, g_saved, sizeof g_conf)) {
        if (g_conf[FPS30] != g_saved[FPS30]) apply_fps();
        save_conf();
    }
}

static WNDPROC g_prev_proc;

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DMHOOK && wp) {
        g_dragging = wp == 1;
        if (g_section) apply_fps();
        return 0;
    }
    if (msg == WM_DMHOOK) {
        /* a message loop inside our own managed calls must not tick again */
        static int busy;
        if (!busy) {
            busy = 1;
            tick();
            busy = 0;
        }
        return 0;
    }
    return CallWindowProcW(g_prev_proc, hwnd, msg, wp, lp);
}

/* This thread never runs managed code: an il2cpp-attached thread that exits
 * without detaching makes the next garbage collection abort the game. */
static DWORD WINAPI worker(void *arg)
{
    (void)arg;
    g_log = fopen(LOG_PATH, "w");
    HWND game = NULL;
    for (int i = 0; i < 600 && !(game = FindWindowW(L"UnityWndClass", NULL)); ++i) Sleep(100);
    if (!game || !(g_ga = GetModuleHandleA("GameAssembly.dll"))) {
        logf_("game window or GameAssembly.dll not found");
        return 0;
    }
    if (!load_map()) {
        logf_("no %s", MAP_PATH);
        return 0;
    }
    if (!resolve_api() || !widen_caller_check((uint8_t *)il_class_from_name)) return 0;
    for (int i = 0; i < 600 && !il_domain_get(); ++i) Sleep(100);

    DEVMODEW mode = { .dmSize = sizeof mode };
    g_drag_fps = EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 60
               ? (int)mode.dmDisplayFrequency : 60;
    logf_("%d FPS while dragging", g_drag_fps);
    /* window messages run on the main thread, where UI Toolkit may be used.
       The game can recreate its window, so follow it. Mouse buttons only
       reach the game when pressed on the character. */
    HWND hooked = NULL;
    int held = 0;
    for (DWORD waited = 0;; waited += DRAG_POLL) {
        /* the game's window plugin subclasses the window after us and calls
           our procedure in turn, so only a new window needs hooking */
        if (!hooked || !IsWindow(hooked)) {
            if (!(game = FindWindowW(L"UnityWndClass", NULL))) {
                Sleep(500);
                continue;
            }
            g_prev_proc = (WNDPROC)SetWindowLongPtrW(game, GWLP_WNDPROC, (LONG_PTR)wndproc);
            logf_("%s game window %p", hooked ? "re-hooked" : "hooked", (void *)game);
            hooked = game;
        }
        int now = GetAsyncKeyState(VK_LBUTTON) < 0;
        if (now != held && g_drag_fps > 60) {
            held = now;
            PostMessageW(hooked, WM_DMHOOK, held ? 1 : 2, 0);
        }
        if (waited >= (g_watching ? 10u : g_faces ? 30u : (DWORD)TICK_MS)) {
            PostMessageW(hooked, WM_DMHOOK, 0, 0);
            waited = 0;
        }
        Sleep(DRAG_POLL);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    }
    return TRUE;
}
