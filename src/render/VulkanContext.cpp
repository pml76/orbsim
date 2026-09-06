#include "render/VulkanContext.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <expected>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

namespace orb::gfx {
namespace {

// Reverse-Z: the depth buffer is cleared to 0.0 and the pipelines compare with
// GREATER. Spreading float precision evenly across a range that runs from a
// cockpit panel a metre away to a planet a hundred million kilometres out is
// only possible this way; a conventional 0..1 depth buffer z-fights badly long
// before it reaches those distances. See docs/adr/0003.
constexpr float kDepthClear = 0.0F;

enum class FenceState {
    Unsignalled,
    Signalled,
};

[[nodiscard]] std::unexpected<RenderError> fail(std::string message) {
    return std::unexpected(RenderError{.message = std::move(message)});
}

[[nodiscard]] std::unexpected<RenderError> failSdl(std::string_view what) {
    return fail(std::string(what) + " failed: " + SDL_GetError());
}

// The spec's name for a result, for messages. Vulkan-Headers no longer ships
// vk_enum_string_helper.h (it moved to Vulkan-Utility-Libraries), and a
// dependency for one function is not worth it. Only the results this renderer
// can meet are named; anything else shows its number, which is still enough to
// look up.
[[nodiscard]] std::string resultName(VkResult result) {
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_OUT_OF_POOL_MEMORY:
        return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
        return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
    default:
        return "VkResult " + std::to_string(static_cast<int>(result));
    }
}

// Every Vulkan and VMA call that returns a VkResult goes through here, so that
// none is dropped. This is rule 7 of the Power of Ten -- check the return
// value of every non-void function -- for the one API that [[nodiscard]]
// cannot reach. The name of the result is the useful part of the message:
// VK_ERROR_DEVICE_LOST and VK_ERROR_OUT_OF_DEVICE_MEMORY call for different
// responses.
[[nodiscard]] std::expected<void, RenderError> vkCheck(VkResult result, std::string_view what) {
    if (result == VK_SUCCESS) return {};
    return fail(std::string(what) + " failed: " + resultName(result));
}

[[nodiscard]] std::expected<UniqueSemaphore, RenderError> makeSemaphore(VkDevice device) {
    const VkSemaphoreCreateInfo ci{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore semaphore = VK_NULL_HANDLE;
    if (auto ok = vkCheck(vkCreateSemaphore(device, &ci, nullptr, &semaphore), "vkCreateSemaphore");
        !ok) {
        return std::unexpected(ok.error());
    }
    return UniqueSemaphore{device, semaphore};
}

[[nodiscard]] std::expected<UniqueFence, RenderError> makeFence(VkDevice device, FenceState state) {
    const VkFenceCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags =
            state == FenceState::Signalled ? VK_FENCE_CREATE_SIGNALED_BIT : VkFenceCreateFlags{0},
    };
    VkFence fence = VK_NULL_HANDLE;
    if (auto ok = vkCheck(vkCreateFence(device, &ci, nullptr, &fence), "vkCreateFence"); !ok) {
        return std::unexpected(ok.error());
    }
    return UniqueFence{device, fence};
}

[[nodiscard]] std::expected<UniqueCommandPool, RenderError> makeCommandPool(VkDevice device,
                                                                            uint32_t queueFamily) {
    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamily,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    if (auto ok =
            vkCheck(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "vkCreateCommandPool");
        !ok) {
        return std::unexpected(ok.error());
    }
    return UniqueCommandPool{device, pool};
}

// Resets a command buffer and opens it for a single submission. Both the frame
// loop and the upload path record this way.
[[nodiscard]] std::expected<void, RenderError> beginOneTimeCommandBuffer(VkCommandBuffer cmd) {
    if (auto ok = vkCheck(vkResetCommandBuffer(cmd, 0), "vkResetCommandBuffer"); !ok) return ok;

    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    return vkCheck(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");
}

[[nodiscard]] std::expected<VkCommandBuffer, RenderError>
allocatePrimaryCommandBuffer(VkDevice device, VkCommandPool pool) {
    const VkCommandBufferAllocateInfo info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (auto ok =
            vkCheck(vkAllocateCommandBuffers(device, &info, &cmd), "vkAllocateCommandBuffers");
        !ok) {
        return std::unexpected(ok.error());
    }
    return cmd;
}

// Where the validation layers report. Errors are counted, so that a run can
// fail on them -- that is what turns `orbsim --validate --seconds 2` into a
// test rather than a log to read -- and everything at warning level and above
// is logged. The counter is the caller's (see VulkanContext::create) and is
// atomic because the specification allows this callback on any thread.
VKAPI_ATTR VkBool32 VKAPI_CALL onValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                   void* userData) {
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        auto* errors = static_cast<std::atomic<uint32_t>*>(userData);
        if (errors != nullptr) errors->fetch_add(1, std::memory_order_relaxed);
    }
    SDL_Log("[validation] %s", data != nullptr ? data->pMessage : "(no message)");
    // The specification requires VK_FALSE from an application callback: VK_TRUE
    // would abort the call that triggered the message.
    return VK_FALSE;
}

