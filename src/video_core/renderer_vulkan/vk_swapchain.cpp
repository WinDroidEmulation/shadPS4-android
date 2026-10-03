// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Enable Android Vulkan WSI types (vk::AndroidSurfaceCreateInfoKHR) on
// Android so we can (re)create the vk::SurfaceKHR from a fresh ANativeWindow
// when the system swaps it during orientation transitions.
#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR
#include <android/native_window.h>
#include <chrono>
#include <thread>
#endif

#include <algorithm>
#include <limits>
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/emulator_settings.h"
#include "imgui/renderer/imgui_core.h"
#include "sdl_window.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"

namespace Vulkan {

static constexpr vk::SurfaceFormatKHR SURFACE_FORMAT_HDR = {
    .format = vk::Format::eA2B10G10R10UnormPack32,
    .colorSpace = vk::ColorSpaceKHR::eHdr10St2084EXT,
};

Swapchain::Swapchain(const Instance& instance_, const Frontend::WindowSDL& window_)
    : instance{instance_}, window{window_} {
    RecreateSurface();
    if (!surface) {
        LOG_CRITICAL(Render_Vulkan, "Swapchain constructed without a valid surface; rendering "
                                    "will be deferred until the surface is acquired.");
    }
    FindPresentFormat();
    FindPresentMode();

    Create(window.GetWidth(), window.GetHeight());
    ImGui::Core::Initialize(instance, window, image_count, surface_format.format);
}

Swapchain::~Swapchain() {
    Destroy();
    if (surface) {
        instance.GetInstance().destroySurfaceKHR(surface);
        surface = nullptr;
    }
    native_window_used = nullptr;
}

void Swapchain::RecreateSurface() {
#if defined(__ANDROID__)
    // On Android the ANativeWindow can be swapped underneath us by the
    // system during an orientation transition. Recreate the vk::SurfaceKHR
    // from the *current* window_info.render_surface pointer.
    if (surface) {
        // Make sure any in-flight work touching the old surface is done
        // before we tear it down.
        instance.GetDevice().waitIdle();
        instance.GetInstance().destroySurfaceKHR(surface);
        surface = nullptr;
    }
    auto new_window = window.GetWindowInfo().render_surface;
    if (new_window == nullptr) {
        // Refresh from SDL in case the cached value in WindowSDL is stale.
        window.PollAndroidNativeWindow();
        new_window = window.GetWindowInfo().render_surface;
    }
    if (new_window == nullptr) {
        LOG_ERROR(Render_Vulkan, "RecreateSurface: no ANativeWindow available yet");
        native_window_used = nullptr;
        return;
    }
    const vk::AndroidSurfaceCreateInfoKHR android_ci = {
        .window = static_cast<ANativeWindow*>(new_window),
    };
    const auto result = instance.GetInstance().createAndroidSurfaceKHR(&android_ci, nullptr, &surface);
    if (result != vk::Result::eSuccess) {
        LOG_ERROR(Render_Vulkan, "Failed to (re)create Android surface: {}",
                  static_cast<int>(result));
        surface = nullptr;
        native_window_used = nullptr;
        return;
    }
    native_window_used = new_window;
    LOG_INFO(Render_Vulkan, "Android surface recreated from ANativeWindow={}", new_window);
#else
    if (surface) {
        return; // already created at construction time, never changes
    }
    surface = CreateSurface(instance.GetInstance(), window);
    native_window_used = window.GetWindowInfo().render_surface;
#endif
}

bool Swapchain::RefreshSurfaceFromNewANativeWindow() {
#if defined(__ANDROID__)
    // The cached window_info.render_surface has just been updated to the
    // new ANativeWindow pointer. Recreate the vk::SurfaceKHR from it, and
    // also refresh the cached window size so the next swapchain recreate
    // matches the post-orientation geometry (e.g. portrait 1080x2400 ->
    // landscape 2400x1080). The main-thread SDL event loop may not have
    // processed the SDL_EVENT_WINDOW_RESIZED yet.
    window.PollWindowSize();
    RecreateSurface();
    if (surface) {
        // Surface format / present mode may differ on the new surface;
        // re-query to be safe.
        FindPresentFormat();
        FindPresentMode();
    }
    needs_recreation = true;
    return surface != nullptr;
#else
    return false;
#endif
}

bool Swapchain::RefreshSurfaceIfNeeded() {
#if defined(__ANDROID__)
    if (window.PollAndroidNativeWindow()) {
        // ANativeWindow was swapped under us. The old vk::SurfaceKHR is now
        // backed by a destroyed ANativeWindow, so every swapchain operation
        // against it returns eErrorSurfaceLostKHR — the screen stays black.
        // Recreate the surface from the fresh ANativeWindow and force a
        // swapchain recreate on the next Present() / AcquireNextImage().
        LOG_INFO(Render_Vulkan, "Detected ANativeWindow change, recreating vk::SurfaceKHR");
        return RefreshSurfaceFromNewANativeWindow();
    }
    return false;
#else
    return false;
#endif
}

bool Swapchain::WaitForFreshSurface() {
#if defined(__ANDROID__)
    // Try the immediate poll first (covers the common case where SDL has
    // already delivered the new ANativeWindow before we got here).
    if (RefreshSurfaceIfNeeded()) {
        return true;
    }

    // We *know* the surface is lost (caller only invokes this from the
    // AcquireNextImage-failure path). SDL3's `onNativeSurfaceDestroyed`
    // sets the internal data->native_window to NULL but does NOT update
    // the SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER property — that only
    // happens later in `onNativeSurfaceCreated` (which is fired by the
    // Java side after the new Surface is constructed).
    //
    // During an Android orientation transition there is a window of
    // 100-500ms where the old ANativeWindow is dead but SDL hasn't yet
    // received the new one. Polling once at that moment returns false;
    // if we just gave up, the screen would stay black forever (because
    // Present() may not be called again until the game's main loop
    // issues its next flip, which on interpreter-backed Android can take
    // a long time, and may even deadlock on other emulator init steps).
    //
    // We use TWO independent signals here:
    //   (1) `Shadps4Activity.sSurfaceGeneration` — a Java-side counter
    //       bumped synchronously inside our `Shadps4Surface.surfaceCreated`
    //       hook BEFORE calling `super.surfaceCreated` (which fires SDL3's
    //       onNativeSurfaceCreated and updates the SDL3 property).
    //       Authoritative: guaranteed to fire on every surfaceCreated.
    //   (2) `PollAndroidNativeWindow()` — re-reads SDL3's property and
    //       compares to the cached value. Sometimes returns false even
    //       after a fresh surface if SDL3 hasn't yet processed the Java
    //       callback from this thread's POV.
    //
    // Either signal triggers a `RefreshSurfaceIfNeeded()` which
    // re-creates the vk::SurfaceKHR from the current SDL3 property.
    constexpr int kMaxRetries = 40; // 40 × 25ms = 1000ms
    constexpr auto kRetryDelay = std::chrono::milliseconds(25);
    LOG_WARNING(Render_Vulkan,
                "Surface lost; waiting up to {}ms for SDL/Java to deliver new ANativeWindow",
                kMaxRetries * 25);
    const int initial_java_gen = Frontend::WindowSDL::GetJavaSurfaceGeneration();
    for (int i = 0; i < kMaxRetries; ++i) {
        std::this_thread::sleep_for(kRetryDelay);
        // (1) Check Java counter first — it fires BEFORE SDL3's property
        // is updated, so as soon as we see a bump we know a fresh
        // surface is on the way. We then call RefreshSurfaceIfNeeded()
        // which polls SDL3's property — but SDL3's update happens
        // inside super.surfaceCreated which runs synchronously after
        // our counter bump, so by the time we observe a new generation
        // the property is guaranteed to be updated.
        const int java_gen = Frontend::WindowSDL::GetJavaSurfaceGeneration();
        if (java_gen != initial_java_gen && java_gen != -1) {
            LOG_INFO(Render_Vulkan,
                      "Java surface generation changed: {} -> {} (after {}ms); "
                      "refreshing vk::SurfaceKHR",
                      initial_java_gen, java_gen, (i + 1) * 25);
            // SDL3 property update happens synchronously inside our
            // surfaceCreated hook, so the property is already updated.
            // Poll it once to refresh our cache and recreate the surface.
            if (RefreshSurfaceIfNeeded()) {
                return true;
            }
            // Edge case: Java bumped but SDL3's property still hasn't
            // been observed to change from this thread. Spin a couple
            // more times to give SDL3 a chance to publish.
            for (int j = 0; j < 4; ++j) {
                std::this_thread::sleep_for(kRetryDelay);
                if (RefreshSurfaceIfNeeded()) {
                    LOG_INFO(Render_Vulkan,
                              "Got fresh ANativeWindow after Java gen bump "
                              "({} extra spins, {}ms total)",
                              j + 1, (i + 1 + j + 1) * 25);
                    return true;
                }
            }
            // SDL3's property update seems to be lagging. Force a
            // surface recreate from the Java-bumped ANativeWindow
            // pointer that should already be in window_info if SDL3
            // processed surfaceCreated (we can verify by reading the
            // property directly via PollAndroidNativeWindow once more).
            LOG_WARNING(Render_Vulkan,
                        "Java reported surfaceCreated but SDL3 property still "
                        "stale; forcing recreate from cached ANativeWindow");
            return RefreshSurfaceFromNewANativeWindow();
        }
        // (2) Fallback: directly poll SDL3's property. Catches the case
        // where Java counter was bumped but we somehow missed the change
        // (e.g. JNI returned -1).
        if (RefreshSurfaceIfNeeded()) {
            LOG_INFO(Render_Vulkan,
                      "Got fresh ANativeWindow after {} retries ({}ms) via SDL3 "
                      "property poll",
                      i + 1, (i + 1) * 25);
            return true;
        }
    }
    LOG_ERROR(Render_Vulkan,
              "ANativeWindow did not change after {}ms (java_gen: initial={} final={}); "
              "giving up this frame (will retry on next Present())",
              kMaxRetries * 25, initial_java_gen,
              Frontend::WindowSDL::GetJavaSurfaceGeneration());
    return false;
#else
    return false;
#endif
}

void Swapchain::Create(u32 width_, u32 height_) {
    width = width_;
    height = height_;
    needs_recreation = false;

    Destroy();

#if defined(__ANDROID__)
    // The vk::SurfaceKHR may not be available yet (e.g. before the first
    // ANativeWindow is ready, or right after an orientation transition).
    // Skip swapchain creation; the next RefreshSurfaceIfNeeded() will
    // recreate the surface and trigger another Recreate() call.
    if (!surface) {
        LOG_WARNING(Render_Vulkan,
                    "Swapchain::Create called without a surface; deferring creation");
        return;
    }
#endif

    SetSurfaceProperties();

    const std::array queue_family_indices = {
        instance.GetGraphicsQueueFamilyIndex(),
        instance.GetPresentQueueFamilyIndex(),
    };

    const bool exclusive = queue_family_indices[0] == queue_family_indices[1];
    const u32 queue_family_indices_count = exclusive ? 1u : 2u;
    const vk::SharingMode sharing_mode =
        exclusive ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent;
    const auto format = needs_hdr ? SURFACE_FORMAT_HDR : surface_format;
    const vk::SwapchainCreateInfoKHR swapchain_info = {
        .surface = surface,
        .minImageCount = image_count,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
                      vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
        .imageSharingMode = sharing_mode,
        .queueFamilyIndexCount = queue_family_indices_count,
        .pQueueFamilyIndices = queue_family_indices.data(),
        .preTransform = transform,
        .compositeAlpha = composite_alpha,
        .presentMode = present_mode,
        .clipped = true,
        .oldSwapchain = nullptr,
    };

    auto [swapchain_result, chain] = instance.GetDevice().createSwapchainKHR(swapchain_info);
#if defined(__ANDROID__)
    if (swapchain_result != vk::Result::eSuccess) {
        LOG_ERROR(Render_Vulkan, "Failed to create swapchain: {}",
                  vk::to_string(swapchain_result));
        return;
    }
#else
    ASSERT_MSG(swapchain_result == vk::Result::eSuccess, "Failed to create swapchain: {}",
               vk::to_string(swapchain_result));
#endif
    swapchain = chain;

    SetupImages();
    RefreshSemaphores();
}

void Swapchain::Recreate(u32 width_, u32 height_) {
    LOG_DEBUG(Render_Vulkan, "Recreate the swapchain: width={} height={} HDR={}", width_, height_,
              needs_hdr);
    Create(width_, height_);
}

void Swapchain::SetHDR(bool hdr) {
    if (needs_hdr == hdr) {
        return;
    }

    auto result = instance.GetDevice().waitIdle();
    if (result != vk::Result::eSuccess) {
        LOG_WARNING(ImGui, "Failed to wait for Vulkan device idle on mode change: {}",
                    vk::to_string(result));
    }

    needs_hdr = hdr;
    Recreate(width, height);
    ImGui::Core::OnSurfaceFormatChange(needs_hdr ? SURFACE_FORMAT_HDR.format
                                                 : surface_format.format);
}

bool Swapchain::AcquireNextImage() {
    if (!swapchain) {
        // No swapchain (e.g. waiting for surface). Force a recreation next
        // time the presenter gets a chance.
        needs_recreation = true;
        return false;
    }
    vk::Device device = instance.GetDevice();
    vk::Result result =
        device.acquireNextImageKHR(swapchain, std::numeric_limits<u64>::max(),
                                   image_acquired[frame_index], VK_NULL_HANDLE, &image_index);

    switch (result) {
    case vk::Result::eSuccess:
        break;
    case vk::Result::eSuboptimalKHR:
    case vk::Result::eErrorSurfaceLostKHR:
    case vk::Result::eErrorOutOfDateKHR:
    case vk::Result::eErrorUnknown:
        needs_recreation = true;
        break;
    default:
        LOG_CRITICAL(Render_Vulkan, "Swapchain acquire returned unknown result {}",
                     vk::to_string(result));
        UNREACHABLE();
        break;
    }

    return !needs_recreation;
}

bool Swapchain::Present() {
    if (!swapchain) {
        needs_recreation = true;
        return false;
    }
    const vk::PresentInfoKHR present_info = {
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &present_ready[image_index],
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
    };

    auto result = instance.GetPresentQueue().presentKHR(present_info);
    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
        needs_recreation = true;
    } else {
        ASSERT_MSG(result == vk::Result::eSuccess, "Swapchain presentation failed: {}",
                   vk::to_string(result));
    }

