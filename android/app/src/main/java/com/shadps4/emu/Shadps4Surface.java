// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Android port contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package com.shadps4.emu;

import android.content.Context;
import android.view.SurfaceHolder;

import org.libsdl.app.SDLSurface;

/**
 * Trivial subclass of SDL3's {@link SDLSurface} that bumps a static
 * generation counter every time Android fires {@code surfaceCreated}.
 *
 * <p>This counter is the only signal the C++ renderer can rely on to
 * detect that a new SurfaceHolder (and therefore a new ANativeWindow)
 * has been created by the Android framework after an orientation
 * transition. SDL3's own {@code SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER}
 * property is updated on the Java main thread inside SDL3's
 * {@code onNativeSurfaceCreated} JNI callback, which fires synchronously
 * inside the parent class's {@code surfaceCreated}; but the render
 * thread polling that property may not see the update reliably during
 * the narrow window between {@code surfaceDestroyed} (old ANativeWindow
 * dies immediately, but SDL3's property is left pointing at the dead
 * handle) and {@code surfaceCreated} (new ANativeWindow arrives).
 *
 * <p>By hooking the SurfaceHolder callback directly here and bumping a
 * {@code volatile int}, the C++ side gets a thread-safe atomic signal
 * that's updated before SDL3 even runs its own hook — so the renderer
 * can stop polling as soon as the new generation arrives, regardless
 * of whether SDL3's property update is yet visible from the render
 * thread.
 *
 * <p>The renderer reads this via JNI:
 * <pre>
 *   jint gen = SDL_JNI_GetSurfaceGeneration();
 *   if (gen != last_seen_gen) {
 *       // New ANativeWindow is on its way (or already here).
 *       // Re-poll SDL3's property; if it's still the old pointer,
 *       // spin a few more times — SDL3's onNativeSurfaceCreated
 *       // runs as part of this same Java callback, so by the time
 *       // we return from getSurfaceGeneration() the property is
 *       // guaranteed to be updated.
 *   }
 * </pre>
 */
public class Shadps4Surface extends SDLSurface {

    public Shadps4Surface(Context context) {
        super(context);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        // Bump the generation counter BEFORE calling super.surfaceCreated
        // (which fires SDL3's onNativeSurfaceCreated and updates the
        // SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER property). The C++
        // side reads this counter via JNI and, on a change, knows to
        // re-poll the SDL3 property.
        //
        // volatile write here, volatile read on the C++ side via JNI —
        // happens-before relationship is established by the JNI call
        // boundary, so the renderer will observe the new value.
        Shadps4Activity.sSurfaceGeneration++;

        super.surfaceCreated(holder);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        // Don't bump — the C++ side detects surface loss via the
        // vk::SurfaceKHR operations returning eErrorSurfaceLostKHR,
        // not via this counter. We only need a positive signal for
        // "new surface arrived".
        super.surfaceDestroyed(holder);
    }
}