// The instance-creation and device-selection steps both need the vkb::Instance,
// but that type must not appear in the header -- a renderer header that drags
// vk-bootstrap in makes every translation unit pay for it. Keeping these as
// file-local functions that hand back a small bundle solves both problems.

struct InstanceBundle {
    vkb::Instance instance;
    VkSurfaceKHR surface{VK_NULL_HANDLE};
};

[[nodiscard]] std::expected<InstanceBundle, RenderError> makeInstanceAndSurface(
    SDL_Window* window, Validation validation, std::atomic<uint32_t>& validationErrors) {
    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
    if (sdlExts == nullptr) return failSdl("SDL_Vulkan_GetInstanceExtensions");

    const auto build = [&](Validation requested) {
        vkb::InstanceBuilder builder;
        builder.set_app_name("orbsim").set_engine_name("orbsim").require_api_version(1, 3, 0);
        for (uint32_t i = 0; i < sdlExtCount; ++i)
            builder.enable_extension(sdlExts[i]);
        if (requested == Validation::Enabled) {
            builder.request_validation_layers()
                .set_debug_callback(onValidationMessage)
                .set_debug_callback_user_data_pointer(&validationErrors)
                .set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
        }
        return builder.build();
    };

    // Validation layers ship with the SDK, not the driver, so a machine with
    // only a runtime will not have them. Fall back rather than refusing to run.
    auto built = build(validation);
    if (!built && validation == Validation::Enabled) {
        SDL_Log("Validation layers unavailable (%s); continuing without them.",
                built.error().message().c_str());
        built = build(Validation::Disabled);
    }
    if (!built) return fail("Vulkan instance creation failed: " + built.error().message());

    InstanceBundle bundle{.instance = built.value(), .surface = VK_NULL_HANDLE};
    if (!SDL_Vulkan_CreateSurface(window, bundle.instance.instance, nullptr, &bundle.surface)) {
        return failSdl("SDL_Vulkan_CreateSurface");
    }
    return bundle;
}

struct DeviceBundle {
    vkb::Device device;
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkQueue graphicsQueue{VK_NULL_HANDLE};
    uint32_t graphicsQueueFamily{0};
    std::string name;
};

[[nodiscard]] std::expected<DeviceBundle, RenderError> makeDevice(const vkb::Instance& instance,
                                                                  VkSurfaceKHR surface) {
    // Dynamic rendering and synchronization2 are the two 1.3 features this
    // renderer is built around; there is no fallback path for them.
    VkPhysicalDeviceVulkan13Features const features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features const features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .bufferDeviceAddress = VK_TRUE,
    };
    VkPhysicalDeviceFeatures const features10{
        .fillModeNonSolid = VK_TRUE, // wireframe, for debugging meshes
        .wideLines = VK_FALSE,       // widely unsupported; lines stay 1px
    };

    vkb::PhysicalDeviceSelector selector{instance};
    auto physical = selector.set_surface(surface)
                        .set_minimum_version(1, 3)
                        .set_required_features(features10)
                        .set_required_features_12(features12)
                        .set_required_features_13(features13)
                        .select();
    if (!physical) return fail("No suitable Vulkan 1.3 device: " + physical.error().message());

    vkb::DeviceBuilder const deviceBuilder{physical.value()};
    auto device = deviceBuilder.build();
    if (!device) return fail("Vulkan device creation failed: " + device.error().message());

    auto queue = device.value().get_queue(vkb::QueueType::graphics);
    auto family = device.value().get_queue_index(vkb::QueueType::graphics);
    if (!queue || !family) return fail("No graphics queue available");

    return DeviceBundle{
        .device = device.value(),
        .physicalDevice = physical.value().physical_device,
        .graphicsQueue = queue.value(),
        .graphicsQueueFamily = family.value(),
        .name = physical.value().name,
    };
}

} // namespace

