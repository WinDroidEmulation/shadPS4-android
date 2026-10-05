// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "imgui/touch_overlay.h"

#include <imgui.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/scm_rev.h"
#include "core/emulator_settings.h"
#include "core/file_sys/fs.h"
#include "core/libraries/pad/pad.h"
#include "imgui/imgui_translations.h"
#include "imgui/renderer/font_stack.h"
#include "input/controller.h"

// ImGui SDL3 backend headers — shadPS4 vendors its own copies in
// src/imgui/big_picture/ (slightly patched from upstream imgui_impl_sdl3.h
// and imgui_impl_sdlrenderer3.h). Re-use them rather than pulling upstream's.
#include "imgui/big_picture/imgui_impl_sdl3_big_picture.h"
#include "imgui/big_picture/imgui_impl_sdlrenderer3.h"
#include "imgui/imgui_layer.h"

namespace TouchOverlay {

namespace {

// State that persists across frames within one compatibility-mode session.
struct OverlayState {
    bool visible = true;
    bool show_compat_message = true;
    // Touch button press tracking. Each face button / D-pad direction records
    // whether it's currently held down so we can fire Button(true)/Button(false)
    // transitions to the underlying GameController.
    bool dpad_up = false, dpad_down = false, dpad_left = false, dpad_right = false;
    bool btn_cross = false, btn_circle = false, btn_square = false, btn_triangle = false;
    bool btn_l1 = false, btn_l2 = false, btn_r1 = false, btn_r2 = false;
    bool btn_options = false;
    // Analog-stick state, in range [-1.0, 1.0].
    float left_stick_x = 0.0f, left_stick_y = 0.0f;
    float right_stick_x = 0.0f, right_stick_y = 0.0f;
    // When dragging an analog stick, the finger ID that owns the drag and
    // the screen position where it started.
    SDL_FingerID left_stick_finger = 0;
    SDL_FingerID right_stick_finger = 0;
    float left_stick_origin_x = 0.0f, left_stick_origin_y = 0.0f;
    float right_stick_origin_x = 0.0f, right_stick_origin_y = 0.0f;
    bool left_stick_active = false;
    bool right_stick_active = false;
};

OverlayState g_state;
Input::GameControllers* g_controllers = nullptr;

// Color palette — translucent so the underlying game window stays visible.
const ImVec4 kButtonColorIdle{0.20f, 0.22f, 0.30f, 0.55f};
const ImVec4 kButtonColorActive{0.40f, 0.55f, 0.95f, 0.80f};
const ImVec4 kButtonColorText{1.0f, 1.0f, 1.0f, 0.90f};
const ImVec4 kCompatBg{0.04f, 0.05f, 0.08f, 0.85f};

// Convert SDL normalized touch coords (0..1, top-left origin) to window
// pixels (top-left origin).
inline ImVec2 TouchToPx(float nx, float ny, float w, float h) {
    return ImVec2(nx * w, ny * h);
}

// Test if a 2D point is inside a circle centered at (cx,cy) with radius r.
inline bool PointInCircle(float px, float py, float cx, float cy, float r) {
    const float dx = px - cx, dy = py - cy;
    return (dx * dx + dy * dy) <= r * r;
}

using PadBtn = Libraries::Pad::OrbisPadButtonDataOffset;

void SendButton(PadBtn btn, bool pressed) {
    if (!g_controllers) {
        return;
    }
    auto* ctrl = (*g_controllers)[0];
    if (ctrl) {
        ctrl->Button(btn, pressed);
    }
}

void SendAxis(Input::Axis axis, int value) {
    if (!g_controllers) {
        return;
    }
    auto* ctrl = (*g_controllers)[0];
    if (ctrl) {
        ctrl->Axis(axis, value, false);
    }
}

// Clamp normalized [-1,1] to the OrbisPadAnalogStick 0..255 range.
inline int AxisToPadByte(float v) {
    int b = static_cast<int>(std::round((v + 1.0f) * 127.5f));
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return b;
}

// Draw a single round touch button. Returns true if the button's pressed
// state changed this frame.
bool RoundButton(const char* id, float cx, float cy, float radius, const char* label,
                 bool currently_pressed) {
    const ImVec2 center{cx, cy};
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_AlwaysAutoResize;
    char win_id[64];
    std::snprintf(win_id, sizeof(win_id), "##touch_%s", id);
    ImGui::SetNextWindowPos(ImVec2(cx - radius, cy - radius), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(radius * 2, radius * 2), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::Begin(win_id, nullptr, flags);
    const ImVec2 p_min = ImGui::GetCursorScreenPos();
    const ImVec2 p_max{p_min.x + radius * 2, p_min.y + radius * 2};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 fill_col = ImGui::ColorConvertFloat4ToU32(currently_pressed ? kButtonColorActive
                                                                               : kButtonColorIdle);
    dl->AddCircleFilled(center, radius, fill_col, 24);
    dl->AddCircle(center, radius, ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.25f)),
                  24, 2.0f);
    // Label centered.
    const ImVec2 label_size = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(center.x - label_size.x * 0.5f, center.y - label_size.y * 0.5f),
                ImGui::ColorConvertFloat4ToU32(kButtonColorText), label);
    // Touch hit-test.
    bool touched = false;
    ImGuiIO& io = ImGui::GetIO();
    // Mouse fallback (desktop testing).
    if (io.MousePos.x >= p_min.x && io.MousePos.x <= p_max.x && io.MousePos.y >= p_min.y &&
        io.MousePos.y <= p_max.y) {
        if (io.MouseClicked[0] || io.MouseDown[0]) {
            touched = true;
        }
    }
    // Touch: any finger within the button's circle counts as pressed.
    // SDL tracks fingers via io.AddMouseSourceEvent / SDL_EVENT_FINGER_*; here
    // we pull them straight from the SDL touch API on the active touch device.
    int touch_dev_count = 0;
    SDL_TouchID* devices = SDL_GetTouchDevices(&touch_dev_count);
    if (devices) {
        const float w = ImGui::GetMainViewport()->Size.x;
        const float h = ImGui::GetMainViewport()->Size.y;
        for (int i = 0; i < touch_dev_count; ++i) {
            int finger_count = 0;
            SDL_Finger** fingers = SDL_GetTouchFingers(devices[i], &finger_count);
            if (!fingers) continue;
            for (int f = 0; f < finger_count; ++f) {
                const SDL_Finger* fg = fingers[f];
                if (!fg) continue;
                const ImVec2 pos = TouchToPx(fg->x, fg->y, w, h);
                if (PointInCircle(pos.x, pos.y, cx, cy, radius)) {
                    touched = true;
                    break;
                }
            }
            SDL_free(fingers);
        }
        SDL_free(devices);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    return touched;
}

// Draw an analog stick with a draggable knob. Updates g_state fields.
void AnalogStick(const char* id, float cx, float cy, float outer_r, float inner_r,
                 bool& active, float& stick_x, float& stick_y, SDL_FingerID& finger_id,
                 float& origin_x, float& origin_y) {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_AlwaysAutoResize;
    char win_id[64];
    std::snprintf(win_id, sizeof(win_id), "##analog_%s", id);
    ImGui::SetNextWindowPos(ImVec2(cx - outer_r, cy - outer_r), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(outer_r * 2, outer_r * 2), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::Begin(win_id, nullptr, flags);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 outer_col = ImGui::ColorConvertFloat4ToU32(
        active ? kButtonColorActive : kButtonColorIdle);
    dl->AddCircleFilled(ImVec2(cx, cy), outer_r, outer_col, 24);
    dl->AddCircle(ImVec2(cx, cy), outer_r, ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.25f)),
                  24, 2.0f);