    frame_index = (frame_index + 1) % image_count;

    return !needs_recreation;
}

void Swapchain::FindPresentFormat() {
#if defined(__ANDROID__)
    if (!surface) {
        // Pick a safe default until a real surface is available; the next
        // RefreshSurfaceIfNeeded() will re-query when the surface is back.
        surface_format.format = vk::Format::eR8G8B8A8Unorm;
        surface_format.colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        supports_hdr = false;
        return;
    }
#endif
    const auto [formats_result, formats] =
        instance.GetPhysicalDevice().getSurfaceFormatsKHR(surface);
#if defined(__ANDROID__)
    if (formats_result != vk::Result::eSuccess) {
        LOG_ERROR(Render_Vulkan, "Failed to query surface formats: {}; using RGBA8 sRGB default",
                  vk::to_string(formats_result));
        surface_format.format = vk::Format::eR8G8B8A8Unorm;
        surface_format.colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        supports_hdr = false;
        return;
    }
#else
    ASSERT_MSG(formats_result == vk::Result::eSuccess, "Failed to query surface formats: {}",
               vk::to_string(formats_result));
#endif

    // Check if the device supports HDR formats. Here we care of Rec.2020 PQ only as it is expected
    // game output. Other variants as e.g. linear Rec.2020 will require additional color space
    // rotation
    supports_hdr =
        std::find_if(formats.begin(), formats.end(), [](const vk::SurfaceFormatKHR& format) {
            return format == SURFACE_FORMAT_HDR;
        }) != formats.end();
    // Also make sure that user allowed us to use HDR
    supports_hdr &= EmulatorSettings.IsHdrAllowed();

    // If there is a single undefined surface format, the device doesn't care, so we'll just use
    // RGBA sRGB.
    if (formats[0].format == vk::Format::eUndefined) {
        surface_format.format = vk::Format::eR8G8B8A8Unorm;
        surface_format.colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        return;
    }

    // Try to find a suitable format.
    for (const vk::SurfaceFormatKHR& sformat : formats) {
        vk::Format format = sformat.format;
        if (format != vk::Format::eR8G8B8A8Unorm && format != vk::Format::eB8G8R8A8Unorm) {
            continue;
        }

        surface_format.format = format;
        surface_format.colorSpace = sformat.colorSpace;
        return;
    }

    UNREACHABLE_MSG("Unable to find required swapchain format!");
}

