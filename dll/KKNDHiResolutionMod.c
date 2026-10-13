// KKNDHiResolutionMod.dll - loaded by the patched KKNDgame_hires.exe in place of VERSION.dll
// (the three VERSION.dll functions the game uses are passed through below).
//
// The game draws every frame through one routine (0x4122D0):
//   Lock back buffer -> store pixel pointer/pitch -> run layer callbacks -> Unlock -> Flip.
// We hand the game a private buffer instead of the surface, and before Unlock
// copy it to the real surface:
//   gameplay - unchanged (the battlefield uses the whole screen)
//   menus    - the 640x480 screen is centered, the rest cleared to black
//   videos   - the 640x480 frame is scaled up to the largest size that fits
// The mouse is mapped the same way so menus stay clickable.

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define G_SCREEN_W   (*(int *)0x4798C8)
#define G_SCREEN_H   (*(int *)0x4798B8)
#define G_REAL_PTR   (*(unsigned char **)0x47983C)   // DDSURFACEDESC.lpSurface after Lock
#define G_REAL_PITCH (*(int *)0x479828)              // DDSURFACEDESC.lPitch
#define G_PITCH_PX   (*(int *)0x479810)              // pitch the draw primitives use
#define G_SCREEN_TO_CLIENT (*(BOOL (WINAPI **)(HWND, POINT *))0x4804B4)
#define G_CLIENT_TO_SCREEN (*(BOOL (WINAPI **)(HWND, POINT *))0x4804CC)
#define G_CAMERA_X   (*(int *)0x47C380)              // 24.8 fixed point
#define G_CAMERA_Y   (*(int *)0x47C384)
#define G_VIEW_W     (*(int *)0x478A8C)              // battlefield view size
#define G_VIEW_H     (*(int *)0x478A88)
#define G_SCENE      (*(unsigned char **)0x47A010)   // +8: map layer node (layer object at +0x14)

// Layer draw callbacks (first dword of a layer's type descriptor).
#define LAYER_TILEMAP 0x412860   // desc: +4 tile w, +8 tile h, +0xC cols, +0x10 rows
#define LAYER_BITMAP  0x412A80   // desc: +4 w, +8 h, +0x18 pixels

#define UI_W 640
#define UI_H 480

enum { MODE_GAME, MODE_MENU, MODE_VIDEO };
static const char *mode_name[] = { "gameplay", "menu", "video" };

static unsigned char *g_buf;
static int g_bufW, g_bufH;
static volatile int g_mode = MODE_MENU;
static int g_ox, g_oy;           // menu offset
static int g_vx, g_vy, g_vw, g_vh; // video destination rect
static int g_px, g_py, g_pw, g_ph; // battlefield (playfield) rect on screen
static FILE *g_log;
static DWORD g_t0;