    // Touch logic.
    int touch_dev_count = 0;
    SDL_TouchID* devices = SDL_GetTouchDevices(&touch_dev_count);
    const float w = ImGui::GetMainViewport()->Size.x;
    const float h = ImGui::GetMainViewport()->Size.y;
    if (devices) {
        for (int i = 0; i < touch_dev_count; ++i) {
            int finger_count = 0;
            SDL_Finger** fingers = SDL_GetTouchFingers(devices[i], &finger_count);
            if (!fingers) continue;
            for (int f = 0; f < finger_count; ++f) {
                const SDL_Finger* fg = fingers[f];
                if (!fg) continue;
                const ImVec2 pos = TouchToPx(fg->x, fg->y, w, h);
                if (!active && PointInCircle(pos.x, pos.y, cx, cy, outer_r)) {
                    active = true;
                    finger_id = fg->id;
                    origin_x = pos.x;
                    origin_y = pos.y;
                }
                if (active && finger_id == fg->id) {
                    const float dx = pos.x - origin_x;
                    const float dy = pos.y - origin_y;
                    const float max_dx = (outer_r - inner_r) * 2.0f;
                    stick_x = std::clamp(dx / max_dx, -1.0f, 1.0f);
                    stick_y = std::clamp(dy / max_dx, -1.0f, 1.0f);
                    const float kx = cx + stick_x * (outer_r - inner_r);
                    const float ky = cy + stick_y * (outer_r - inner_r);
                    dl->AddCircleFilled(ImVec2(kx, ky), inner_r, outer_col, 16);
                    dl->AddCircle(ImVec2(kx, ky), inner_r,
                                  ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.45f)), 16, 2.0f);
                }
            }
            SDL_free(fingers);
        }
        // Detect finger-up by checking that the tracked finger is gone.
        if (active) {
            bool still_down = false;
            for (int i = 0; i < touch_dev_count && !still_down; ++i) {
                int finger_count = 0;
                SDL_Finger** fingers = SDL_GetTouchFingers(devices[i], &finger_count);
                if (!fingers) continue;
                for (int f = 0; f < finger_count; ++f) {
                    if (fingers[f] && fingers[f]->id == finger_id) {
                        still_down = true;
                        break;
                    }
                }
                SDL_free(fingers);
            }
            if (!still_down) {
                active = false;
                stick_x = 0.0f;
                stick_y = 0.0f;
            }
        }
        SDL_free(devices);
    }
    if (!active) {
        // Draw centered knob.
        dl->AddCircleFilled(ImVec2(cx, cy), inner_r, outer_col, 16);
        dl->AddCircle(ImVec2(cx, cy), inner_r,
                      ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.45f)), 16, 2.0f);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