// ---------------------------------------------------------------------------

void transitionImage(VkCommandBuffer cmd,
                     VkImage image,
                     VkImageLayout from,
                     VkImageLayout to,
                     VkImageAspectFlags aspect) {
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        // ALL_COMMANDS is heavier than necessary, but this renderer makes only
        // a couple of transitions per frame and correctness is worth more here
        // than shaving a pipeline stall.
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange =
            {
                .aspectMask = aspect,
                .baseMipLevel = 0,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
    };

    const VkDependencyInfo dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

// ---------------------------------------------------------------------------

std::expected<VulkanContext, RenderError> VulkanContext::create(
    SDL_Window* window, Validation validation, std::atomic<uint32_t>& validationErrors) {
    VulkanContext ctx;
    ctx.window_ = window;

    auto instance = makeInstanceAndSurface(window, validation, validationErrors);
    if (!instance) return std::unexpected(instance.error());

    // Wrapped immediately, so that from here on an early return destroys them.
    // That is the difference this refactor makes: there is no teardown path to
    // remember, because there is no way to leave without running one.
    ctx.instance_ = UniqueInstance{instance->instance.instance};
    ctx.debugMessenger_ =
        UniqueDebugMessenger{ctx.instance_.get(), instance->instance.debug_messenger};
    ctx.surface_ = UniqueSurface{ctx.instance_.get(), instance->surface};

    auto device = makeDevice(instance->instance, ctx.surface_.get());
    if (!device) return std::unexpected(device.error());

    ctx.device_ = UniqueDevice{device->device.device};
    ctx.physicalDevice_ = device->physicalDevice;
    ctx.graphicsQueue_ = device->graphicsQueue;
    ctx.graphicsQueueFamily_ = device->graphicsQueueFamily;
    ctx.deviceName_ = std::move(device->name);
    ctx.idleGuard_ = DeviceIdleGuard{ctx.device_.get()};

    if (auto step = ctx.createAllocator(); !step) return std::unexpected(step.error());
    if (auto step = ctx.createFrameResources(); !step) return std::unexpected(step.error());
    if (auto step = ctx.createUploadContext(); !step) return std::unexpected(step.error());
    if (auto step = ctx.createSwapchain(); !step) return std::unexpected(step.error());

    return ctx;
}

std::expected<void, RenderError> VulkanContext::createAllocator() {
    VmaVulkanFunctions const vulkanFunctions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };
    const VmaAllocatorCreateInfo allocatorInfo{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = physicalDevice_,
        .device = device_.get(),
        .pVulkanFunctions = &vulkanFunctions,
        .instance = instance_.get(),
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };

    VmaAllocator allocator = nullptr;
    if (auto ok = vkCheck(vmaCreateAllocator(&allocatorInfo, &allocator), "vmaCreateAllocator");
        !ok) {
        return ok;
    }
    allocator_ = UniqueAllocator{allocator};
    return {};
}

std::expected<void, RenderError> VulkanContext::createFrameResources() {
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        auto pool = makeCommandPool(device_.get(), graphicsQueueFamily_);
        if (!pool) return std::unexpected(pool.error());
        commandPools_.at(i) = std::move(*pool);

        auto cmd = allocatePrimaryCommandBuffer(device_.get(), commandPools_.at(i).get());
        if (!cmd) return std::unexpected(cmd.error());
        commandBuffers_.at(i) = *cmd;

        auto available = makeSemaphore(device_.get());
        if (!available) return std::unexpected(available.error());
        imageAvailable_.at(i) = std::move(*available);

        // Signalled, so that the first wait on each slot returns at once.
        auto fence = makeFence(device_.get(), FenceState::Signalled);
        if (!fence) return std::unexpected(fence.error());
        inFlight_.at(i) = std::move(*fence);
    }
    return {};
}

std::expected<void, RenderError> VulkanContext::createUploadContext() {
    auto pool = makeCommandPool(device_.get(), graphicsQueueFamily_);
    if (!pool) return std::unexpected(pool.error());
    uploadPool_ = std::move(*pool);

    auto cmd = allocatePrimaryCommandBuffer(device_.get(), uploadPool_.get());
    if (!cmd) return std::unexpected(cmd.error());
    uploadCmd_ = *cmd;

    auto fence = makeFence(device_.get(), FenceState::Unsignalled);
    if (!fence) return std::unexpected(fence.error());
    uploadFence_ = std::move(*fence);
    return {};
}

