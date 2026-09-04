#include "render/VulkanContext.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>

#include <cstring>
#include <expected>
#include <fstream>
#include <utility>

namespace orb::gfx {
namespace {

// Reverse-Z: the depth buffer is cleared to 0.0 and the pipelines compare with
// GREATER. Spreading float precision evenly across a range that runs from a
// cockpit panel a metre away to a planet a hundred million kilometres out is
// only possible this way; a conventional 0..1 depth buffer z-fights badly long
// before it reaches those distances.
constexpr float kDepthClear = 0.0F;

enum class FenceState {
    Unsignalled,
    Signalled,
};

[[nodiscard]] std::unexpected<InitError> fail(std::string message) {
    return std::unexpected(InitError{.message = std::move(message)});
}

[[nodiscard]] UniqueSemaphore makeSemaphore(VkDevice device) {
    const VkSemaphoreCreateInfo ci{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore semaphore = VK_NULL_HANDLE;
    vkCreateSemaphore(device, &ci, nullptr, &semaphore);
    return UniqueSemaphore{device, semaphore};
}

[[nodiscard]] UniqueFence makeFence(VkDevice device, FenceState state) {
    const VkFenceCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags =
            state == FenceState::Signalled ? VK_FENCE_CREATE_SIGNALED_BIT : VkFenceCreateFlags{0},
    };
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(device, &ci, nullptr, &fence);
    return UniqueFence{device, fence};
}

// The instance-creation and device-selection steps both need the vkb::Instance,
// but that type must not appear in the header -- a renderer header that drags
// vk-bootstrap in makes every translation unit pay for it. Keeping these as
// file-local functions that hand back a small bundle solves both problems.

struct InstanceBundle {
    vkb::Instance instance;
    VkSurfaceKHR surface{VK_NULL_HANDLE};
};

[[nodiscard]] std::expected<InstanceBundle, InitError>
makeInstanceAndSurface(SDL_Window* window, Validation validation) {
    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
    if (sdlExts == nullptr) {
        return fail(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }

    const auto build = [&](Validation requested) {
        vkb::InstanceBuilder builder;
        builder.set_app_name("orbsim").set_engine_name("orbsim").require_api_version(1, 3, 0);
        for (uint32_t i = 0; i < sdlExtCount; ++i)
            builder.enable_extension(sdlExts[i]);
        if (requested == Validation::Enabled) {
            builder.request_validation_layers().use_default_debug_messenger();
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
        return fail(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
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

[[nodiscard]] std::expected<DeviceBundle, InitError> makeDevice(const vkb::Instance& instance,
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

std::expected<VulkanContext, InitError> VulkanContext::create(SDL_Window* window,
                                                              Validation validation) {
    VulkanContext ctx;
    ctx.window_ = window;

    auto instance = makeInstanceAndSurface(window, validation);
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

std::expected<void, InitError> VulkanContext::createAllocator() {
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
    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        return fail("vmaCreateAllocator failed");
    }
    allocator_ = UniqueAllocator{allocator};
    return {};
}

std::expected<void, InitError> VulkanContext::createFrameResources() {
    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsQueueFamily_,
    };

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        VkCommandPool pool = VK_NULL_HANDLE;
        if (vkCreateCommandPool(device_.get(), &poolInfo, nullptr, &pool) != VK_SUCCESS) {
            return fail("vkCreateCommandPool failed");
        }
        commandPools_.at(i) = UniqueCommandPool{device_.get(), pool};

        const VkCommandBufferAllocateInfo cbInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPools_.at(i).get(),
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (vkAllocateCommandBuffers(device_.get(), &cbInfo, &commandBuffers_.at(i)) !=
            VK_SUCCESS) {
            return fail("vkAllocateCommandBuffers failed");
        }

        imageAvailable_.at(i) = makeSemaphore(device_.get());
        inFlight_.at(i) = makeFence(device_.get(), FenceState::Signalled);
    }
    return {};
}

std::expected<void, InitError> VulkanContext::createUploadContext() {
    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsQueueFamily_,
    };

    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device_.get(), &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        return fail("vkCreateCommandPool (upload) failed");
    }
    uploadPool_ = UniqueCommandPool{device_.get(), pool};

    const VkCommandBufferAllocateInfo uploadCbInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = uploadPool_.get(),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(device_.get(), &uploadCbInfo, &uploadCmd_) != VK_SUCCESS) {
        return fail("vkAllocateCommandBuffers (upload) failed");
    }

    uploadFence_ = makeFence(device_.get(), FenceState::Unsignalled);
    return {};
}

std::expected<void, InitError> VulkanContext::createSwapchain() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (width <= 0 || height <= 0) return fail("Window has zero size");

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
            .build();
    if (!built) return fail("Swapchain creation failed: " + built.error().message());

    vkb::Swapchain vkbSwapchain = built.value();
    swapchain_ = UniqueSwapchain{device_.get(), vkbSwapchain.swapchain};
    swapchainFormat_ = vkbSwapchain.image_format;
    swapchainExtent_ = vkbSwapchain.extent;
    swapchainImages_ = vkbSwapchain.get_images().value();

    // vkb hands back raw views; wrap each one so the vector owns them, and
    // clearing the vector destroys the previous set.
    swapchainViews_.clear();
    swapchainViews_.reserve(swapchainImages_.size());
    for (VkImageView view : vkbSwapchain.get_image_views().value()) {
        swapchainViews_.emplace_back(device_.get(), view);
    }

    renderFinished_.clear();
    renderFinished_.reserve(swapchainImages_.size());
    for (size_t i = 0; i < swapchainImages_.size(); ++i) {
        renderFinished_.push_back(makeSemaphore(device_.get()));
    }

    if (auto depth = createDepthAttachment(); !depth) return depth;

    swapchainDirty_ = false;
    return {};
}

// Split out of createSwapchain: the swapchain and its depth buffer are two
// resources with two failure modes, and one function doing both could only
// report them with the same undifferentiated failure.
std::expected<void, InitError> VulkanContext::createDepthAttachment() {
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
    if (vmaCreateImage(allocator_.get(), &depthInfo, &depthAlloc, &image, &allocation, nullptr) !=
        VK_SUCCESS) {
        return fail("Depth image allocation failed");
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
    if (vkCreateImageView(device_.get(), &depthViewInfo, nullptr, &view) != VK_SUCCESS) {
        return fail("Depth image view creation failed");
    }
    depthView_ = UniqueImageView{device_.get(), view};

    return {};
}

bool VulkanContext::recreateSwapchain() {
    vkDeviceWaitIdle(device_.get());

    // There is no destroySwapchain() any more. Assigning over each handle
    // destroys the old one, and createSwapchain reassigns every member it owns.
    // That is the whole point of the wrappers.
    if (auto rebuilt = createSwapchain(); !rebuilt) {
        SDL_Log("Swapchain rebuild failed: %s", rebuilt.error().message.c_str());
        return false;
    }
    return true;
}

std::optional<FrameContext> VulkanContext::beginFrame() {
    // A minimised window has a zero-size swapchain, which cannot be created.
    // Report no frame and let the caller idle.
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (width <= 0 || height <= 0) return std::nullopt;

    if (swapchainDirty_ && !recreateSwapchain()) return std::nullopt;

    const uint32_t frame = frameIndex_;
    VkFence waitFence = inFlight_.at(frame).get();
    vkWaitForFences(device_.get(), 1, &waitFence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    const VkResult acquire = vkAcquireNextImageKHR(device_.get(),
                                                   swapchain_.get(),
                                                   UINT64_MAX,
                                                   imageAvailable_.at(frame).get(),
                                                   VK_NULL_HANDLE,
                                                   &imageIndex);

    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return std::nullopt;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        SDL_Log("vkAcquireNextImageKHR failed: %d", static_cast<int>(acquire));
        return std::nullopt;
    }

    // Only reset the fence once the frame is definitely going to be submitted;
    // returning early above with the fence already reset would deadlock the
    // next wait on this slot.
    vkResetFences(device_.get(), 1, &waitFence);

    VkCommandBuffer cmd = commandBuffers_.at(frame);
    vkResetCommandBuffer(cmd, 0);

    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);

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

void VulkanContext::endFrame(const FrameContext& frame) {
    vkCmdEndRendering(frame.cmd);

    transitionImage(frame.cmd,
                    swapchainImages_.at(frame.imageIndex),
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    vkEndCommandBuffer(frame.cmd);

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
    vkQueueSubmit2(graphicsQueue_, 1, &submit, inFlight_.at(frame.frameIndex).get());

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
    const VkResult result = vkQueuePresentKHR(graphicsQueue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        swapchainDirty_ = true;
    }

    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
}

void VulkanContext::waitIdle() const {
    if (device_) vkDeviceWaitIdle(device_.get());
}

// --- resources -------------------------------------------------------------

Buffer VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Memory memory) {
    Buffer buffer;
    buffer.size = size;

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

    VmaAllocationInfo info{};
    if (vmaCreateBuffer(
            allocator_.get(), &bufferInfo, &allocInfo, &buffer.handle, &buffer.allocation, &info) !=
        VK_SUCCESS) {
        return {};
    }
    buffer.mapped = info.pMappedData;
    return buffer;
}

void VulkanContext::destroyBuffer(Buffer& buffer) noexcept {
    if (buffer.handle != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_.get(), buffer.handle, buffer.allocation);
    }
    buffer = {};
}

std::expected<void, InitError>
VulkanContext::uploadBuffer(Buffer& dst, const void* data, VkDeviceSize size) {
    if (size == 0) return {};
    if (size > dst.size) return fail("Upload larger than destination buffer");

    // A host-visible destination can be written directly; the staging round
    // trip is only needed for device-local memory.
    if (dst.mapped != nullptr) {
        std::memcpy(dst.mapped, data, size);
        return {};
    }

    Buffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Memory::HostVisible);
    if (staging.handle == VK_NULL_HANDLE || staging.mapped == nullptr) {
        destroyBuffer(staging);
        return fail("Staging buffer allocation failed");
    }
    std::memcpy(staging.mapped, data, size);