// Sync the boolean button states to the underlying GameController.
void SyncControllerButtons() {
    SendButton(PadBtn::Up, g_state.dpad_up);
    SendButton(PadBtn::Down, g_state.dpad_down);
    SendButton(PadBtn::Left, g_state.dpad_left);
    SendButton(PadBtn::Right, g_state.dpad_right);
    SendButton(PadBtn::Cross, g_state.btn_cross);
    SendButton(PadBtn::Circle, g_state.btn_circle);
    SendButton(PadBtn::Square, g_state.btn_square);
    SendButton(PadBtn::Triangle, g_state.btn_triangle);
    SendButton(PadBtn::L1, g_state.btn_l1);
    SendButton(PadBtn::L2, g_state.btn_l2);
    SendButton(PadBtn::R1, g_state.btn_r1);
    SendButton(PadBtn::R2, g_state.btn_r2);
    SendButton(PadBtn::Options, g_state.btn_options);
    // Analog axes: SDL/ImGui y grows downward, OrbisPad y grows upward, so flip y.
    SendAxis(Input::Axis::LeftX, AxisToPadByte(g_state.left_stick_x));
    SendAxis(Input::Axis::LeftY, AxisToPadByte(-g_state.left_stick_y));
    SendAxis(Input::Axis::RightX, AxisToPadByte(g_state.right_stick_x));
    SendAxis(Input::Axis::RightY, AxisToPadByte(-g_state.right_stick_y));
    // Triggers: drive from L2/R2 booleans (no pressure sensitivity here).
    SendAxis(Input::Axis::TriggerLeft, g_state.btn_l2 ? 255 : 0);
    SendAxis(Input::Axis::TriggerRight, g_state.btn_r2 ? 255 : 0);
}

void DrawCompatMessage(float w, float h, const std::string& game_title) {
    const float msg_w = w * 0.8f;
    const float msg_h = 220.0f;
    ImGui::SetNextWindowPos(ImVec2((w - msg_w) * 0.5f, 24.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(msg_w, msg_h), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kCompatBg);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.95f));
    ImGui::Begin("##compat_msg", nullptr, flags);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextWrapped("%s", game_title.c_str());
    ImGui::Separator();
    ImGui::SetWindowFontScale(0.9f);
    using ImguiTranslate::tr;
    ImGui::TextWrapped("%s",
                       tr("Compatibility mode — game loaded, but x86-64 → ARM64 "
                          "interpreter is still in development (see ANDROID_PORT.md §6).")
                           .c_str());
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextWrapped("%s",
                       tr("Touch the on-screen buttons to test the virtual gamepad. "
                          "When the interpreter backend lands, games will execute here.")
                           .c_str());
    ImGui::Dummy(ImVec2(0, 6));
    if (ImGui::Button(tr("Hide message").c_str())) {
        g_state.show_compat_message = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Back to Big Picture").c_str())) {
        // Post a QUIT event so the loop exits and Emulator::Run returns.
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

} // namespace

