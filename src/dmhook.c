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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
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
    R(gchandle_new) R(class_get_methods) R(method_get_name) R(method_get_param_count)
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
    if (depth < 12)
        for (int i = 0, c = unbox_int(invoke(ui.child_count, e, NULL)); i < c; ++i) dump(element_at(e, i), depth + 1);
}

static void dump_request(void)
{
    char name[128];
    FILE *f = fopen(DUMP_PATH, "r");
    if (!f) return;
    int ok = fscanf(f, "%127s", name) == 1;
    fclose(f);
    DeleteFileA(DUMP_PATH);
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
    if (GetFileAttributesA(RUN_DIR "dmhook.inspect") == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileA(RUN_DIR "dmhook.inspect");
    static const char *classes[][2] = {
        { "UniVRM10", "Vrm10" }, { "UniVRM10", "Vrm10Data" }, { "UniVRM10", "Vrm10Importer" },
        { "UniVRM10", "Vrm10Instance" }, { "UniVRM10", "Vrm10Runtime" }, { "UniVRM10", "MigrationVrm" },
        { "UniVRM10", "UrpVrm10MaterialDescriptorGenerator" }, { "UniGLTF", "GlbFileParser" },
        { "UniGLTF", "GltfData" }, { "UniGLTF", "RuntimeGltfInstance" }, { "UniGLTF", "ImporterContextExtensions" },
        { "VRMShaders", "ImmediateCaller" }, { "VRMShaders", "RuntimeOnlyAwaitCaller" },
        { "UniHumanoid", "HumanPoseTransfer" }, { "UniVRM10", "ControlRigGenerationOption" },
    };
    for (size_t i = 0; i < sizeof classes / sizeof classes[0]; ++i) inspect_class(classes[i][0], classes[i][1]);
    inspect_characters();
}

/* Custom character proof of concept: write a Windows path of a .vrm file to
 * /dev/shm/ghost-bridge/dmhook.vrm. The VRM is loaded with the game's own
 * UniVRM10, put where the character's model is, and follows her pose through
 * UniHumanoid.HumanPoseTransfer while her own meshes are hidden. */
static void *g_vrm_task, *g_pose_src, *g_pose_dst;

static void set_field(void *obj, const char *name, void *value)
{
    void *f = il_class_get_field_from_name(il_object_get_class(obj), name);
    if (f) il_field_set_value(obj, f, value);
    else logf_("vrm: no field %s", name);
}

/* the humanoid Animator of the character under VRMRoot */
static void *find_character_animator(void)
{
    void *animator = find_class("UnityEngine", "Animator"), *component = find_class("UnityEngine", "Component");
    void *transform = find_class("UnityEngine", "Transform");
    int none = 0;
    void *args[] = { il_type_get_object(il_class_get_type(animator)), &none };
    void *arr = invoke(ui.find_objects, NULL, args);
    uintptr_t n = arr ? *(uintptr_t *)((char *)arr + 0x18) : 0;
    for (uintptr_t i = 0; i < n; ++i) {
        void *a = ((void **)((char *)arr + 0x20))[i];
        void *human = invoke(method(animator, "get_isHuman", 0), a, NULL);
        if (!human || !*(uint8_t *)il_object_unbox(human)) continue;
        void *t = invoke(method(component, "get_transform", 0), a, NULL);
        void *parent = t ? invoke(method(transform, "get_parent", 0), t, NULL) : NULL;
        char name[64] = "";
        if (parent) to_ascii(invoke(method(ui.object, "get_name", 0), parent, NULL), name, sizeof name);
        if (!strcmp(name, "VRMRoot")) return a;
    }
    return NULL;
}

static void vrm_attach(void *instance)
{
    int stage = 3;
    FILE *sf = fopen(RUN_DIR "dmhook.vrmstage", "r");
    if (sf) {
        if (fscanf(sf, "%d", &stage) != 1) stage = 3;
        fclose(sf);
    }
    logf_("vrm: attach stage %d", stage);
    if (stage < 1) return;
    void *component = find_class("UnityEngine", "Component"), *transform = find_class("UnityEngine", "Transform");
    void *go_class = find_class("UnityEngine", "GameObject"), *renderer = find_class("UnityEngine", "Renderer");
    void *pose = find_class("UniHumanoid", "HumanPoseTransfer");
    void *orig = find_character_animator();
    if (!orig || !pose) {
        logf_("vrm: character animator %p, HumanPoseTransfer %p", orig, pose);
        return;
    }
    void *ot = invoke(method(component, "get_transform", 0), orig, NULL);
    void *og = invoke(method(component, "get_gameObject", 0), orig, NULL);
    void *vt = invoke(method(component, "get_transform", 0), instance, NULL);
    void *vg = invoke(method(component, "get_gameObject", 0), instance, NULL);

    /* same parent and local placement as her model */
    void *parent = invoke(method(transform, "get_parent", 0), ot, NULL);
    uint8_t keep = 0;
    void *sp[] = { parent, &keep };
    invoke(method(transform, "SetParent", 2), vt, sp);
    const char *props[][2] = { { "get_localPosition", "set_localPosition" }, { "get_localRotation", "set_localRotation" },
                               { "get_localScale", "set_localScale" } };
    for (int i = 0; i < 3; ++i) {
        void *v = invoke(method(transform, props[i][0], 0), ot, NULL);
        if (!v) continue;
        float *q = il_object_unbox(v);
        if (i == 1) {
            /* VRM 1.0 models face +Z, the game's characters the other way: q * 180 degrees about Y */
            float x = q[0], y = q[1], z = q[2], w = q[3];
            q[0] = -z;
            q[1] = w;
            q[2] = x;
            q[3] = -y;
        }
        void *a[] = { q };
        invoke(method(transform, props[i][1], 1), vt, a);
    }

    /* her meshes are on the layer the character camera draws; use it for ours */
    void *rs = children_of_type(orig, renderer);
    uintptr_t nr = rs ? *(uintptr_t *)((char *)rs + 0x18) : 0;
    for (uintptr_t i = 0; i < nr; ++i) {
        void *g = invoke(method(component, "get_gameObject", 0), ((void **)((char *)rs + 0x20))[i], NULL);
        void *layer = invoke(method(go_class, "get_layer", 0), g, NULL);
        char name[64];
        to_ascii(invoke(method(ui.object, "get_name", 0), g, NULL), name, sizeof name);
        void *mat = invoke(method(renderer, "get_sharedMaterial", 0), ((void **)((char *)rs + 0x20))[i], NULL);
        void *sh = mat ? invoke(method(find_class("UnityEngine", "Material"), "get_shader", 0), mat, NULL) : NULL;
        char sname[128] = "";
        if (sh) to_ascii(invoke(method(find_class("UnityEngine", "Shader"), "get_name", 0), sh, NULL), sname, sizeof sname);
        logf_("vrm: her renderer %s on layer %d, shader %s", name, layer ? *(int32_t *)il_object_unbox(layer) : -1, sname);
    }
    if (nr) {
        void *g = invoke(method(component, "get_gameObject", 0), ((void **)((char *)rs + 0x20))[0], NULL);
        void *layer = invoke(method(go_class, "get_layer", 0), g, NULL);
        void *ts = children_of_type(instance, transform);
        uintptr_t nt = ts ? *(uintptr_t *)((char *)ts + 0x18) : 0;
        int l = layer ? *(int32_t *)il_object_unbox(layer) : 0;
        for (uintptr_t i = 0; i < nt; ++i) {
            void *tg = invoke(method(component, "get_gameObject", 0), ((void **)((char *)ts + 0x20))[i], NULL);
            void *a[] = { &l };
            invoke(method(go_class, "set_layer", 1), tg, a);
        }
        logf_("vrm: layer %d on %lu objects", l, (unsigned long)nt);
    }

    /* the game draws characters with its own lilToon build; MToon only casts a
       shadow there. Move the VRM's materials over, keeping texture and color. */
    void *shader_class = find_class("UnityEngine", "Shader"), *material_class = find_class("UnityEngine", "Material");
    void *templ = NULL, *lil = NULL;
    for (uintptr_t i = 0; i < nr && !templ; ++i) {
        void *m = invoke(method(renderer, "get_sharedMaterial", 0), ((void **)((char *)rs + 0x20))[i], NULL);
        void *sh = m ? invoke(method(material_class, "get_shader", 0), m, NULL) : NULL;
        char sname[128] = "";
        if (sh) to_ascii(invoke(method(shader_class, "get_name", 0), sh, NULL), sname, sizeof sname);
        if (!strcmp(sname, "Hidden/lilToonEncryptable/lilToonOutline")) {
            templ = m;
            lil = sh;
        }
    }
    void *vrs = children_of_type(instance, renderer);
    uintptr_t nv = vrs ? *(uintptr_t *)((char *)vrs + 0x18) : 0;
    int changed = 0;
    for (uintptr_t i = 0; lil && i < nv; ++i) {
        void *r = ((void **)((char *)vrs + 0x20))[i];
        void *get = il_class_get_method_from_name(renderer, "get_materials", 0);
        if (!get) get = il_class_get_method_from_name(renderer, "get_sharedMaterials", 0);
        void *mats = invoke(get, r, NULL);
        if (i == 0) logf_("vrm: %lu renderers, materials getter %p, array %p", (unsigned long)nv, get, mats);
        uintptr_t nm = mats ? *(uintptr_t *)((char *)mats + 0x18) : 0;
        for (uintptr_t j = 0; j < nm; ++j) {
            void *m = ((void **)((char *)mats + 0x20))[j];
            void *tex = invoke(method(material_class, "get_mainTexture", 0), m, NULL);
            void *col = invoke(method(material_class, "get_color", 0), m, NULL);
            float c[4] = { 1, 1, 1, 1 };
            if (col) memcpy(c, il_object_unbox(col), sizeof c);
            void *a[] = { lil };
            invoke(method(material_class, "set_shader", 1), m, a);
            void *tp[] = { templ };
            if (GetFileAttributesA(RUN_DIR "dmhook.vrm-copyprops") != INVALID_FILE_ATTRIBUTES)
                invoke(method(material_class, "CopyPropertiesFromMaterial", 1), m, tp);
            /* her keys unscramble the game's own meshes; ours are not scrambled */
            static const char *const fsig[] = { "System.String", "System.Single" };
            float one = 1;
            void *fa[] = { il_string_new("_IgnoreEncryption"), &one };
            invoke(method_typed(material_class, "SetFloat", 2, fsig), m, fa);
            void *t[] = { tex };
            invoke(method(material_class, "set_mainTexture", 1), m, t);
            void *cc[] = { c };
            invoke(method(material_class, "set_color", 1), m, cc);
            changed++;
        }
    }
    logf_("vrm: lilToon %p, %d materials moved", lil, changed);

    if (stage < 2) return;
    /* hide her meshes; her skeleton keeps running the game's animation, IK and colliders */
    uint8_t off = 0;
    for (uintptr_t i = 0; i < nr; ++i) {
        void *a[] = { &off };
        invoke(method(renderer, "set_enabled", 1), ((void **)((char *)rs + 0x20))[i], a);
    }

    if (stage < 3) return;
    /* her pose -> the VRM, every frame */
    static const char *const tsig[] = { "System.Type" };
    void *add = method_typed(go_class, "AddComponent", 1, tsig);
    void *ta[] = { il_type_get_object(il_class_get_type(pose)) };
    void *src = invoke(add, og, ta);
    void *dst = invoke(add, vg, ta);
    if (!src || !dst) {
        logf_("vrm: AddComponent failed (%p, %p)", src, dst);
        return;
    }
    int from_transfer = 1; /* HumanPoseTransferSourceType: None, HumanPoseTransfer, HumanPoseClip */
    set_field(dst, "Source", src); /* reference fields take the object itself */
    set_field(dst, "SourceType", &from_transfer);
    il_gchandle_new(src, 0);
    il_gchandle_new(dst, 0);
    g_pose_src = src;
    g_pose_dst = dst;
    logf_("vrm: attached, %lu renderers hidden", (unsigned long)nr);
}

static void *g_vrm_instance;

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

static void vrm_diag(void)
{
    if (GetFileAttributesA(RUN_DIR "dmhook.vrmdiag") == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileA(RUN_DIR "dmhook.vrmdiag");
    void *orig = find_character_animator();
    if (orig) describe_renderers("character", orig);
    if (g_vrm_instance) describe_renderers("vrm", g_vrm_instance);
    void *behaviour = find_class("UnityEngine", "Behaviour");
    for (int i = 0; i < 2; ++i) {
        void *c = i ? g_pose_dst : g_pose_src;
        if (!c) continue;
        void *k = il_object_get_class(c), *v = NULL;
        int type = -1;
        void *f = il_class_get_field_from_name(k, "Avatar");
        if (f) il_field_get_value(c, f, &v);
        void *avatar = v;
        v = NULL;
        if ((f = il_class_get_field_from_name(k, "Source"))) il_field_get_value(c, f, &v);
        if ((f = il_class_get_field_from_name(k, "SourceType"))) il_field_get_value(c, f, &type);
        void *handler = NULL;
        if ((f = il_class_get_field_from_name(k, "tgt"))) il_field_get_value(c, f, &handler);
        void *en = invoke(method(behaviour, "get_enabled", 0), c, NULL);
        void *ae = invoke(method(behaviour, "get_isActiveAndEnabled", 0), c, NULL);
        logf_("pose %s: avatar %p, source %p, type %d, handler %p, enabled %d, active %d", i ? "dst" : "src", avatar, v,
              type, handler, en ? *(uint8_t *)il_object_unbox(en) : -1, ae ? *(uint8_t *)il_object_unbox(ae) : -1);
    }
    void *camera = find_class("UnityEngine", "Camera"), *component = find_class("UnityEngine", "Component");
    void *transform = find_class("UnityEngine", "Transform");
    int none = 0;
    void *args[] = { il_type_get_object(il_class_get_type(camera)), &none };
    void *arr = invoke(ui.find_objects, NULL, args);
    uintptr_t n = arr ? *(uintptr_t *)((char *)arr + 0x18) : 0;
    for (uintptr_t i = 0; i < n; ++i) {
        void *c = ((void **)((char *)arr + 0x20))[i];
        char name[96];
        to_ascii(invoke(method(ui.object, "get_name", 0), c, NULL), name, sizeof name);
        void *mask = invoke(method(camera, "get_cullingMask", 0), c, NULL);
        void *t = invoke(method(component, "get_transform", 0), c, NULL);
        void *pos = t ? invoke(method(transform, "get_position", 0), t, NULL) : NULL;
        float *pv = pos ? il_object_unbox(pos) : NULL;
        logf_("camera %s: cullingMask %#x position (%.2f %.2f %.2f)", name, mask ? *(int *)il_object_unbox(mask) : -1,
              pv ? pv[0] : 0, pv ? pv[1] : 0, pv ? pv[2] : 0);
    }
}

static void vrm_request(void)
{
    vrm_diag();
    if (g_vrm_task) {
        void *k = il_object_get_class(g_vrm_task);
        void *done = invoke(method(k, "get_IsCompleted", 0), g_vrm_task, NULL);
        if (!done || !*(uint8_t *)il_object_unbox(done)) return;
        void *faulted = invoke(method(k, "get_IsFaulted", 0), g_vrm_task, NULL);
        if (faulted && *(uint8_t *)il_object_unbox(faulted)) {
            char text[1024];
            void *ex = invoke(method(k, "get_Exception", 0), g_vrm_task, NULL);
            to_ascii(ex ? invoke(method(il_object_get_class(ex), "ToString", 0), ex, NULL) : NULL, text, sizeof text);
            logf_("vrm: load failed: %s", text);
        } else {
            void *instance = invoke(method(k, "get_Result", 0), g_vrm_task, NULL);
            logf_("vrm: loaded %p", instance);
            if (instance) {
                il_gchandle_new(instance, 0);
                g_vrm_instance = instance;
                vrm_attach(instance);
            }
        }
        g_vrm_task = NULL;
        return;
    }
    char path[512];
    FILE *f = fopen(RUN_DIR "dmhook.vrm", "r");
    if (!f) return;
    if (!fgets(path, sizeof path, f)) path[0] = 0;
    fclose(f);
    DeleteFileA(RUN_DIR "dmhook.vrm");
    path[strcspn(path, "\r\n")] = 0;
    void *vrm10 = find_class("UniVRM10", "Vrm10"), *gen = find_class("UniVRM10", "UrpVrm10MaterialDescriptorGenerator");
    if (!vrm10 || !gen) return;
    void *material = NULL;
    if (GetFileAttributesA(RUN_DIR "dmhook.vrm-urp") != INVALID_FILE_ATTRIBUTES) {
        material = il_object_new(gen);
        invoke(method(gen, ".ctor", 0), material, NULL);
    }
    uint8_t vrm0x = 1, show = 1;
    int rig = 0; /* ControlRigGenerationOption.None */
    uint64_t token = 0;
    void *args[] = { il_string_new(path), &vrm0x, &rig, &show, NULL, material, NULL, NULL, &token };
    g_vrm_task = invoke(method(vrm10, "LoadPathAsync", 9), NULL, args);
    if (g_vrm_task) il_gchandle_new(g_vrm_task, 0);
    logf_("vrm: loading %s, task %p", path, g_vrm_task);
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
    vrm_request();
    tick_bright();

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
        if (waited >= TICK_MS) {
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