void Swapchain::FindPresentMode() {
#if defined(__ANDROID__)
    if (!surface) {
        // FIFO is guaranteed by the Vulkan spec; use it as a placeholder
        // until a real surface is available.
        present_mode = vk::PresentModeKHR::eFifo;
        return;
    }
#endif
    const auto [modes_result, modes] =
        instance.GetPhysicalDevice().getSurfacePresentModesKHR(surface);
    if (modes_result != vk::Result::eSuccess) {
        LOG_ERROR(Render, "Failed to query available present modes, falling back to Fifo as "
                          "guaranteed supported option.");
        present_mode = vk::PresentModeKHR::eFifo;
        return;
    }

    const auto requested_mode = EmulatorSettings.GetPresentMode();
    if (requested_mode == "Mailbox") {
        present_mode = vk::PresentModeKHR::eMailbox;
    } else if (requested_mode == "Fifo") {
        present_mode = vk::PresentModeKHR::eFifo;
    } else if (requested_mode == "Immediate") {
        present_mode = vk::PresentModeKHR::eImmediate;
    } else {
        LOG_ERROR(Render_Vulkan, "Unknown present mode {}, defaulting to Mailbox.",
                  EmulatorSettings.GetPresentMode());
        present_mode = vk::PresentModeKHR::eMailbox;
    }

    if (std::ranges::find(modes, present_mode) == modes.cend()) {
        // FIFO is guaranteed to be supported by the Vulkan spec.
        constexpr auto fallback = vk::PresentModeKHR::eFifo;
        LOG_WARNING(Render, "Requested present mode {} is not supported, falling back to {}.",
                    vk::to_string(present_mode), vk::to_string(fallback));
        present_mode = fallback;
    }
}