// ImGui Layer subclass that calls TouchOverlay::Draw every frame. We register
// this with ImGui::Core via TouchOverlay::Init() on Android, so the virtual
// buttons are drawn on top of the running game's Vulkan-rendered output.
//
// On non-Android platforms, Init() is a no-op (see the #if below), so the
// layer registration never happens and the touch overlay stays disabled.
#if defined(__ANDROID__)
namespace {
class TouchOverlayLayer : public ImGui::Layer {
public:
    void Draw() override {
        // The renderer's ImGui::Core calls us once per frame between
        // ImGui::NewFrame() and ImGui::Render(). We don't have direct
        // access to the SDL_Window's pixel dimensions from here, but
        // ImGui::GetMainViewport()->Size gives us the same information
        // (it's set by ImGui_ImplVulkan_NewFrame from the surface extent).
        ImGuiViewport* vp = ImGui::GetMainViewport();
        if (vp == nullptr || vp->Size.x <= 0.0f || vp->Size.y <= 0.0f) {
            return;
        }
        TouchOverlay::Draw(vp->Size.x, vp->Size.y);
    }
};

TouchOverlayLayer g_touch_overlay_layer;
bool g_layer_registered = false;
} // namespace
#endif

void Init() {
#if defined(__ANDROID__)
    // Register the touch overlay as an ImGui layer so it gets drawn on
    // every frame by the renderer's ImGui::Core::Render() loop. This is
    // what makes the virtual D-pad / face / shoulder buttons visible
    // on top of the game's video output on touch-only devices.
    //
    // Idempotent — only registers once even if Init is called multiple
    // times (e.g. by Emulator::Run after the Big Picture UI already
    // initialized ImGui).
    if (!g_layer_registered) {
        ImGui::Layer::AddLayer(&g_touch_overlay_layer);
        g_layer_registered = true;
        LOG_INFO(ImGui, "Touch overlay layer registered (virtual gamepad on Android)");
    }
#else
    // Touch overlay is Android-only — on desktop platforms the user has
    // a real gamepad / keyboard, no need for the overlay.
    (void)0;
#endif
}

void SetVisible(bool visible) {
    g_state.visible = visible;
}

bool IsVisible() {
    return g_state.visible;
}

void Draw(float window_width, float window_height) {
    if (!g_state.visible) {
        return;
    }
    const float w = window_width;
    const float h = window_height;

    // Button radius scales with the smaller screen dimension so phones in
    // portrait and tablets in landscape both get sensible hit areas.
    const float scale = std::min(w, h) / 720.0f;
    const float btn_r = 50.0f * scale;       // face / shoulder buttons
    const float dpad_r = 38.0f * scale;      // D-pad half-width
    const float dpad_gap = 6.0f * scale;    // gap between D-pad arms
    const float stick_outer = 70.0f * scale;
    const float stick_inner = 28.0f * scale;
    const float margin = 36.0f * scale;     // distance from the screen edge

    // D-pad (left side, lower half).
    const float dpad_cx = margin + dpad_r + dpad_gap + 10.0f;
    const float dpad_cy = h - margin - (dpad_r + dpad_gap) * 1.5f - 60.0f * scale;
    const float up_cy = dpad_cy - (dpad_r + dpad_gap);
    const float down_cy = dpad_cy + (dpad_r + dpad_gap);
    const float left_cx = dpad_cx - (dpad_r + dpad_gap);
    const float right_cx = dpad_cx + (dpad_r + dpad_gap);
    g_state.dpad_up = RoundButton("dpad_up", dpad_cx, up_cy, dpad_r, "▲", g_state.dpad_up);
    g_state.dpad_down = RoundButton("dpad_down", dpad_cx, down_cy, dpad_r, "▼", g_state.dpad_down);
    g_state.dpad_left =
        RoundButton("dpad_left", left_cx, dpad_cy, dpad_r, "◀", g_state.dpad_left);
    g_state.dpad_right =
        RoundButton("dpad_right", right_cx, dpad_cy, dpad_r, "▶", g_state.dpad_right);

    // Face buttons (right side, lower half) — PS4 layout: Triangle top,
    // Circle right, Cross bottom, Square left.
    const float face_cx = w - margin - (btn_r * 2.0f + 10.0f);
    const float face_cy = h - margin - (btn_r * 1.5f) - 60.0f * scale;
    const float face_gap = btn_r + 4.0f * scale;
    g_state.btn_triangle =
        RoundButton("tri", face_cx, face_cy - face_gap, btn_r, "△", g_state.btn_triangle);
    g_state.btn_circle =
        RoundButton("cir", face_cx + face_gap, face_cy, btn_r, "○", g_state.btn_circle);
    g_state.btn_cross =
        RoundButton("crs", face_cx, face_cy + face_gap, btn_r, "✕", g_state.btn_cross);
    g_state.btn_square =
        RoundButton("sqr", face_cx - face_gap, face_cy, btn_r, "□", g_state.btn_square);

    // Shoulder buttons (top corners).
    const float sh_y = margin + btn_r;
    g_state.btn_l1 = RoundButton("l1", margin + btn_r, sh_y, btn_r * 0.85f, "L1", g_state.btn_l1);
    g_state.btn_l2 = RoundButton("l2", margin + btn_r * 2.8f, sh_y, btn_r * 0.85f, "L2",
                                  g_state.btn_l2);
    g_state.btn_r1 =
        RoundButton("r1", w - margin - btn_r, sh_y, btn_r * 0.85f, "R1", g_state.btn_r1);
    g_state.btn_r2 = RoundButton("r2", w - margin - btn_r * 2.8f, sh_y, btn_r * 0.85f, "R2",
                                  g_state.btn_r2);

    // Options button (top center).
    const float opt_y = margin + btn_r * 0.85f;
    g_state.btn_options =
        RoundButton("opt", w * 0.5f, opt_y, btn_r * 0.85f, "≡", g_state.btn_options);

    // Analog sticks (lower center, between D-pad and face buttons).
    const float stick_y = h - margin - stick_outer - 10.0f * scale;
    AnalogStick("left", w * 0.36f, stick_y, stick_outer, stick_inner, g_state.left_stick_active,
                g_state.left_stick_x, g_state.left_stick_y, g_state.left_stick_finger,
                g_state.left_stick_origin_x, g_state.left_stick_origin_y);
    AnalogStick("right", w * 0.64f, stick_y, stick_outer, stick_inner,
                g_state.right_stick_active, g_state.right_stick_x, g_state.right_stick_y,
                g_state.right_stick_finger, g_state.right_stick_origin_x,
                g_state.right_stick_origin_y);

    SyncControllerButtons();
}

