// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>
#include <vector>
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Frontend {
class WindowSDL;
}

namespace Vulkan {

class Instance;
class Scheduler;

class Swapchain {
public:
    explicit Swapchain(const Instance& instance, const Frontend::WindowSDL& window);
    ~Swapchain();

    /// Creates (or recreates) the swapchain with a given size.
    void Create(u32 width, u32 height);

    /// Recreates the swapchain with a given size and current surface.
    void Recreate(u32 width, u32 height);

    /// Acquires the next image in the swapchain.
    bool AcquireNextImage();

    /// Presents the current image and move to the next one
    bool Present();

    /// On Android, the underlying ANativeWindow can be destroyed and
    /// replaced by the system during an orientation transition (we declare
    /// `orientation` in AndroidManifest's configChanges so the Activity is
    /// NOT recreated, but the surface still is). The vk::SurfaceKHR we
    /// created at construction time then becomes invalid and every swapchain
    /// operation returns eErrorSurfaceLostKHR — the screen stays black.
    ///
    /// This method polls the window's current native render_surface and, if
    /// it has changed, destroys the stale vk::SurfaceKHR and creates a fresh
    /// one from the new ANativeWindow. Returns true if the surface was
    /// actually refreshed (caller should follow up with Recreate()).
    ///
    /// On non-Android platforms this is a no-op and returns false.
    bool RefreshSurfaceIfNeeded();

    /// Like RefreshSurfaceIfNeeded, but if the immediate poll doesn't
    /// detect a change, retries with a short sleep for up to ~1 second.
    ///
    /// Use this from the AcquireNextImage-failure path in Presenter::Present
    /// when we *know* the surface is lost (ErrorSurfaceLostKHR was returned)
    /// but SDL3 may not yet have delivered the new ANativeWindow — on
    /// Android, surfaceDestroyed fires immediately (the vk::SurfaceKHR is
    /// dead), but surfaceCreated fires only after the Java side creates the
    /// new Surface, which can take 100-500ms during an orientation
    /// transition. Without waiting, we'd skip the frame and the renderer
    /// would never recover if Present() isn't called again.
    ///
    /// On non-Android platforms this is a no-op and returns false.
    bool WaitForFreshSurface();

    vk::SurfaceKHR GetSurface() const {
        return surface;
    }

    vk::Image Image() const {
        return images[image_index];
    }

    vk::ImageView ImageView() const {
        return images_view[image_index];
    }

    vk::SurfaceFormatKHR GetSurfaceFormat() const {
        return surface_format;
    }

    vk::SwapchainKHR GetHandle() const {
        return swapchain;
    }

    u32 GetWidth() const {
        return width;
    }

    u32 GetHeight() const {
        return height;
    }

    u32 GetImageCount() const {
        return image_count;
    }

    u32 GetFrameIndex() const {
        return frame_index;
    }

    vk::Extent2D GetExtent() const {
        return extent;
    }

    [[nodiscard]] vk::Semaphore GetImageAcquiredSemaphore() const {
        return image_acquired[frame_index];
    }

    [[nodiscard]] vk::Semaphore GetPresentReadySemaphore() const {
        return present_ready[image_index];
    }

    bool HasHDR() const {
        return supports_hdr;
    }

    void SetHDR(bool hdr);

    bool GetHDR() const {
        return needs_hdr;
    }

private:
    /// Selects the best available swapchain image format
    void FindPresentFormat();

    /// Selects the best available present mode
    void FindPresentMode();

    /// Sets the surface properties according to device capabilities
    void SetSurfaceProperties();

    /// Destroys current swapchain resources
    void Destroy();

    /// Performs creation of image views and framebuffers from the swapchain images
    void SetupImages();

    /// Creates the image acquired and present ready semaphores
    void RefreshSemaphores();

    /// (Re)creates the vk::SurfaceKHR from the window's current native
    /// render_surface. Destroys the previous surface first if any.
    /// On non-Android platforms this is a no-op (the surface is created
    /// once at construction time and never changes).
    void RecreateSurface();

    /// Internal helper: assumes a new ANativeWindow has already been
    /// detected (window_info.render_surface is the new pointer), refreshes
    /// the cached window size, recreates the vk::SurfaceKHR, re-queries
    /// format/present-mode, and marks needs_recreation=true. Returns true
    /// if a new usable surface was created.
    ///
    /// On non-Android this is a no-op and returns false.
    bool RefreshSurfaceFromNewANativeWindow();

private:
    const Instance& instance;
    const Frontend::WindowSDL& window;
    vk::SwapchainKHR swapchain{};
    vk::SurfaceKHR surface{};
    // Cached native window handle that was used to create `surface`.
    // On Android this lets us detect when the system has swapped the
    // ANativeWindow underneath us during an orientation transition.
    void* native_window_used = nullptr;
    vk::SurfaceFormatKHR surface_format;
    vk::Format view_format;
    vk::PresentModeKHR present_mode;
    vk::Extent2D extent;
    vk::SurfaceTransformFlagBitsKHR transform;
    vk::CompositeAlphaFlagBitsKHR composite_alpha;
    std::vector<vk::Image> images;
    std::vector<vk::ImageView> images_view;
    std::vector<vk::Semaphore> image_acquired;
    std::vector<vk::Semaphore> present_ready;
    u32 width = 0;
    u32 height = 0;
    u32 image_count = 0;
    u32 image_index = 0;
    u32 frame_index = 0;
    bool needs_recreation = true;
    bool needs_hdr = false;    // The game requested HDR swapchain
    bool supports_hdr = false; // SC supports HDR output
};

} // namespace Vulkan