void Swapchain::SetSurfaceProperties() {
    const auto [capabilities_result, capabilities] =
        instance.GetPhysicalDevice().getSurfaceCapabilitiesKHR(surface);
#if defined(__ANDROID__)
    // On Android, the surface may be lost during orientation changes or
    // when the activity goes through lifecycle transitions. Don't abort;
    // log the error and fall back to the current window dimensions so the
    // swapchain we eventually create matches the user's actual screen
    // geometry once the surface is re-acquired.
    //
    // Also poll SDL for a fresh window size — `width`/`height` cached in
    // WindowSDL may still be the pre-orientation portrait dimensions even
    // though the orientation transition has already happened at the Android
    // system level (SDL may not have processed SDL_EVENT_WINDOW_RESIZED
    // yet, but SDL_GetWindowSizeInPixels reads the live ANativeWindow
    // geometry).
    if (capabilities_result != vk::Result::eSuccess || !surface) {
        window.PollWindowSize();
        LOG_ERROR(Render_Vulkan, "Failed to query surface capabilities: {}; using window {}x{}",
                  vk::to_string(capabilities_result), width, height);
        extent = vk::Extent2D{static_cast<u32>(width), static_cast<u32>(height)};
        if (extent.width == 0 || extent.height == 0) {
            extent = vk::Extent2D{1280, 720};
        }
        image_count = 3;
        transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
        composite_alpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
        return;
    }
#else
    ASSERT_MSG(capabilities_result == vk::Result::eSuccess,
               "Failed to query surface capabilities: {}", vk::to_string(capabilities_result));
#endif

    extent = capabilities.currentExtent;
    if (capabilities.currentExtent.width == std::numeric_limits<u32>::max()) {
        extent.width = std::max(capabilities.minImageExtent.width,
                                std::min(capabilities.maxImageExtent.width, width));
        extent.height = std::max(capabilities.minImageExtent.height,
                                 std::min(capabilities.maxImageExtent.height, height));
    }

    // Select number of images in swap chain, we prefer one buffer in the background to work on
    image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0) {
        image_count = std::min(image_count, capabilities.maxImageCount);
    }

    // Prefer identity transform if possible
    transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    if (!(capabilities.supportedTransforms & transform)) {
        transform = capabilities.currentTransform;
    }

    // Opaque is not supported everywhere.
    composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    if (!(capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque)) {
        composite_alpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
    }
}