void RunCompatibilityMode(const std::string& game_path,
                         const std::string& game_title_in,
                         Input::GameControllers* controllers) {
    g_controllers = controllers;
    g_state = OverlayState{};

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR(ImGui, "Compatibility mode: SDL_INIT_VIDEO failed: {}", SDL_GetError());
        return;
    }

    SDL_Window* window = SDL_CreateWindow(
        "shadPS4 — Compatibility Mode", 1280, 720,
        EmulatorSettings.IsFullScreen() ? SDL_WINDOW_FULLSCREEN : 0);
    if (!window) {
        LOG_ERROR(ImGui, "Compatibility mode: window creation failed: {}", SDL_GetError());
        SDL_Quit();
        return;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        LOG_ERROR(ImGui, "Compatibility mode: renderer creation failed: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImFontConfig cfgBase{};
    cfgBase.OversampleH = 2;
    cfgBase.OversampleV = 1;
    // Reuse the same font stack as Big Picture so the same CJK fallback
    // covers zh-CN text on the compat message and the touch overlay.
    io.FontDefault = ImGui::FontStack::AddPrimaryUiFont(
        io.Fonts, 32.0f, EmulatorSettings.GetConsoleLanguage(), cfgBase, true);
    io.FontGlobalScale = 0.5f;

    const auto max_dim = SDL_GetNumberProperty(SDL_GetRendererProperties(renderer),
                                                SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 8192);
    const int atlas_max = static_cast<int>(std::bit_floor(std::max<u64>(max_dim, 512)));
    io.Fonts->TexMaxWidth = atlas_max;
    io.Fonts->TexMaxHeight = atlas_max;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Localized game title fallback. If the caller passed an empty string
    // (e.g. failed metadata parse), show the basename instead.
    std::string game_title = game_title_in;
    if (game_title.empty()) {
        try {
            game_title = std::filesystem::path(game_path).filename().string();
        } catch (...) {
            game_title = "(unknown title)";
        }
    }

    // Try to mark controller 0 as connected so the overlay's button presses
    // actually flow into the OrbisPad API.
    if (g_controllers) {
        auto* ctrl = (*g_controllers)[0];
        if (ctrl) {
            ctrl->ConnectController(nullptr);
        }
    }

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                done = true;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                done = true;
            }
            // SDL_EVENT_GAMEPAD_BUTTON_DOWN on the Android "Back" button
            // (the OS-level back gesture) should also exit.
            // SDL3's SDL_KeyboardEvent has the keycode in `event.key.key`
            // (SDL2 used `event.key.keysym.sym`).
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_AC_BACK) {
                done = true;
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        if (w <= 0 || h <= 0) {
            w = 1280;
            h = 720;
        }

        if (g_state.show_compat_message) {
            DrawCompatMessage(static_cast<float>(w), static_cast<float>(h), game_title);
        }
        Draw(static_cast<float>(w), static_cast<float>(h));

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 10, 10, 14, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

} // namespace TouchOverlay