std::expected<void, RenderError> VulkanContext::createSwapchain() {
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window_, &width, &height)) {
        return failSdl("SDL_GetWindowSizeInPixels");
    }
    if (width <= 0 || height <= 0) return fail("Window has zero size");

    // Children before parents. The image views reference the swapchain and
    // the depth view references the depth image; Vulkan wants them gone
    // before the objects they were created from. Everything here is idle --
    // recreateSwapchain waited for the device -- so the order is the only
    // thing that matters.
    swapchainViews_.clear();
    renderFinished_.clear();
    depthView_.reset();
    depthImage_.reset();

    vkb::SwapchainBuilder builder{
        physicalDevice_, device_.get(), surface_.get(), graphicsQueueFamily_, graphicsQueueFamily_};

    // UNORM rather than SRGB: the shaders write display-ready colours directly,
    // so an automatic linear-to-sRGB conversion would wash them out.
    auto built =
        builder
            .set_desired_format(VkSurfaceFormatKHR{.format = VK_FORMAT_B8G8R8A8_UNORM,
                                                   .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            // Mailbox keeps latency low without tearing. FIFO is the
            // required fallback and is always present.
            .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
            .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(static_cast<uint32_t>(width), static_cast<uint32_t>(height))
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            // A surface may have only one live swapchain. Handing the old one
            // over retires it, which is what lets the new one be created while
            // the old is still owned by swapchain_ below.
            .set_old_swapchain(swapchain_.get())
            .build();
    if (!built) return fail("Swapchain creation failed: " + built.error().message());

    vkb::Swapchain vkbSwapchain = built.value();
    swapchain_ = UniqueSwapchain{device_.get(), vkbSwapchain.swapchain}; // destroys the retired one
    swapchainFormat_ = vkbSwapchain.image_format;
    swapchainExtent_ = vkbSwapchain.extent;

    auto images = vkbSwapchain.get_images();
    if (!images) return fail("Swapchain images unavailable: " + images.error().message());
    swapchainImages_ = std::move(images.value());

    // vkb hands back raw views; wrap each one so the vector owns them.
    auto views = vkbSwapchain.get_image_views();
    if (!views) return fail("Swapchain image views unavailable: " + views.error().message());
    swapchainViews_.reserve(views.value().size());
    for (VkImageView view : views.value()) {
        swapchainViews_.emplace_back(device_.get(), view);
    }

    renderFinished_.reserve(swapchainImages_.size());
    for (size_t i = 0; i < swapchainImages_.size(); ++i) {
        auto finished = makeSemaphore(device_.get());
        if (!finished) return std::unexpected(finished.error());
        renderFinished_.push_back(std::move(*finished));
    }

    if (auto depth = createDepthAttachment(); !depth) return depth;

    swapchainDirty_ = false;
    return {};
}

// Split out of createSwapchain: the swapchain and its depth buffer are two
// resources with two failure modes, and one function doing both could only
// report them with the same undifferentiated failure.
std::expected<void, RenderError> VulkanContext::createDepthAttachment() {
    const VkImageCreateInfo depthInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = kDepthFormat,
        .extent = {.width = swapchainExtent_.width, .height = swapchainExtent_.height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo depthAlloc{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    if (auto ok = vkCheck(
            vmaCreateImage(allocator_.get(), &depthInfo, &depthAlloc, &image, &allocation, nullptr),
            "vmaCreateImage (depth)");
        !ok) {
        return ok;
    }
    depthImage_ = UniqueImage{allocator_.get(), image, allocation};

    const VkImageViewCreateInfo depthViewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depthImage_.get(),
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = kDepthFormat,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
    };
    VkImageView view = VK_NULL_HANDLE;
    if (auto ok = vkCheck(vkCreateImageView(device_.get(), &depthViewInfo, nullptr, &view),
                          "vkCreateImageView (depth)");
        !ok) {
        return ok;
    }
    depthView_ = UniqueImageView{device_.get(), view};

    return {};
}

std::expected<void, RenderError> VulkanContext::recreateSwapchain() {
    // Nothing may still be using the old swapchain when it is retired.
    if (auto idle = vkCheck(vkDeviceWaitIdle(device_.get()), "vkDeviceWaitIdle"); !idle) {
        return idle;
    }
    // There is no destroySwapchain(). createSwapchain reassigns every member
    // it owns, and assigning over a handle destroys the old one. That is the
    // whole point of the wrappers.
    return createSwapchain();
}

std::expected<std::optional<FrameContext>, RenderError> VulkanContext::beginFrame() {
    // A minimised window has a zero-size swapchain, which cannot be created.
    // Report no frame and let the caller idle.
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window_, &width, &height)) {
        return failSdl("SDL_GetWindowSizeInPixels");
    }
    if (width <= 0 || height <= 0) return std::nullopt;

    if (swapchainDirty_) {
        if (auto rebuilt = recreateSwapchain(); !rebuilt) return std::unexpected(rebuilt.error());
    }

    const uint32_t frame = frameIndex_;
    VkFence waitFence = inFlight_.at(frame).get();
    if (auto ok = vkCheck(vkWaitForFences(device_.get(), 1, &waitFence, VK_TRUE, UINT64_MAX),
                          "vkWaitForFences");
        !ok) {
        return std::unexpected(ok.error());
    }

    uint32_t imageIndex = 0;
    const VkResult acquire = vkAcquireNextImageKHR(device_.get(),
                                                   swapchain_.get(),
                                                   UINT64_MAX,
                                                   imageAvailable_.at(frame).get(),
                                                   VK_NULL_HANDLE,
                                                   &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        if (auto rebuilt = recreateSwapchain(); !rebuilt) return std::unexpected(rebuilt.error());
        return std::nullopt;
    }
    // Suboptimal still hands back a usable image; the rebuild happens after
    // this frame is presented.
    if (acquire != VK_SUBOPTIMAL_KHR) {
        if (auto ok = vkCheck(acquire, "vkAcquireNextImageKHR"); !ok) {
            return std::unexpected(ok.error());
        }
    }

    // Only reset the fence once the frame is definitely going to be submitted;
    // returning early above with the fence already reset would deadlock the
    // next wait on this slot.
    if (auto ok = vkCheck(vkResetFences(device_.get(), 1, &waitFence), "vkResetFences"); !ok) {
        return std::unexpected(ok.error());
    }

    VkCommandBuffer cmd = commandBuffers_.at(frame);
    if (auto ok = beginOneTimeCommandBuffer(cmd); !ok) return std::unexpected(ok.error());

    transitionImage(cmd,
                    swapchainImages_.at(imageIndex),
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    transitionImage(cmd,
                    depthImage_.get(),
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT);

    beginRendering(cmd, imageIndex);

    return FrameContext{
        .cmd = cmd, .imageIndex = imageIndex, .frameIndex = frame, .extent = swapchainExtent_};
}

// Split out of beginFrame: acquiring an image and describing a render pass are
// two jobs, and only the first of them can fail. F.2 -- a function does one
// thing.
void VulkanContext::beginRendering(VkCommandBuffer cmd, uint32_t imageIndex) const {
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainViews_.at(imageIndex).get(),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        // Deep space is not pure black: a very dark blue reads better than
        // #000000 once stars and a lit limb are in frame.
        .clearValue = {.color = {{0.004F, 0.006F, 0.012F, 1.0F}}},
    };
    const VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthView_.get(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {.depth = kDepthClear, .stencil = 0}},
    };

    const VkRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {.x = 0, .y = 0}, .extent = swapchainExtent_},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };
    vkCmdBeginRendering(cmd, &rendering);

    // Viewport is flipped vertically so that +Y is up in clip space, matching
    // the maths convention used throughout the sim rather than Vulkan's.
    const VkViewport viewport{
        .x = 0.0F,
        .y = static_cast<float>(swapchainExtent_.height),
        .width = static_cast<float>(swapchainExtent_.width),
        .height = -static_cast<float>(swapchainExtent_.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    };
    const VkRect2D scissor{.offset = {.x = 0, .y = 0}, .extent = swapchainExtent_};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

std::expected<void, RenderError> VulkanContext::endFrame(const FrameContext& frame) {
    vkCmdEndRendering(frame.cmd);

    transitionImage(frame.cmd,
                    swapchainImages_.at(frame.imageIndex),
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    if (auto ok = vkCheck(vkEndCommandBuffer(frame.cmd), "vkEndCommandBuffer"); !ok) return ok;

    const VkSemaphoreSubmitInfo waitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = imageAvailable_.at(frame.frameIndex).get(),
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkSemaphoreSubmitInfo signalInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = renderFinished_.at(frame.imageIndex).get(),
        .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    };
    const VkCommandBufferSubmitInfo cmdInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.cmd,
    };
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalInfo,
    };
    if (auto ok = vkCheck(
            vkQueueSubmit2(graphicsQueue_, 1, &submit, inFlight_.at(frame.frameIndex).get()),
            "vkQueueSubmit2");
        !ok) {
        return ok;
    }

    VkSemaphore presentWait = renderFinished_.at(frame.imageIndex).get();
    VkSwapchainKHR swapchain = swapchain_.get();
    const VkPresentInfoKHR present{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &presentWait,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &frame.imageIndex,
    };
    const VkResult presented = vkQueuePresentKHR(graphicsQueue_, &present);

    // The work was submitted whatever present said, so the slot advances.
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;

    // Both mean the window changed under us; the next beginFrame rebuilds.
    // Anything else is a real failure.
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        swapchainDirty_ = true;
        return {};
    }
    return vkCheck(presented, "vkQueuePresentKHR");
}