void Swapchain::Destroy() {
    vk::Device device = instance.GetDevice();
    const auto wait_result = device.waitIdle();
    if (wait_result != vk::Result::eSuccess) {
        LOG_WARNING(Render_Vulkan, "Failed to wait for device to become idle: {}",
                    vk::to_string(wait_result));
    }

    for (auto& image_view : images_view) {
        device.destroyImageView(image_view);
    }
    images_view.clear();

    if (swapchain) {
        device.destroySwapchainKHR(swapchain);
    }

    for (const auto& sem : image_acquired) {
        device.destroySemaphore(sem);
    }
    for (const auto& sem : present_ready) {
        device.destroySemaphore(sem);
    }

    image_acquired.clear();
    present_ready.clear();
}

void Swapchain::RefreshSemaphores() {
    const vk::Device device = instance.GetDevice();
    image_acquired.resize(image_count);
    present_ready.resize(image_count);

    for (vk::Semaphore& semaphore : image_acquired) {
        auto [semaphore_result, sem] = device.createSemaphore({});
        ASSERT_MSG(semaphore_result == vk::Result::eSuccess,
                   "Failed to create image acquired semaphore: {}",
                   vk::to_string(semaphore_result));
        semaphore = sem;
    }
    for (vk::Semaphore& semaphore : present_ready) {
        auto [semaphore_result, sem] = device.createSemaphore({});
        ASSERT_MSG(semaphore_result == vk::Result::eSuccess,
                   "Failed to create present ready semaphore: {}", vk::to_string(semaphore_result));
        semaphore = sem;
    }

    for (u32 i = 0; i < image_count; ++i) {
        SetObjectName(device, image_acquired[i], "Swapchain Semaphore: image_acquired {}", i);
        SetObjectName(device, present_ready[i], "Swapchain Semaphore: present_ready {}", i);
    }
}

