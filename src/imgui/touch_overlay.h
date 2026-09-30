// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace Input {
class GameControllers;
}

namespace TouchOverlay {

// Initialize the overlay. Must be called once per session, after ImGui is up
// and the controllers are reachable. Safe to call multiple times — it just
// re-reads the singleton each frame.
void Init();

// Render the overlay for the current frame. Call between ImGui::NewFrame()
// and ImGui::Render() each frame while the overlay should be visible.
// `window_width` and `window_height` are the SDL window's drawable size
// in pixels — used to scale the button hit-areas to the screen.
void Draw(float window_width, float window_height);

// Toggle visibility (called from a keyboard/gamepad shortcut). The overlay
// is visible by default.
void SetVisible(bool visible);
bool IsVisible();

// Android port: a one-shot entry point that loads the game metadata,
// opens an SDL2D-renderer window, and runs the overlay + a "Compatibility
// mode" message loop until the user closes the window.
//
// On ARM64 the emulator cannot yet execute the PS4's x86-64 game code
// (see ANDROID_PORT.md §6 for the roadmap), so instead of jumping into
// foreign code and crashing we surface a soft "compatibility mode" UI:
// the game's icon/title, a touch overlay that drives the standard
// OrbisPad API, and a clear message explaining the current state.
//
// This is the closest thing to "the game starts on Android" we can
// deliver today without writing a multi-month x86-64 interpreter.
//
// Returns when the user closes the window (back button / window-close).
void RunCompatibilityMode(const std::string& game_path,
                          const std::string& game_title,
                          Input::GameControllers* controllers);

} // namespace TouchOverlay