    VkFence fence = uploadFence_.get();
    vkResetFences(device_.get(), 1, &fence);
    vkResetCommandBuffer(uploadCmd_, 0);

    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(uploadCmd_, &begin);
    const VkBufferCopy copy{.srcOffset = 0, .dstOffset = 0, .size = size};
    vkCmdCopyBuffer(uploadCmd_, staging.handle, dst.handle, 1, &copy);
    vkEndCommandBuffer(uploadCmd_);

    const VkCommandBufferSubmitInfo cmdInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = uploadCmd_,
    };
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
    };
    vkQueueSubmit2(graphicsQueue_, 1, &submit, fence);
    vkWaitForFences(device_.get(), 1, &fence, VK_TRUE, UINT64_MAX);

    destroyBuffer(staging);
    return {};
}

std::expected<VkShaderModule, InitError>
VulkanContext::loadShaderModule(const std::filesystem::path& path) const {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return fail("Cannot open shader: " + path.string());

    const std::streamsize size = file.tellg();
    if (size <= 0 || size % 4 != 0) {
        return fail("Shader is not valid SPIR-V (bad size): " + path.string());
    }

    std::vector<uint32_t> code(static_cast<size_t>(size) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), size);

    const VkShaderModuleCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = static_cast<size_t>(size),
        .pCode = code.data(),
    };
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_.get(), &info, nullptr, &module) != VK_SUCCESS) {
        return fail("vkCreateShaderModule failed for " + path.string());
    }
    return module;
}

} // namespace orb::gfx