void Swapchain::SetupImages() {
    vk::Device device = instance.GetDevice();
    auto [images_result, imgs] = device.getSwapchainImagesKHR(swapchain);
    ASSERT_MSG(images_result == vk::Result::eSuccess, "Failed to create swapchain images: {}",
               vk::to_string(images_result));
    images = std::move(imgs);
    image_count = static_cast<u32>(images.size());
    images_view.resize(image_count);
    for (u32 i = 0; i < image_count; ++i) {
        if (images_view[i]) {
            device.destroyImageView(images_view[i]);
        }
        auto [im_view_result, im_view] = device.createImageView(vk::ImageViewCreateInfo{
            .image = images[i],
            .viewType = vk::ImageViewType::e2D,
            .format = needs_hdr ? SURFACE_FORMAT_HDR.format : surface_format.format,
            .subresourceRange =
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .levelCount = 1,
                    .layerCount = 1,
                },
        });
        ASSERT_MSG(im_view_result == vk::Result::eSuccess, "Failed to create image view: {}",
                   vk::to_string(im_view_result));
        images_view[i] = im_view;
    }

    for (u32 i = 0; i < image_count; ++i) {
        SetObjectName(device, images[i], "Swapchain Image {}", i);
        SetObjectName(device, images_view[i], "Swapchain ImageView {}", i);
    }
}

} // namespace Vulkan