std::expected<void, RenderError> VulkanContext::waitIdle() const {
    if (!device_) return {};
    return vkCheck(vkDeviceWaitIdle(device_.get()), "vkDeviceWaitIdle");
}

// --- resources -------------------------------------------------------------

std::expected<UniqueBuffer, RenderError>
VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Memory memory) {
    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_AUTO};
    if (memory == Memory::HostVisible) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo info{};
    if (auto ok = vkCheck(
            vmaCreateBuffer(allocator_.get(), &bufferInfo, &allocInfo, &buffer, &allocation, &info),
            "vmaCreateBuffer");
        !ok) {
        return std::unexpected(ok.error());
    }
    return UniqueBuffer{allocator_.get(), buffer, allocation, info.pMappedData, size};
}

std::expected<void, RenderError> VulkanContext::uploadBuffer(UniqueBuffer& dst,
                                                             std::span<const std::byte> data) {
    if (data.empty()) return {};
    if (data.size() > dst.size()) return fail("Upload larger than destination buffer");

    // A host-visible destination can be written directly; the staging round
    // trip is only needed for device-local memory.
    if (dst.mapped() != nullptr) {
        std::memcpy(dst.mapped(), data.data(), data.size());
        return {};
    }

    auto staging = createBuffer(data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Memory::HostVisible);
    if (!staging) return std::unexpected(staging.error());
    if (staging->mapped() == nullptr) return fail("Staging buffer was not mapped");
    std::memcpy(staging->mapped(), data.data(), data.size());

    VkFence fence = uploadFence_.get();
    if (auto ok = vkCheck(vkResetFences(device_.get(), 1, &fence), "vkResetFences (upload)"); !ok) {
        return ok;
    }
    if (auto ok = beginOneTimeCommandBuffer(uploadCmd_); !ok) return ok;

    const VkBufferCopy copy{.srcOffset = 0, .dstOffset = 0, .size = data.size()};
    vkCmdCopyBuffer(uploadCmd_, staging->get(), dst.get(), 1, &copy);
    if (auto ok = vkCheck(vkEndCommandBuffer(uploadCmd_), "vkEndCommandBuffer (upload)"); !ok) {
        return ok;
    }

    const VkCommandBufferSubmitInfo cmdInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = uploadCmd_,
    };
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
    };
    if (auto ok =
            vkCheck(vkQueueSubmit2(graphicsQueue_, 1, &submit, fence), "vkQueueSubmit2 (upload)");
        !ok) {
        return ok;
    }
    // The staging buffer destroys itself on return, so the copy must be
    // complete before then, not merely submitted.
    return vkCheck(vkWaitForFences(device_.get(), 1, &fence, VK_TRUE, UINT64_MAX),
                   "vkWaitForFences (upload)");
}

std::expected<UniqueShaderModule, RenderError>
VulkanContext::loadShaderModule(const std::filesystem::path& path) const {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return fail("Cannot open shader: " + path.string());

    const std::streamsize size = file.tellg();
    if (size <= 0 || size % 4 != 0) {
        return fail("Shader is not valid SPIR-V (bad size): " + path.string());
    }

    std::vector<uint32_t> code(static_cast<size_t>(size) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(code.data()), size)) {
        return fail("Short read on shader: " + path.string());
    }

    const VkShaderModuleCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = static_cast<size_t>(size),
        .pCode = code.data(),
    };
    VkShaderModule module = VK_NULL_HANDLE;
    if (auto ok = vkCheck(vkCreateShaderModule(device_.get(), &info, nullptr, &module),
                          "vkCreateShaderModule");
        !ok) {
        return std::unexpected(ok.error());
    }
    return UniqueShaderModule{device_.get(), module};
}

} // namespace orb::gfx