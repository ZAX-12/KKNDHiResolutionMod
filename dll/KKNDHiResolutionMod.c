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
        for (y = 0; y < h; y++)
            memcpy(dst + (size_t)y * dpitch, g_buf + (size_t)y * w, w);
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
// Mouse. The game reads the cursor with GetCursorPos + ScreenToClient (0x4285E8)
// and warps it with ClientToScreen + SetCursorPos (0x428413).
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static BOOL __stdcall my_screen_to_client(HWND hwnd, POINT *pt)
{
    BOOL r = G_SCREEN_TO_CLIENT(hwnd, pt);
    if (g_mode == MODE_MENU) {
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
    if (g_mode == MODE_MENU) {
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
        char path[MAX_PATH], *slash;
        int ok;
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, path, MAX_PATH);
        slash = strrchr(path, '\\');
        if (slash) strcpy(slash + 1, "KKNDHiResolutionMod.log");
        g_log = fopen(path, "w");
        g_t0 = GetTickCount();

        // The buffer swap and the compose step only make sense together.
        ok = !memcmp((void *)0x412419, lock_ptr, sizeof lock_ptr) && !memcmp((void *)0x412452, unlock, sizeof unlock);
        if (ok) {
            patch_call(0x412419, stub_get_buffer, sizeof lock_ptr, lock_ptr);
            patch_call(0x412452, stub_compose, sizeof unlock, unlock);
            patch_call(0x4285E8, my_screen_to_client, sizeof s2c, s2c);
            patch_call(0x428413, my_client_to_screen, sizeof c2s, c2s);
        }
        logf("KKNDHiResolutionMod loaded, hooks %s\n", ok ? "installed" : "NOT installed (unexpected exe)");
    }
    return TRUE;
}
