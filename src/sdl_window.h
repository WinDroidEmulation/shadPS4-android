// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string>

#include "common/types.h"
#include "core/libraries/pad/pad.h"
#include "input/controller.h"

struct SDL_Window;
struct SDL_Gamepad;
union SDL_Event;

namespace Input {
class GameController;
}

namespace Frontend {

enum class WindowSystemType : u8 {
    Headless,
    Windows,
    X11,
    Wayland,
    Metal,
};

// On Android, only one SDL_Window can exist per Activity (SDL3 limitation).
// Big Picture UI creates a window when the app starts; when the user picks
// a game and clicks Launch, Big Picture would normally destroy that window
// and `Emulator::Run` would create a new one for the emulator. But
// destroying an SDL window on Android triggers `surfaceDestroyed` and
// causes the Activity to lose its ANativeWindow — Android then marks the
// Task as MINIMIZED, and the emulator window never recovers (MIUI is
// particularly aggressive about taking the launcher to foreground when
// the Activity briefly loses its surface).
//
// To avoid this transition entirely, Big Picture can hand its existing
// SDL_Window pointer to WindowSDL via this global. WindowSDL's ctor
// checks the global: if non-null, it adopts the existing window (calls
// SDL_DestroyRenderer on it if any, sets the VULKAN flag, and skips
// SDL_CreateWindow). If null, WindowSDL creates a new window as usual.
//
// Only used on Android. On desktop platforms this stays NULL and the
// existing create-new-window path is taken (multi-window SDL is fine
// there).
//
// Defined in sdl_window.cpp.
extern SDL_Window* g_reuse_sdl_window_on_android;

struct WindowSystemInfo {
    // Connection to a display server. This is used on X11 and Wayland platforms.
    void* display_connection = nullptr;

    // Render surface. This is a pointer to the native window handle, which depends
    // on the platform. e.g. HWND for Windows, Window for X11. If the surface is
    // set to nullptr, the video backend will run in headless mode.
    void* render_surface = nullptr;

    // Scale of the render surface. For hidpi systems, this will be >1.
    float render_surface_scale = 1.0f;

    // Window system type. Determines which GL context or Vulkan WSI is used.
    WindowSystemType type = WindowSystemType::Headless;
};

class WindowSDL {
    int keyboard_grab = 0;

public:
    explicit WindowSDL(s32 width, s32 height, Input::GameControllers* controllers,
                       std::string_view window_title);
    ~WindowSDL();

    s32 GetWidth() const {
        return width;
    }

    s32 GetHeight() const {
        return height;
    }

    bool IsOpen() const {
        return is_open;
    }

    [[nodiscard]] SDL_Window* GetSDLWindow() const {
        return window;
    }

    WindowSystemInfo GetWindowInfo() const {
        return window_info;
    }

    // Returns the most up-to-date native render_surface pointer currently
    // published by SDL in the window's property set. On Android this can
    // change at runtime when the Activity goes through a config-change /
    // orientation transition (the system destroys the old ANativeWindow
    // and hands SDL a new one, even though the Activity itself is not
    // recreated because we declare `orientation` in `configChanges`).
    // On non-Android platforms this is a no-op and returns false.
    //
    // The check is read-only with respect to SDL state and may be called
    // from any thread; SDL_GetWindowProperties + SDL_GetPointerProperty are
    // internally synchronized in SDL3. The method is const because it only
    // refreshes the cached `window_info.render_surface` pointer (which is
    // mutable for exactly this reason).
    bool PollAndroidNativeWindow() const;

    // Re-reads the current window pixel size from SDL and updates the
    // cached `width`/`height` members. Returns true if either dimension
    // changed. Safe to call from any thread; on Android the size returned
    // by SDL reflects the latest ANativeWindow geometry. Like
    // PollAndroidNativeWindow, this is const — it only refreshes cached
    // state that is allowed to change underneath us.
    bool PollWindowSize() const;

    // Android-only: returns the current value of the static Java counter
    // `Shadps4Activity.sSurfaceGeneration`, bumped every time Android
    // fires `surfaceCreated` (i.e. a new SurfaceHolder — and therefore
    // a new ANativeWindow — is on its way). Returns -1 on non-Android
    // or if the JNI call fails.
    //
    // The renderer uses this to detect that a new ANativeWindow is
    // available even when SDL3's `SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER`
    // property hasn't yet been observed to change from the render
    // thread (SDL3 updates it on the Java main thread inside
    // `onNativeSurfaceCreated`, which runs synchronously inside our
    // `Shadps4Surface.surfaceCreated` hook BEFORE bumping the counter,
    // so by the time we observe a new generation the SDL3 property is
    // guaranteed to already be updated — but visibility from the
    // render thread depends on memory ordering across the JNI boundary,
    // which is why this counter is the authoritative signal).
    //
    // On non-Android platforms this returns -1.
    static int GetJavaSurfaceGeneration();

    void SetIcon(std::span<const u8> png_data);

    void WaitEvent();
    void InitTimers();

    void RequestKeyboard();
    void ReleaseKeyboard();

private:
    void OnResize();
    void OnKeyboardMouseInput(const SDL_Event* event);
    void OnGamepadEvent(const SDL_Event* event);

private:
    // `width`, `height`, and `window_info` are marked mutable because they
    // are cached values that get refreshed from SDL at any time (see
    // PollAndroidNativeWindow / PollWindowSize / OnResize). The refreshers
    // are const methods callable from any thread, which is the standard
    // "mutable cached state" idiom.
    mutable s32 width;
    mutable s32 height;
    Input::GameControllers controllers{};
    mutable WindowSystemInfo window_info{};
    SDL_Window* window{};
    bool is_shown{};
    bool is_open{true};
};

void SetWindowIcon(SDL_Window* window, const std::vector<u8>& png);
void SetDefaultWindowIcon(SDL_Window* window);

} // namespace Frontend