static void logf(const char *fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

// ---------------------------------------------------------------------------
// Called right after Lock: give the game our buffer instead of the surface.
static unsigned char *__cdecl get_buffer(void)
{
    int w = G_SCREEN_W, h = G_SCREEN_H;
    if (!g_buf || w != g_bufW || h != g_bufH) {
        free(g_buf);
        g_buf = (unsigned char *)calloc((size_t)w * h + 64, 1);
        g_bufW = w; g_bufH = h;
    }
    G_PITCH_PX = w;
    return g_buf;
}

// ---------------------------------------------------------------------------
// Decide what kind of frame this is from the layer list.
// Node layout: +0x00 next, +0x14 flags (0xC0000000 = hidden, not drawn),
// +0x18 type descriptor (first dword = draw callback).
static int classify(void *head)
{
    unsigned char *node = *(unsigned char **)head;
    int tilemaps = 0, bitmaps = 0, guard = 0;
    while (node && node != (unsigned char *)head && guard++ < 100000) {
        int *d = *(int **)(node + 0x18);
        unsigned flags = *(unsigned *)(node + 0x14);
        if (d && !(flags & 0xC0000000)) {
            if (d[0] == LAYER_TILEMAP) {
                int w = d[1] * d[3], h = d[2] * d[4];
                // Battle maps are much larger than a screen. UI screens are at most a
                // tile bigger (menus 640x480, briefing 672x512) and the credits
                // scroller is 640x992.
                if (w > 800 || h > 1024) {
                    if (g_mode != MODE_GAME)
                        logf("  tilemap %dx%d (tile %dx%d) at %d,%d flags %08X\n", w, h, d[1], d[2],
                             *(int *)(node + 0x24), *(int *)(node + 0x28), flags);
                    return MODE_GAME;
                }
                tilemaps++;
            } else if (d[0] == LAYER_BITMAP) {
                bitmaps++;
            }
        }
        node = *(unsigned char **)node;
    }
    if (bitmaps && !tilemaps) return MODE_VIDEO;
    return MODE_MENU;
}

static void clear_surface(unsigned char *dst, int pitch, int w, int h)
{
    int y;
    for (y = 0; y < h; y++) memset(dst + (size_t)y * pitch, 0, w);
}

// ---------------------------------------------------------------------------
// Called before Unlock: copy our buffer to the real surface.
static void __cdecl compose(void *head)
{
    unsigned char *dst = G_REAL_PTR;
    int dpitch = G_REAL_PITCH, w = g_bufW, h = g_bufH, y, mode;
    if (!dst || !g_buf) return;

    mode = classify(head);
    if (w <= UI_W && h <= UI_H) mode = MODE_GAME;   // original resolution: nothing to do

    // Battlefield view: the whole screen, or smaller when fit_view_to_map shrank it.
    g_pw = G_VIEW_W & 0xFFFFFF; g_ph = G_VIEW_H & 0xFFFFFF;
    if (g_pw <= 0 || g_pw > w) g_pw = w;
    if (g_ph <= 0 || g_ph > h) g_ph = h;
    g_px = (w - g_pw) / 2;
    g_py = (h - g_ph) / 2;

    g_ox = (w - UI_W) / 2;
    g_oy = (h - UI_H) / 2;
    {
        // Largest 4:3 rect that fits, centered.
        double s = (double)w / UI_W < (double)h / UI_H ? (double)w / UI_W : (double)h / UI_H;
        g_vw = (int)(UI_W * s); g_vh = (int)(UI_H * s);
        g_vx = (w - g_vw) / 2;  g_vy = (h - g_vh) / 2;
    }

    if (mode != g_mode) {
        logf("[%7lu ms] %s\n", GetTickCount() - g_t0, mode_name[mode]);
        g_mode = mode;
    }
    if (mode == MODE_GAME) {
        if (g_pw == w && g_ph == h) {
            for (y = 0; y < h; y++)
                memcpy(dst + (size_t)y * dpitch, g_buf + (size_t)y * w, w);
        } else {
            // Map smaller than the screen: center the playfield, black around it.
            clear_surface(dst, dpitch, w, h);
            for (y = 0; y < g_ph; y++)
                memcpy(dst + (size_t)(y + g_py) * dpitch + g_px, g_buf + (size_t)y * w, g_pw);
        }
    } else if (mode == MODE_MENU) {
        clear_surface(dst, dpitch, w, h);
        for (y = 0; y < UI_H; y++)
            memcpy(dst + (size_t)(y + g_oy) * dpitch + g_ox, g_buf + (size_t)y * w, UI_W);
    } else {
        // Nearest-neighbour scale of the 640x480 frame into the video rect.
        static int xmap[8192];
        int x;
        clear_surface(dst, dpitch, w, h);
        for (x = 0; x < g_vw && x < 8192; x++) xmap[x] = x * UI_W / g_vw;
        for (y = 0; y < g_vh; y++) {
            const unsigned char *src = g_buf + (size_t)(y * UI_H / g_vh) * w;
            unsigned char *row = dst + (size_t)(y + g_vy) * dpitch + g_vx;
            for (x = 0; x < g_vw; x++) row[x] = src[xmap[x]];
        }
    }
}

// ---------------------------------------------------------------------------
// Camera clamp. The game keeps the camera (24.8 fixed point) inside
// [0, (map size - view size) << 8], but when the map is smaller than the view
// that range is negative and several of its scroll routines then leave the
// camera far outside the map - the screen shows mostly empty space. Before each
// frame's screen positions are computed (start of the render driver 0x43B770)
// we clamp it properly. An axis where the map is smaller than the view is
// pinned to 0: the game never had negative camera values, so we don't create any.

#define G_FULL_VIEW_W (*(int *)0x478AA0)          // view size requested at startup (the patched resolution)
#define G_FULL_VIEW_H (*(int *)0x478A84)
#define G_DEFAULT_VP  (*(int **)0x465808)          // default clip viewport: [6] w, [7] h
#define G_CLIP_CACHE  (*(void **)0x479AF8)         // viewport the renderer's clip rect was last set from
// Size of the current battle map in pixels, or 0 if the scene isn't a battle.
static int battle_map_size(int *w, int *h)
{
    __try {
        unsigned char *scene = G_SCENE, *node;
        int *d;
        if (!scene || !(node = *(unsigned char **)(scene + 8))) return 0;
        d = *(int **)(node + 0x18);
        if (!d || d[0] != LAYER_TILEMAP) return 0;
        *w = d[1] * d[3]; *h = d[2] * d[4];
        return *w > 800 || *h > 1024;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Playfield fit. The battlefield view is the full game resolution, and the
// sidebar is created at its right edge. When a map is smaller than that, the
// sidebar ended up beyond the map in empty space. At the mission setup sites the
// view is shrunk to the map, exactly like the original 640x480 game where the map
// always filled the view, and the sidebar is moved with it. compose() then
// centers the playfield on the screen.
static void move_right_edge_ui(int oldW, int newW);

static void __cdecl fit_view_to_map(unsigned site)
{
    int fullW = G_FULL_VIEW_W, fullH = G_FULL_VIEW_H, w = fullW, h = fullH, mapW, mapH;
    if (battle_map_size(&mapW, &mapH)) {
        if (mapW < w) w = mapW;
        if (mapH < h) h = mapH;
    }
    if (w != G_VIEW_W || h != G_VIEW_H) {
        logf("[%7lu ms] view %dx%d -> %dx%d (at %06X)\n", GetTickCount() - g_t0, G_VIEW_W, G_VIEW_H, w, h, site);
        if (w != G_VIEW_W) move_right_edge_ui(G_VIEW_W, w);
        G_VIEW_W = w;
        G_VIEW_H = h;
        __try {
            if (G_DEFAULT_VP) { G_DEFAULT_VP[6] = w; G_DEFAULT_VP[7] = h; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        G_CLIP_CACHE = NULL;   // make the renderer recompute its clip rect
    }
}

// The sidebar is created at mission setup (0x4469F0) before the map is loaded, so it
// is placed at the previous view width: x = width - 32. When the view width changes,
// move the screen-anchored sprites (update callback 0x4483E0, object x at +0x10 in
// 24.8) that sit in that right-edge column by the same amount. They live in the
// render driver's two lists (head at 0x47C5D8 and 0x47C5E8; node +0x0C object,
// +0x10 update callback).
static void move_right_edge_ui(int oldW, int newW)
{
    static const unsigned heads[] = { 0x47C5D8, 0x47C5E8 };
    int i, moved = 0;
    for (i = 0; i < 2; i++) {
        __try {
            unsigned char *head = (unsigned char *)heads[i], *node = *(unsigned char **)head;
            int guard = 0;
            while (node && node != head && guard++ < 20000) {
                unsigned char *obj = *(unsigned char **)(node + 0x0C);
                if (*(unsigned *)(node + 0x10) == 0x4483E0 && obj) {
                    int x = *(int *)(obj + 0x10) >> 8;
                    if (x >= oldW - 0x20 && x < oldW) {
                        *(int *)(obj + 0x10) += (newW - oldW) << 8;
                        moved++;
                    }
                }
                node = *(unsigned char **)node;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    logf("  moved %d sidebar sprites by %d\n", moved, newW - oldW);
}

static int clamp_axis(int cam, int map, int view)
{
    int max = map > view ? (map - view) << 8 : 0;
    return cam < 0 ? 0 : cam > max ? max : cam;
}

static void __cdecl clamp_camera(void)
{
    static int lastMapW, lastMapH;
    int mapW, mapH, viewW, viewH, x, y;
    if (!battle_map_size(&mapW, &mapH)) return;
    {
        viewW = G_VIEW_W & 0xFFFFFF; viewH = G_VIEW_H & 0xFFFFFF;
        x = clamp_axis(G_CAMERA_X, mapW, viewW);
        y = clamp_axis(G_CAMERA_Y, mapH, viewH);
        if (mapW != lastMapW || mapH != lastMapH) {
            logf("[%7lu ms] map %dx%d, view %dx%d, camera (%d,%d) -> (%d,%d)\n", GetTickCount() - g_t0,
                 mapW, mapH, viewW, viewH, G_CAMERA_X >> 8, G_CAMERA_Y >> 8, x >> 8, y >> 8);
            lastMapW = mapW; lastMapH = mapH;
        }
        G_CAMERA_X = x;
        G_CAMERA_Y = y;
    }
}

// ---------------------------------------------------------------------------
// Mouse. The game reads the cursor with GetCursorPos + ScreenToClient (0x4285E8)
// and warps it with ClientToScreen + SetCursorPos (0x428413).
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static BOOL __stdcall my_screen_to_client(HWND hwnd, POINT *pt)
{
    BOOL r = G_SCREEN_TO_CLIENT(hwnd, pt);
    if (g_mode == MODE_GAME) {
        if (g_pw < g_bufW || g_ph < g_bufH) {
            pt->x = clampi(pt->x - g_px, 0, g_pw - 1);
            pt->y = clampi(pt->y - g_py, 0, g_ph - 1);
        }
    } else if (g_mode == MODE_MENU) {
        pt->x = clampi(pt->x - g_ox, 0, UI_W - 1);
        pt->y = clampi(pt->y - g_oy, 0, UI_H - 1);
    } else if (g_mode == MODE_VIDEO && g_vw > 0 && g_vh > 0) {
        pt->x = clampi((pt->x - g_vx) * UI_W / g_vw, 0, UI_W - 1);
        pt->y = clampi((pt->y - g_vy) * UI_H / g_vh, 0, UI_H - 1);
    }
    return r;
}

static BOOL __stdcall my_client_to_screen(HWND hwnd, POINT *pt)
{
    if (g_mode == MODE_GAME) {
        pt->x += g_px;
        pt->y += g_py;
    } else if (g_mode == MODE_MENU) {
        pt->x += g_ox;
        pt->y += g_oy;
    } else if (g_mode == MODE_VIDEO && g_vw > 0 && g_vh > 0) {
        pt->x = g_vx + pt->x * g_vw / UI_W;
        pt->y = g_vy + pt->y * g_vh / UI_H;
    }
    return G_CLIENT_TO_SCREEN(hwnd, pt);
}

// ---------------------------------------------------------------------------
// Naked stubs called from the patched instructions.
__declspec(naked) static void stub_get_buffer(void)   // replaces: mov edx, [0x47983C]
{
    __asm {
        pushad
        call get_buffer
        mov [esp + 20], eax      // saved EDX slot
        popad
        ret
    }
}

__declspec(naked) static void stub_compose(void)      // replaces: mov eax, [0x4798D8]
{
    __asm {
        pushad
        push ebp                 // layer list head
        call compose
        add esp, 4
        popad
        mov eax, dword ptr ds:[0x4798D8]
        ret
    }
}

__declspec(naked) static void stub_render(void)       // replaces: call 0x412190 (start of 0x43B770)
{
    __asm {
        pushad
        call clamp_camera
        popad
        mov eax, 0x412190        // continue into the original call; it returns to 0x43B775
        jmp eax
    }
}

// Mission setup / sidebar creation sites that read the view width: fit the view
// to the map first, then do the original load.
#define FIT_STUB(name, site, reg)                 \
    __declspec(naked) static void name(void)      \
    {                                             \
        __asm pushad                              \
        __asm push site                           \
        __asm call fit_view_to_map                \
        __asm add esp, 4                          \
        __asm popad                               \
        __asm mov reg, dword ptr ds:[0x478A8C]    \
        __asm ret                                 \
    }
FIT_STUB(stub_fit_422A47, 0x422A47, eax)
FIT_STUB(stub_fit_422CA2, 0x422CA2, eax)
FIT_STUB(stub_fit_42327E, 0x42327E, ecx)
FIT_STUB(stub_fit_446ED0, 0x446ED0, eax)
FIT_STUB(stub_fit_446D18, 0x446D18, edx)

// ---------------------------------------------------------------------------
// VERSION.dll replacements: the game imports these three; pass them through.
static FARPROC real_version(const char *name)
{
    static HMODULE mod;
    if (!mod) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strcat(path, "\\version.dll");
        mod = LoadLibraryA(path);
    }
    return mod ? GetProcAddress(mod, name) : NULL;
}

DWORD WINAPI K_GetFileVersionInfoSizeA(LPCSTR file, LPDWORD handle)
{
    DWORD (WINAPI *f)(LPCSTR, LPDWORD) = (DWORD (WINAPI *)(LPCSTR, LPDWORD))real_version("GetFileVersionInfoSizeA");
    return f ? f(file, handle) : 0;
}

BOOL WINAPI K_GetFileVersionInfoA(LPCSTR file, DWORD handle, DWORD len, LPVOID data)
{
    BOOL (WINAPI *f)(LPCSTR, DWORD, DWORD, LPVOID) = (BOOL (WINAPI *)(LPCSTR, DWORD, DWORD, LPVOID))real_version("GetFileVersionInfoA");
    return f ? f(file, handle, len, data) : FALSE;
}

BOOL WINAPI K_VerQueryValueA(LPCVOID block, LPCSTR sub, LPVOID *buf, PUINT len)
{
    BOOL (WINAPI *f)(LPCVOID, LPCSTR, LPVOID *, PUINT) = (BOOL (WINAPI *)(LPCVOID, LPCSTR, LPVOID *, PUINT))real_version("VerQueryValueA");
    return f ? f(block, sub, buf, len) : FALSE;
}

// ---------------------------------------------------------------------------
static int patch_call(DWORD at, void *target, int len, const unsigned char *expect)
{
    DWORD old;
    unsigned char *p = (unsigned char *)at;
    int i;
    if (memcmp(p, expect, len)) { logf("patch at %08lX: unexpected bytes, skipped\n", at); return 0; }
    VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE8;
    *(DWORD *)(p + 1) = (DWORD)target - (at + 5);
    for (i = 5; i < len; i++) p[i] = 0x90;
    VirtualProtect(p, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        static const unsigned char lock_ptr[] = { 0x8B, 0x15, 0x3C, 0x98, 0x47, 0x00 }; // mov edx, [0x47983C]
        static const unsigned char unlock[]   = { 0xA1, 0xD8, 0x98, 0x47, 0x00 };       // mov eax, [0x4798D8]
        static const unsigned char s2c[]      = { 0xFF, 0x15, 0xB4, 0x04, 0x48, 0x00 }; // call [ScreenToClient]
        static const unsigned char c2s[]      = { 0xFF, 0x15, 0xCC, 0x04, 0x48, 0x00 }; // call [ClientToScreen]
        static const unsigned char render[]   = { 0xE8, 0x1B, 0x6A, 0xFD, 0xFF };       // call 0x412190 @ 0x43B770
        static const unsigned char mov_eax_view[] = { 0xA1, 0x8C, 0x8A, 0x47, 0x00 };   // mov eax, [0x478A8C]
        static const unsigned char mov_ecx_view[] = { 0x8B, 0x0D, 0x8C, 0x8A, 0x47, 0x00 };
        static const unsigned char mov_edx_view[] = { 0x8B, 0x15, 0x8C, 0x8A, 0x47, 0x00 };
        char path[MAX_PATH], *slash;
        int ok, center;
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, path, MAX_PATH);
        slash = strrchr(path, '\\');
        if (slash) strcpy(slash + 1, "KKNDHiResolutionMod.log");
        g_log = fopen(path, "w");
        g_t0 = GetTickCount();

        // Settings written by the launcher after our import name: "KHRM", flags (bit 0 = center menus).
        center = !(memcmp((void *)0x480EB0, "KHRM", 4) == 0 && !(*(unsigned *)0x480EB4 & 1));

        // The buffer swap and the compose step only make sense together.
        ok = !memcmp((void *)0x412419, lock_ptr, sizeof lock_ptr) && !memcmp((void *)0x412452, unlock, sizeof unlock);
        if (ok && center) {
            patch_call(0x412419, stub_get_buffer, sizeof lock_ptr, lock_ptr);
            patch_call(0x412452, stub_compose, sizeof unlock, unlock);
            patch_call(0x4285E8, my_screen_to_client, sizeof s2c, s2c);
            patch_call(0x428413, my_client_to_screen, sizeof c2s, c2s);

            // Fit the battlefield view to small maps. Needs compose() to center it
            // and clear around it, so it is part of the same option.
            patch_call(0x422A47, stub_fit_422A47, 5, mov_eax_view);
            patch_call(0x422CA2, stub_fit_422CA2, 5, mov_eax_view);
            patch_call(0x42327E, stub_fit_42327E, 6, mov_ecx_view);
            patch_call(0x446ED0, stub_fit_446ED0, 5, mov_eax_view);
            patch_call(0x446D18, stub_fit_446D18, 6, mov_edx_view);
        }
        // Independent of the menu/video hooks: only touches the camera in battles.
        patch_call(0x43B770, stub_render, sizeof render, render);
        logf("KKNDHiResolutionMod loaded, menu/video hooks %s\n",
             !ok ? "NOT installed (unexpected exe)" : center ? "installed" : "off (disabled in launcher)");
    }
    return TRUE;
}
