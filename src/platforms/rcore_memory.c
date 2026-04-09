/**********************************************************************************************
*
*   rcore_memory - Functions to manage window, graphics device and inputs
*
*   PLATFORM: MEMORY (No OS)
*       - Memory framebuffer output (no os)
*
*   LIMITATIONS:
*       - Software renderer (rlsw)
*       - No input system
*
*   POSSIBLE IMPROVEMENTS:
*       - Improvement 01
*       - Improvement 02
*
*   CONFIGURATION:
*       #define RCORE_PLATFORM_CUSTOM_FLAG
*           Custom flag for rcore on target platform -not used-
*
*   DEPENDENCIES:
*       - rlsw: Software renderer
*       - gestures: Gestures system for touch-ready devices (or simulated from mouse inputs)
*
*
*   LICENSE: zlib/libpng
*
*   Copyright (c) 2025-2026 Ramon Santamaria (@raysan5) and contributors
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#if defined(_WIN32)
    #include <conio.h>              // Required for: kbhit()
#else
    // Provide kbhit() function in non-Windows platforms
    #include <termios.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#if defined(__linux__)
    #include <linux/input.h>
    #include <linux/fb.h>
    #include <sys/ioctl.h>
    #include <sys/mman.h>
    #include <stdio.h>
    #include <string.h>
    #include <stdlib.h>
    #include <stdint.h>
    #include <dlfcn.h>
#endif

#if defined(RAYLIB_USE_TINYGL)
    #include "zbuffer.h"    // TinyGL ZBuffer for direct framebuffer access
#endif

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------
// Platform-specific required data for timing (Win32)
#if defined(_WIN32)
typedef struct _LARGE_INTEGER { int64_t QuadPart; } LARGE_INTEGER;
__declspec(dllimport) int __stdcall QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);
#endif

typedef struct {
    unsigned int *pixels;   // Pointer to pixel data buffer (RGBA8888 or ARGB8888 format)
#if defined(RAYLIB_USE_TINYGL)
    ZBuffer *tglZBuffer;    // TinyGL framebuffer; pixels rendered here in ARGB8888
#endif
#if defined(__linux__)
    int fbFd;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    unsigned char *fbp;
    size_t fbSize;
    int fbBuffers;
    int fbBufferIndex;
    int renderWidth;
    int renderHeight;
    int inputFds[8];
    int inputFdCount;
#endif
#if defined(_WIN32)
    LARGE_INTEGER timerFrequency;
#endif
#if defined(__linux__) && defined(RAYLIB_MMF_FB)
    struct
    {
        bool enabled;
        void *libGfx;
        void *libSys;
        int renderWidth;
        int renderHeight;
        uint64_t fbPhy;
        void *fbVir;
        uint64_t srcPhy;
        void *srcVir;
        size_t srcSize;
        int (*MI_SYS_Init)(void);
        int (*MI_SYS_Exit)(void);
        int (*MI_SYS_MMA_Alloc)(unsigned char *heapName, unsigned int size, uint64_t *phyAddr);
        int (*MI_SYS_MMA_Free)(uint64_t phyAddr);
        int (*MI_SYS_Mmap)(uint64_t phyAddr, unsigned int size, void **virtAddr, unsigned char cache);
        int (*MI_SYS_Munmap)(void *virtAddr, unsigned int size);
        int (*MI_SYS_FlushInvCache)(void *virtAddr, unsigned int size);
        int (*MI_SYS_MemsetPa)(uint64_t phyAddr, unsigned int value, unsigned int length);
        int (*MI_GFX_Open)(void);
        int (*MI_GFX_Close)(void);
        int (*MI_GFX_BitBlit)(void *src, void *srcRect, void *dst, void *dstRect, void *opt, unsigned short *fence);
        int (*MI_GFX_WaitAllDone)(unsigned char waitAllDone, unsigned short fence);
    } mi;
#endif
} PlatformData;

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
extern CoreData CORE;                   // Global CORE state context

static PlatformData platform = { 0 };   // Platform specific data

//----------------------------------------------------------------------------------
// Module Internal Functions Declaration
//----------------------------------------------------------------------------------
int InitPlatform(void);                 // Initialize platform (graphics, inputs and more)
bool InitGraphicsDevice(void);          // Initialize graphics device

#if defined(__linux__) && defined(RAYLIB_MMF_FB)
typedef unsigned char MI_BOOL;
typedef unsigned char MI_U8;
typedef unsigned short MI_U16;
typedef unsigned int MI_U32;
typedef unsigned long long MI_U64;
typedef signed int MI_S32;
typedef unsigned long long MI_PHY;

typedef enum
{
    E_MI_GFX_FMT_I1 = 0,
    E_MI_GFX_FMT_I2 = 1,
    E_MI_GFX_FMT_I4 = 2,
    E_MI_GFX_FMT_I8 = 3,
    E_MI_GFX_FMT_FABAFGBG2266 = 4,
    E_MI_GFX_FMT_1ABFGBG12355 = 5,
    E_MI_GFX_FMT_RGB565 = 6,
    E_MI_GFX_FMT_ARGB1555 = 7,
    E_MI_GFX_FMT_ARGB4444 = 8,
    E_MI_GFX_FMT_ARGB1555_DST = 9,
    E_MI_GFX_FMT_YUV422 = 10,
    E_MI_GFX_FMT_ARGB8888 = 11,
    E_MI_GFX_FMT_RGBA5551 = 12,
    E_MI_GFX_FMT_RGBA4444 = 13,
    E_MI_GFX_FMT_ABGR8888 = 14,
    E_MI_GFX_FMT_BGRA5551 = 15,
    E_MI_GFX_FMT_ABGR1555 = 16,
    E_MI_GFX_FMT_ABGR4444 = 17,
    E_MI_GFX_FMT_BGRA4444 = 18,
    E_MI_GFX_FMT_BGR565 = 19,
    E_MI_GFX_FMT_RGBA8888 = 20,
    E_MI_GFX_FMT_BGRA8888 = 21
} MI_GFX_ColorFmt_e;

typedef enum
{
    E_MI_GFX_RGB_OP_EQUAL = 0,
    E_MI_GFX_RGB_OP_NOT_EQUAL,
    E_MI_GFX_ALPHA_OP_EQUAL,
    E_MI_GFX_ALPHA_OP_NOT_EQUAL,
    E_MI_GFX_ARGB_OP_EQUAL,
    E_MI_GFX_ARGB_OP_NOT_EQUAL
} MI_GFX_ColorKeyOp_e;

typedef enum
{
    E_MI_GFX_DFB_BLD_ZERO = 0,
    E_MI_GFX_DFB_BLD_ONE = 1
} MI_GFX_DfbBldOp_e;

typedef enum
{
    E_MI_GFX_MIRROR_NONE = 0,
    E_MI_GFX_MIRROR_HORIZONTAL = 1,
    E_MI_GFX_MIRROR_VERTICAL = 2,
    E_MI_GFX_MIRROR_BOTH = 3
} MI_GFX_Mirror_e;

typedef enum
{
    E_MI_GFX_ROTATE_0 = 0,
    E_MI_GFX_ROTATE_90 = 1,
    E_MI_GFX_ROTATE_180 = 2,
    E_MI_GFX_ROTATE_270 = 3
} MI_GFX_Rotate_e;

typedef enum
{
    E_MI_GFX_DFB_BLEND_NOFX = 0x00000000
} MI_Gfx_DfbBlendFlags_e;

typedef struct
{
    MI_S32 s32Xpos;
    MI_S32 s32Ypos;
    MI_U32 u32Width;
    MI_U32 u32Height;
} MI_GFX_Rect_t;

typedef struct
{
    MI_U32 u32ColorStart;
    MI_U32 u32ColorEnd;
} MI_GFX_ColorKeyValue_t;

typedef struct
{
    MI_BOOL bEnColorKey;
    MI_GFX_ColorKeyOp_e eCKeyOp;
    MI_GFX_ColorFmt_e eCKeyFmt;
    MI_GFX_ColorKeyValue_t stCKeyVal;
} MI_GFX_ColorKeyInfo_t;

typedef struct
{
    MI_PHY phyAddr;
    MI_GFX_ColorFmt_e eColorFmt;
    MI_U32 u32Width;
    MI_U32 u32Height;
    MI_U32 u32Stride;
} MI_GFX_Surface_t;

typedef struct
{
    MI_GFX_Rect_t stClipRect;
    MI_GFX_ColorKeyInfo_t stSrcColorKeyInfo;
    MI_GFX_ColorKeyInfo_t stDstColorKeyInfo;
    MI_GFX_DfbBldOp_e eSrcDfbBldOp;
    MI_GFX_DfbBldOp_e eDstDfbBldOp;
    MI_GFX_Mirror_e eMirror;
    MI_GFX_Rotate_e eRotate;
    MI_Gfx_DfbBlendFlags_e eDFBBlendFlag;
    MI_U32 u32GlobalSrcConstColor;
    MI_U32 u32GlobalDstConstColor;
} MI_GFX_Opt_t;

static int MMF_GetEnvInt(const char *name, int defaultValue);
static bool MMF_GetEnvBool(const char *name, bool defaultValue);
static bool MMF_InitMiGfx(int renderWidth, int renderHeight);
static void MMF_ShutdownMiGfx(void);
#endif

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
// NOTE: Functions declaration is provided by raylib.h

//----------------------------------------------------------------------------------
// Module Internal Functions Declaration
//----------------------------------------------------------------------------------
#if !defined(_WIN32)
static int kbhit(void);                         // Check if a key has been pressed
static char getch(void) { return getchar(); }   // Get pressed character
#endif

//----------------------------------------------------------------------------------
// Module Functions Definition: Window and Graphics Device
//----------------------------------------------------------------------------------

// Check if application should close
bool WindowShouldClose(void)
{
    if (CORE.Window.ready) return CORE.Window.shouldClose;
    else return true;
}

// Toggle fullscreen mode
void ToggleFullscreen(void)
{
    TRACELOG(LOG_WARNING, "ToggleFullscreen() not available on target platform");
}

// Toggle borderless windowed mode
void ToggleBorderlessWindowed(void)
{
    TRACELOG(LOG_WARNING, "ToggleBorderlessWindowed() not available on target platform");
}

// Set window state: maximized, if resizable
void MaximizeWindow(void)
{
    TRACELOG(LOG_WARNING, "MaximizeWindow() not available on target platform");
}

// Set window state: minimized
void MinimizeWindow(void)
{
    TRACELOG(LOG_WARNING, "MinimizeWindow() not available on target platform");
}

// Restore window from being minimized/maximized
void RestoreWindow(void)
{
    TRACELOG(LOG_WARNING, "RestoreWindow() not available on target platform");
}

// Set window configuration state using flags
void SetWindowState(unsigned int flags)
{
    TRACELOG(LOG_WARNING, "SetWindowState() not available on target platform");
}

// Clear window configuration state flags
void ClearWindowState(unsigned int flags)
{
    TRACELOG(LOG_WARNING, "ClearWindowState() not available on target platform");
}

// Set icon for window
void SetWindowIcon(Image image)
{
    TRACELOG(LOG_WARNING, "SetWindowIcon() not available on target platform");
}

// Set icon for window
void SetWindowIcons(Image *images, int count)
{
    TRACELOG(LOG_WARNING, "SetWindowIcons() not available on target platform");
}

// Set title for window
void SetWindowTitle(const char *title)
{
    CORE.Window.title = title;
}

// Set window position on screen (windowed mode)
void SetWindowPosition(int x, int y)
{
    TRACELOG(LOG_WARNING, "SetWindowPosition() not available on target platform");
}

// Set monitor for the current window
void SetWindowMonitor(int monitor)
{
    TRACELOG(LOG_WARNING, "SetWindowMonitor() not available on target platform");
}

// Set window minimum dimensions (FLAG_WINDOW_RESIZABLE)
void SetWindowMinSize(int width, int height)
{
    CORE.Window.screenMin.width = width;
    CORE.Window.screenMin.height = height;
}

// Set window maximum dimensions (FLAG_WINDOW_RESIZABLE)
void SetWindowMaxSize(int width, int height)
{
    CORE.Window.screenMax.width = width;
    CORE.Window.screenMax.height = height;
}

// Set window dimensions
void SetWindowSize(int width, int height)
{
    TRACELOG(LOG_WARNING, "SetWindowSize() not available on target platform");
}

// Set window opacity, value opacity is between 0.0 and 1.0
void SetWindowOpacity(float opacity)
{
    TRACELOG(LOG_WARNING, "SetWindowOpacity() not available on target platform");
}

// Set window focused
void SetWindowFocused(void)
{
    TRACELOG(LOG_WARNING, "SetWindowFocused() not available on target platform");
}

// Get native window handle
void *GetWindowHandle(void)
{
    TRACELOG(LOG_WARNING, "GetWindowHandle() not implemented on target platform");
    return NULL;
}

// Get number of monitors
int GetMonitorCount(void)
{
    TRACELOG(LOG_WARNING, "GetMonitorCount() not implemented on target platform");
    return 1;
}

// Get current monitor where window is placed
int GetCurrentMonitor(void)
{
    TRACELOG(LOG_WARNING, "GetCurrentMonitor() not implemented on target platform");
    return 0;
}

// Get selected monitor position
Vector2 GetMonitorPosition(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorPosition() not implemented on target platform");
    return (Vector2){ 0, 0 };
}

// Get selected monitor width (currently used by monitor)
int GetMonitorWidth(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorWidth() not implemented on target platform");
    return 0;
}

// Get selected monitor height (currently used by monitor)
int GetMonitorHeight(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorHeight() not implemented on target platform");
    return 0;
}

// Get selected monitor physical width in millimetres
int GetMonitorPhysicalWidth(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorPhysicalWidth() not implemented on target platform");
    return 0;
}

// Get selected monitor physical height in millimetres
int GetMonitorPhysicalHeight(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorPhysicalHeight() not implemented on target platform");
    return 0;
}

// Get selected monitor refresh rate
int GetMonitorRefreshRate(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorRefreshRate() not implemented on target platform");
    return 0;
}

// Get the human-readable, UTF-8 encoded name of the selected monitor
const char *GetMonitorName(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorName() not implemented on target platform");
    return "";
}

// Get window position XY on monitor
Vector2 GetWindowPosition(void)
{
    TRACELOG(LOG_WARNING, "GetWindowPosition() not implemented on target platform");
    return (Vector2){ 0, 0 };
}

// Get window scale DPI factor for current monitor
Vector2 GetWindowScaleDPI(void)
{
    TRACELOG(LOG_WARNING, "GetWindowScaleDPI() not implemented on target platform");
    return (Vector2){ 1.0f, 1.0f };
}

// Set clipboard text content
void SetClipboardText(const char *text)
{
    TRACELOG(LOG_WARNING, "SetClipboardText() not implemented on target platform");
}

// Get clipboard text content
// NOTE: returned string is allocated and freed by GLFW
const char *GetClipboardText(void)
{
    TRACELOG(LOG_WARNING, "GetClipboardText() not implemented on target platform");
    return NULL;
}

// Get clipboard image
Image GetClipboardImage(void)
{
    Image image = { 0 };

    TRACELOG(LOG_WARNING, "GetClipboardImage() not implemented on target platform");

    return image;
}

// Show mouse cursor
void ShowCursor(void)
{
    CORE.Input.Mouse.cursorHidden = false;
}

// Hides mouse cursor
void HideCursor(void)
{
    CORE.Input.Mouse.cursorHidden = true;
}

// Enables cursor (unlock cursor)
void EnableCursor(void)
{
    // Set cursor position in the middle
    SetMousePosition(CORE.Window.screen.width/2, CORE.Window.screen.height/2);

    CORE.Input.Mouse.cursorHidden = false;
}

// Disables cursor (lock cursor)
void DisableCursor(void)
{
    // Set cursor position in the middle
    SetMousePosition(CORE.Window.screen.width/2, CORE.Window.screen.height/2);

    CORE.Input.Mouse.cursorHidden = true;
}

// Swap back buffer with front buffer (screen drawing)
void SwapScreenBuffer(void)
{
    // TinyGL renders directly into platform.pixels via the ZBuffer — no readback needed

#if defined(__linux__) && defined(RAYLIB_MMF_FB)
    if (platform.mi.enabled)
    {
        MI_GFX_Surface_t src = { 0 };
        MI_GFX_Surface_t dst = { 0 };
        MI_GFX_Rect_t srcRect = { 0 };
        MI_GFX_Rect_t dstRect = { 0 };
        MI_GFX_Opt_t opt;
        unsigned short fence = 0;

        memset(&opt, 0, sizeof(opt));

        srcRect.s32Xpos = 0;
        srcRect.s32Ypos = 0;
        srcRect.u32Width = (MI_U32)platform.mi.renderWidth;
        srcRect.u32Height = (MI_U32)platform.mi.renderHeight;

        dstRect.s32Xpos = 0;
        dstRect.s32Ypos = 0;
        dstRect.u32Width = (MI_U32)platform.vinfo.xres;
        dstRect.u32Height = (MI_U32)platform.vinfo.yres;

        src.phyAddr = platform.mi.srcPhy;
        // TinyGL stores pixels as ARGB8888 (matching the framebuffer's channel layout).
        // No channel swap needed; MI GFX passes it through as-is.
        src.eColorFmt = E_MI_GFX_FMT_ARGB8888;
        src.u32Width = (MI_U32)platform.mi.renderWidth;
        src.u32Height = (MI_U32)platform.mi.renderHeight;
        src.u32Stride = (MI_U32)(platform.mi.renderWidth * 4);

        int bufferIndex = platform.fbBufferIndex;
        int yoff = bufferIndex * platform.vinfo.yres;
        uint64_t dstPhy = platform.mi.fbPhy + (uint64_t)platform.finfo.line_length * (uint64_t)yoff;

        dst.phyAddr = dstPhy;
        dst.eColorFmt = E_MI_GFX_FMT_ARGB8888;
        dst.u32Width = (MI_U32)platform.vinfo.xres;
        dst.u32Height = (MI_U32)platform.vinfo.yres;
        dst.u32Stride = (MI_U32)platform.finfo.line_length;

        opt.eSrcDfbBldOp = E_MI_GFX_DFB_BLD_ONE;
        opt.eDstDfbBldOp = E_MI_GFX_DFB_BLD_ZERO;
        opt.eDFBBlendFlag = E_MI_GFX_DFB_BLEND_NOFX;
        opt.eMirror = E_MI_GFX_MIRROR_NONE;
        opt.eRotate = E_MI_GFX_ROTATE_180;

        platform.mi.MI_SYS_FlushInvCache(platform.mi.srcVir, (unsigned int)platform.mi.srcSize);
        platform.mi.MI_GFX_BitBlit(&src, &srcRect, &dst, &dstRect, &opt, &fence);
        platform.mi.MI_GFX_WaitAllDone(1, fence);

        if (platform.fbBuffers > 1)
        {
            platform.vinfo.yoffset = yoff;
            if (ioctl(platform.fbFd, FBIOPAN_DISPLAY, &platform.vinfo) < 0)
            {
                // Ignore pan errors; still better than nothing
            }
            platform.fbBufferIndex = (platform.fbBufferIndex + 1) % platform.fbBuffers;
        }
        return;
    }

    if ((platform.fbp != NULL) && (platform.fbSize > 0))
    {
        int width = CORE.Window.render.width;
        int height = CORE.Window.render.height;
        int bpp = platform.vinfo.bits_per_pixel;
        int bytesPerPixel = bpp/8;

        int bufferIndex = platform.fbBufferIndex;
        int yoff = bufferIndex * height;

        for (int y = 0; y < height; y++)
        {
            unsigned char *row = platform.fbp + (yoff + y) * platform.finfo.line_length;
            unsigned int *src = platform.pixels + (height - 1 - y) * width;

            for (int x = 0; x < width; x++)
            {
                unsigned int px = src[width - 1 - x];
#if defined(RAYLIB_USE_TINYGL)
                // TinyGL pixel format: ARGB8888 (B in low byte, R in byte 2)
                unsigned char r = (unsigned char)((px >> 16) & 0xFF);
                unsigned char g = (unsigned char)((px >> 8) & 0xFF);
                unsigned char b = (unsigned char)(px & 0xFF);
#else
                // rlsw has SW_FRAMEBUFFER_OUTPUT_BGRA=true (the default): byte layout is [B,G,R,A]
                unsigned char b = (unsigned char)(px & 0xFF);
                unsigned char g = (unsigned char)((px >> 8) & 0xFF);
                unsigned char r = (unsigned char)((px >> 16) & 0xFF);
#endif

                unsigned int packed = 0;
                packed |= (r >> (8 - platform.vinfo.red.length)) << platform.vinfo.red.offset;
                packed |= (g >> (8 - platform.vinfo.green.length)) << platform.vinfo.green.offset;
                packed |= (b >> (8 - platform.vinfo.blue.length)) << platform.vinfo.blue.offset;

                memcpy(row + x * bytesPerPixel, &packed, bytesPerPixel);
            }
        }

        // Pan to the drawn buffer
        if (platform.fbBuffers > 1)
        {
            platform.vinfo.yoffset = yoff;
            if (ioctl(platform.fbFd, FBIOPAN_DISPLAY, &platform.vinfo) < 0)
            {
                // Ignore pan errors; still better than nothing
            }
            platform.fbBufferIndex = (platform.fbBufferIndex + 1) % platform.fbBuffers;
        }
    }
#endif
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Misc
//----------------------------------------------------------------------------------

// Get elapsed time measure in seconds since InitTimer()
double GetTime(void)
{
    double time = 0.0;
#if defined(_WIN32)
    LARGE_INTEGER now = { 0 };
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - CORE.Time.base)/(double)platform.timerFrequency.QuadPart;
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__EMSCRIPTEN__)
    struct timespec ts = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long int nanoSeconds = (unsigned long long int)ts.tv_sec*1000000000LLU + (unsigned long long int)ts.tv_nsec;
    time = (double)(nanoSeconds - CORE.Time.base)*1e-9;  // Elapsed time since InitTimer()
#endif
    return time;
}

// Open URL with default system browser (if available)
// NOTE: This function is only safe to use if you control the URL given.
// A user could craft a malicious string performing another action.
// Only call this function yourself not with user input or make sure to check the string yourself.
// REF: https://github.com/raysan5/raylib/issues/686
void OpenURL(const char *url)
{
    // Security check to (partially) avoid malicious code on target platform
    if (strchr(url, '\'') != NULL) TRACELOG(LOG_WARNING, "SYSTEM: Provided URL could be potentially malicious, avoid [\'] character");
    else
    {
        char *cmd = (char *)RL_CALLOC(strlen(url) + 32, sizeof(char));
        sprintf(cmd, "explorer \"%s\"", url);
        int result = system(cmd);
        if (result == -1) TRACELOG(LOG_WARNING, "OpenURL() child process could not be created");
        RL_FREE(cmd);
    }
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Inputs
//----------------------------------------------------------------------------------

// Set internal gamepad mappings
int SetGamepadMappings(const char *mappings)
{
    TRACELOG(LOG_WARNING, "SetGamepadMappings() not implemented on target platform");
    return 0;
}

// Set gamepad vibration
void SetGamepadVibration(int gamepad, float leftMotor, float rightMotor, float duration)
{
    TRACELOG(LOG_WARNING, "SetGamepadVibration() not implemented on target platform");
}

// Set mouse position XY
void SetMousePosition(int x, int y)
{
    CORE.Input.Mouse.currentPosition = (Vector2){ (float)x, (float)y };
    CORE.Input.Mouse.previousPosition = CORE.Input.Mouse.currentPosition;
}

// Set mouse cursor
void SetMouseCursor(int cursor)
{
    TRACELOG(LOG_WARNING, "SetMouseCursor() not implemented on target platform");
}

// Get physical key name
const char *GetKeyName(int key)
{
    TRACELOG(LOG_WARNING, "GetKeyName() not implemented on target platform");
    return "";
}

// Register all input events
void PollInputEvents(void)
{
#if SUPPORT_GESTURES_SYSTEM
    // NOTE: Gestures update must be called every frame to reset gestures correctly
    // because ProcessGestureEvent() is called on an event, not every frame
    UpdateGestures();
#endif

    // Reset keys/chars pressed registered
    CORE.Input.Keyboard.keyPressedQueueCount = 0;
    CORE.Input.Keyboard.charPressedQueueCount = 0;

    // Reset key repeats
    for (int i = 0; i < MAX_KEYBOARD_KEYS; i++) CORE.Input.Keyboard.keyRepeatInFrame[i] = 0;

    // Reset last gamepad button/axis registered state
    CORE.Input.Gamepad.lastButtonPressed = 0; // GAMEPAD_BUTTON_UNKNOWN
    //CORE.Input.Gamepad.axisCount = 0;

    // Register previous touch states
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) CORE.Input.Touch.previousTouchState[i] = CORE.Input.Touch.currentTouchState[i];

    // Reset touch positions
    // TODO: It resets on target platform the mouse position and not filled again until a move-event,
    // so, if mouse is not moved it returns a (0, 0) position... this behaviour should be reviewed!
    //for (int i = 0; i < MAX_TOUCH_POINTS; i++) CORE.Input.Touch.position[i] = (Vector2){ 0, 0 };

    // Register previous keys states
    // NOTE: Android supports up to 260 keys
    for (int i = 0; i < 260; i++)
    {
        CORE.Input.Keyboard.previousKeyState[i] = CORE.Input.Keyboard.currentKeyState[i];
        CORE.Input.Keyboard.keyRepeatInFrame[i] = 0;
    }

    // TODO: Poll input events for current platform

#if defined(__linux__)
    for (int i = 0; i < platform.inputFdCount; i++)
    {
        struct input_event ev;
        while (read(platform.inputFds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev))
        {
            if (ev.type == EV_KEY)
            {
                // Translate Linux evdev keycode → raylib key constant.
                // MMF button mapping (verified via evdev probe):
                //   D-Pad: 103=UP 108=DOWN 105=LEFT 106=RIGHT
                //   A=57(SPACE) B=29(LCTRL) X=42(LSHIFT) Y=56(LALT)
                //   L1=18(E) L2=15(TAB) R1=20(T) R2=14(BACKSPACE)
                //   Start=28(ENTER) Select=97(RCTRL) Menu=1(ESC) Power=116
                int raylibKey = 0;
                switch (ev.code)
                {
                    case 103: raylibKey = 265; break; // KEY_UP
                    case 108: raylibKey = 264; break; // KEY_DOWN
                    case 105: raylibKey = 263; break; // KEY_LEFT
                    case 106: raylibKey = 262; break; // KEY_RIGHT
                    case 57:  raylibKey = 32;  break; // KEY_SPACE (A)
                    case 29:  raylibKey = 341; break; // KEY_LEFT_CONTROL (B)
                    case 42:  raylibKey = 340; break; // KEY_LEFT_SHIFT (X)
                    case 56:  raylibKey = 342; break; // KEY_LEFT_ALT (Y)
                    case 18:  raylibKey = 69;  break; // KEY_E (L1)
                    case 15:  raylibKey = 258; break; // KEY_TAB (L2)
                    case 20:  raylibKey = 84;  break; // KEY_T (R1)
                    case 14:  raylibKey = 259; break; // KEY_BACKSPACE (R2)
                    case 28:  raylibKey = 257; break; // KEY_ENTER (Start)
                    case 97:  raylibKey = 345; break; // KEY_RIGHT_CONTROL (Select)
                    case 1:   raylibKey = 256; break; // KEY_ESCAPE (Menu)
                    default: break;
                }

                if (raylibKey > 0 && raylibKey < MAX_KEYBOARD_KEYS)
                {
                    if (ev.value == 1)       // Press
                    {
                        CORE.Input.Keyboard.currentKeyState[raylibKey] = 1;

                        // Enqueue for GetKeyPressed()
                        if (CORE.Input.Keyboard.keyPressedQueueCount < MAX_KEY_PRESSED_QUEUE)
                        {
                            CORE.Input.Keyboard.keyPressedQueue[CORE.Input.Keyboard.keyPressedQueueCount] = raylibKey;
                            CORE.Input.Keyboard.keyPressedQueueCount++;
                        }
                    }
                    else if (ev.value == 0)  // Release
                    {
                        CORE.Input.Keyboard.currentKeyState[raylibKey] = 0;
                    }
                    else if (ev.value == 2)  // Repeat
                    {
                        CORE.Input.Keyboard.keyRepeatInFrame[raylibKey] = 1;
                    }
                }

                // Menu or Power → close window
                if (ev.value == 1 && (ev.code == 1 || ev.code == 116))
                {
                    CORE.Window.shouldClose = true;
                }
            }
        }
    }
#endif

    // Check for key pressed to exit
    if (kbhit())
    {
        int key = getch();
        if (key == 27) CORE.Window.shouldClose = true; // KEY_SCAPE
    }
}

//----------------------------------------------------------------------------------
// Module Internal Functions Definition
//----------------------------------------------------------------------------------

// Initialize platform: graphics, inputs and more
int InitPlatform(void)
{
    // Memory framebuffer works with rlsw (GRAPHICS_API_OPENGL_SOFTWARE) and TinyGL (GRAPHICS_API_OPENGL_11)
    int glVer = rlGetVersion();
    if (glVer != RL_OPENGL_SOFTWARE && glVer != RL_OPENGL_11)
    {
        TRACELOG(LOG_WARNING, "DISPLAY: Memory platform requires software renderer (GRAPHICS_API_OPENGL_SOFTWARE or GRAPHICS_API_OPENGL_11)");
        TRACELOG(LOG_FATAL, "PLATFORM: Failed to initialize graphics device");
        return -1;
    }
    else
    {
        // Framebuffer will be set up below (MMA or heap)
        platform.pixels = NULL;
    }

#if defined(__linux__) && defined(RAYLIB_MMF_FB)
    platform.fbFd = open("/dev/fb0", O_RDWR);
    if (platform.fbFd >= 0)
    {
        if (ioctl(platform.fbFd, FBIOGET_VSCREENINFO, &platform.vinfo) == 0 &&
            ioctl(platform.fbFd, FBIOGET_FSCREENINFO, &platform.finfo) == 0)
        {
            platform.fbSize = (size_t)platform.finfo.line_length * platform.vinfo.yres_virtual;
            platform.fbp = (unsigned char *)mmap(0, platform.fbSize, PROT_READ | PROT_WRITE, MAP_SHARED, platform.fbFd, 0);
            if (platform.fbp == MAP_FAILED) platform.fbp = NULL;

            // Override screen size to match framebuffer
            CORE.Window.display.width = platform.vinfo.xres;
            CORE.Window.display.height = platform.vinfo.yres;
            CORE.Window.screen.width = platform.vinfo.xres;
            CORE.Window.screen.height = platform.vinfo.yres;

            CORE.Window.render.width = CORE.Window.screen.width;
            CORE.Window.render.height = CORE.Window.screen.height;
            CORE.Window.currentFbo.width = CORE.Window.render.width;
            CORE.Window.currentFbo.height = CORE.Window.render.height;

            platform.fbBuffers = (platform.vinfo.yres > 0) ? (platform.vinfo.yres_virtual / platform.vinfo.yres) : 1;
            if (platform.fbBuffers < 1) platform.fbBuffers = 1;
            platform.fbBufferIndex = 0;
        }
    }

    int scale = MMF_GetEnvInt("RAYLIB_MMF_SCALE", 1);
    if (scale < 1) scale = 1;
    platform.renderWidth = CORE.Window.screen.width;
    platform.renderHeight = CORE.Window.screen.height;

    if (MMF_GetEnvBool("RAYLIB_MMF_MIGFX", true))
    {
        int scaledWidth = CORE.Window.screen.width / scale;
        int scaledHeight = CORE.Window.screen.height / scale;
        if (scaledWidth < 1) scaledWidth = 1;
        if (scaledHeight < 1) scaledHeight = 1;

#if defined(RAYLIB_USE_TINYGL)
        // TinyGL requires width to be a multiple of 4
        scaledWidth = scaledWidth & ~3;
#endif

        if (MMF_InitMiGfx(scaledWidth, scaledHeight))
        {
            platform.renderWidth = scaledWidth;
            platform.renderHeight = scaledHeight;
            platform.pixels = (unsigned int *)platform.mi.srcVir;
            TRACELOG(LOG_INFO, "MMF: Using MI_GFX hardware blit (%dx%d -> %dx%d)",
                platform.renderWidth, platform.renderHeight,
                (int)platform.vinfo.xres, (int)platform.vinfo.yres);
        }
        else
        {
            TRACELOG(LOG_WARNING, "MMF: MI_GFX unavailable, falling back to CPU blit");
        }
    }

    if (platform.pixels == NULL)
    {
        platform.pixels = (unsigned int *)RL_CALLOC(
            (size_t)platform.renderWidth * (size_t)platform.renderHeight, sizeof(int));
    }

#if defined(RAYLIB_USE_TINYGL)
    // Initialize TinyGL with the pixel buffer (zero-copy: TinyGL renders directly into platform.pixels)
    platform.tglZBuffer = ZB_open(platform.renderWidth, platform.renderHeight, ZB_MODE_RGBA, platform.pixels);
    if (platform.tglZBuffer == NULL)
    {
        TRACELOG(LOG_FATAL, "TINYGL: Failed to open ZBuffer");
        return -1;
    }
    glInit(platform.tglZBuffer);
    TRACELOG(LOG_INFO, "TINYGL: Initialized (%dx%d)", platform.renderWidth, platform.renderHeight);
#endif
#else
    platform.renderWidth = CORE.Window.screen.width;
    platform.renderHeight = CORE.Window.screen.height;
#if defined(RAYLIB_USE_TINYGL)
    platform.renderWidth = platform.renderWidth & ~3;
#endif
    platform.pixels = (unsigned int *)RL_CALLOC(
        (size_t)platform.renderWidth * (size_t)platform.renderHeight, sizeof(int));
#endif

#if defined(__linux__)
    platform.inputFdCount = 0;
    for (int i = 0; i < 8; i++)
    {
        char path[32] = { 0 };
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0)
        {
            platform.inputFds[platform.inputFdCount++] = fd;
        }
    }
#endif
    //----------------------------------------------------------------------------

    // If everything worked as expected, continue
#if defined(__linux__)
    CORE.Window.render.width = platform.renderWidth;
    CORE.Window.render.height = platform.renderHeight;
    CORE.Window.currentFbo.width = platform.renderWidth;
    CORE.Window.currentFbo.height = platform.renderHeight;
#else
    CORE.Window.render.width = CORE.Window.screen.width;
    CORE.Window.render.height = CORE.Window.screen.height;
    CORE.Window.currentFbo.width = CORE.Window.render.width;
    CORE.Window.currentFbo.height = CORE.Window.render.height;
#endif

    TRACELOG(LOG_INFO, "DISPLAY: Device initialized successfully");
    TRACELOG(LOG_INFO, "    > Display size: %i x %i", CORE.Window.display.width, CORE.Window.display.height);
    TRACELOG(LOG_INFO, "    > Screen size:  %i x %i", CORE.Window.screen.width, CORE.Window.screen.height);
    TRACELOG(LOG_INFO, "    > Render size:  %i x %i", CORE.Window.render.width, CORE.Window.render.height);
    TRACELOG(LOG_INFO, "    > Viewport offsets: %i, %i", CORE.Window.renderOffset.x, CORE.Window.renderOffset.y);

    CORE.Window.ready = true;

    // TODO: Load OpenGL extensions
    // NOTE: GL procedures address loader is required to load extensions
    //----------------------------------------------------------------------------
    // ...
    //----------------------------------------------------------------------------

    // TODO: Initialize input events system
    // It could imply keyboard, mouse, gamepad, touch...
    // Depending on the platform libraries/SDK it could use a callback mechanism
    // For system events and inputs evens polling on a per-frame basis, use PollInputEvents()
    //----------------------------------------------------------------------------
    // ...
    //----------------------------------------------------------------------------

    // Initialize timing system
    //----------------------------------------------------------------------------
#if defined(_WIN32)
    LARGE_INTEGER time = { 0 };
    QueryPerformanceCounter(&time);
    QueryPerformanceFrequency(&platform.timerFrequency);
    CORE.Time.base = time.QuadPart;
#endif
    InitTimer();
    //----------------------------------------------------------------------------

    // Initialize storage system
    //----------------------------------------------------------------------------
    CORE.Storage.basePath = GetWorkingDirectory();
    //----------------------------------------------------------------------------

    TRACELOG(LOG_INFO, "PLATFORM: MEMORY: Initialized successfully");

    return 0;
}

// Close platform
void ClosePlatform(void)
{
#if defined(RAYLIB_USE_TINYGL)
    glClose();
    ZB_close(platform.tglZBuffer);
    platform.tglZBuffer = NULL;
#endif
#if defined(__linux__) && defined(RAYLIB_MMF_FB)
    if (platform.mi.enabled)
    {
        MMF_ShutdownMiGfx();
        platform.pixels = NULL;
    }
    if (platform.fbp != NULL) munmap(platform.fbp, platform.fbSize);
    if (platform.fbFd >= 0) close(platform.fbFd);
#endif
#if defined(__linux__)
    for (int i = 0; i < platform.inputFdCount; i++)
    {
        if (platform.inputFds[i] >= 0) close(platform.inputFds[i]);
    }
#endif
    if (platform.pixels != NULL) RL_FREE(platform.pixels);
}

//----------------------------------------------------------------------------------
// Module Internal Functions Definition
//----------------------------------------------------------------------------------

#if defined(__linux__) && defined(RAYLIB_MMF_FB)
static int MMF_GetEnvInt(const char *name, int defaultValue)
{
    const char *value = getenv(name);
    if ((value == NULL) || (value[0] == '\0')) return defaultValue;
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value) return defaultValue;
    return (int)parsed;
}

static bool MMF_GetEnvBool(const char *name, bool defaultValue)
{
    const char *value = getenv(name);
    if ((value == NULL) || (value[0] == '\0')) return defaultValue;
    return (value[0] != '0');
}

static bool MMF_InitMiGfx(int renderWidth, int renderHeight)
{
    if (platform.mi.enabled) return true;

    platform.mi.libSys = dlopen("libmi_sys.so", RTLD_NOW | RTLD_GLOBAL);
    platform.mi.libGfx = dlopen("libmi_gfx.so", RTLD_NOW | RTLD_GLOBAL);
    if ((platform.mi.libSys == NULL) || (platform.mi.libGfx == NULL)) goto error;

    #define MMF_LOAD_SYM(handle, field, name) \
        do { \
            *(void **)(&platform.mi.field) = dlsym(handle, name); \
            if (platform.mi.field == NULL) goto error; \
        } while (0)

    MMF_LOAD_SYM(platform.mi.libSys, MI_SYS_Init, "MI_SYS_Init");
    MMF_LOAD_SYM(platform.mi.libSys, MI_SYS_Exit, "MI_SYS_Exit");
    MMF_LOAD_SYM(platform.mi.libSys, MI_SYS_MMA_Alloc, "MI_SYS_MMA_Alloc");
    MMF_LOAD_SYM(platform.mi.libSys, MI_SYS_MMA_Free, "MI_SYS_MMA_Free");
    MMF_LOAD_SYM(platform.mi.libSys, MI_SYS_Mmap, "MI_SYS_Mmap");
    MMF_LOAD_SYM(platform.mi.libSys, MI_SYS_Munmap, "MI_SYS_Munmap");
    MMF_LOAD_SYM(platform.mi.libSys, MI_SYS_FlushInvCache, "MI_SYS_FlushInvCache");
    MMF_LOAD_SYM(platform.mi.libSys, MI_SYS_MemsetPa, "MI_SYS_MemsetPa");

    MMF_LOAD_SYM(platform.mi.libGfx, MI_GFX_Open, "MI_GFX_Open");
    MMF_LOAD_SYM(platform.mi.libGfx, MI_GFX_Close, "MI_GFX_Close");
    MMF_LOAD_SYM(platform.mi.libGfx, MI_GFX_BitBlit, "MI_GFX_BitBlit");
    MMF_LOAD_SYM(platform.mi.libGfx, MI_GFX_WaitAllDone, "MI_GFX_WaitAllDone");

    #undef MMF_LOAD_SYM

    if (platform.mi.MI_SYS_Init() != 0) goto error;
    if (platform.mi.MI_GFX_Open() != 0) goto error;

    platform.mi.srcSize = (size_t)renderWidth * (size_t)renderHeight * 4;
    if (platform.mi.srcSize == 0) goto error;

    if (platform.mi.MI_SYS_MMA_Alloc(NULL, (unsigned int)platform.mi.srcSize, &platform.mi.srcPhy) != 0) goto error;
    if (platform.mi.MI_SYS_Mmap(platform.mi.srcPhy, (unsigned int)platform.mi.srcSize, &platform.mi.srcVir, 1) != 0) goto error;

    platform.mi.fbPhy = (uint64_t)platform.finfo.smem_start;
    (void)platform.mi.MI_SYS_Mmap(platform.mi.fbPhy, (unsigned int)platform.finfo.smem_len, &platform.mi.fbVir, 1);

    if (platform.mi.MI_SYS_MemsetPa != NULL)
    {
        platform.mi.MI_SYS_MemsetPa(platform.mi.fbPhy, 0, (unsigned int)platform.finfo.smem_len);
        platform.mi.MI_SYS_MemsetPa(platform.mi.srcPhy, 0, (unsigned int)platform.mi.srcSize);
    }

    platform.mi.renderWidth = renderWidth;
    platform.mi.renderHeight = renderHeight;
    platform.mi.enabled = true;
    return true;

error:
    MMF_ShutdownMiGfx();
    return false;
}

static void MMF_ShutdownMiGfx(void)
{
    if (platform.mi.srcVir && platform.mi.MI_SYS_Munmap) platform.mi.MI_SYS_Munmap(platform.mi.srcVir, (unsigned int)platform.mi.srcSize);
    if (platform.mi.srcPhy && platform.mi.MI_SYS_MMA_Free) platform.mi.MI_SYS_MMA_Free(platform.mi.srcPhy);
    if (platform.mi.fbVir && platform.mi.MI_SYS_Munmap) platform.mi.MI_SYS_Munmap(platform.mi.fbVir, (unsigned int)platform.finfo.smem_len);

    if (platform.mi.MI_GFX_Close) platform.mi.MI_GFX_Close();
    if (platform.mi.MI_SYS_Exit) platform.mi.MI_SYS_Exit();

    if (platform.mi.libGfx) dlclose(platform.mi.libGfx);
    if (platform.mi.libSys) dlclose(platform.mi.libSys);

    memset(&platform.mi, 0, sizeof(platform.mi));
}
#endif

#if !defined(_WIN32)
// Check if a key has been pressed
static int kbhit(void)
{
    struct termios oldt = { 0 };
    struct termios newt = { 0 };
    int ch = 0;
    int oldf = 0;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (ch != EOF)
    {
        ungetc(ch, stdin);
        return 1;
    }

    return 0;
}
#endif

// EOF
