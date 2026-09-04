#include "render/VulkanContext.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>

#include <cstring>
#include <expected>
#include <fstream>

namespace orb::gfx {
namespace {

// Reverse-Z: the depth buffer is cleared to 0.0 and the pipelines compare with
// GREATER. Spreading float precision evenly across a range that runs from a
// cockpit panel a metre away to a planet a hundred million kilometres out is
// only possible this way; a conventional 0..1 depth buffer z-fights badly long
// before it reaches those distances.
constexpr float kDepthClear = 0.0F;

VkSemaphore makeSemaphore(VkDevice device) {
    const VkSemaphoreCreateInfo ci{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore s = VK_NULL_HANDLE;
    vkCreateSemaphore(device, &ci, nullptr, &s);
    return s;
}

enum class FenceState {
    Unsignalled,
    Signalled,
};

VkFence makeFence(VkDevice device, FenceState state) {
    const VkFenceCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags =
            state == FenceState::Signalled ? VK_FENCE_CREATE_SIGNALED_BIT : VkFenceCreateFlags{0},
    };
    VkFence f = VK_NULL_HANDLE;
    vkCreateFence(device, &ci, nullptr, &f);
    return f;
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

VulkanContext::~VulkanContext() { shutdown(); }

namespace {

// The instance-creation and device-selection steps both need the vkb::Instance,
// but that type must not appear in the header -- a renderer header that drags
// vk-bootstrap in makes every translation unit pay for it. Keeping these as
// file-local functions that hand back a small bundle solves both problems: the
// header stays clean, and init() becomes a sequence of named steps instead of
// 149 lines doing eight jobs.

struct InstanceBundle {
    vkb::Instance instance;
    VkSurfaceKHR surface{VK_NULL_HANDLE};
};

[[nodiscard]] std::expected<InstanceBundle, std::string>
makeInstanceAndSurface(SDL_Window* window, Validation validation) {
    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
    if (sdlExts == nullptr) {
        return std::unexpected(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") +
                               SDL_GetError());
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
    if (!built) {
        return std::unexpected("Vulkan instance creation failed: " + built.error().message());
    }

    InstanceBundle bundle{.instance = built.value(), .surface = VK_NULL_HANDLE};
    if (!SDL_Vulkan_CreateSurface(window, bundle.instance.instance, nullptr, &bundle.surface)) {
        return std::unexpected(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
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

[[nodiscard]] std::expected<DeviceBundle, std::string> makeDevice(const vkb::Instance& instance,
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
    if (!physical) {
        return std::unexpected("No suitable Vulkan 1.3 device: " + physical.error().message());
    }

    vkb::DeviceBuilder const deviceBuilder{physical.value()};
    auto device = deviceBuilder.build();
    if (!device) {
        return std::unexpected("Vulkan device creation failed: " + device.error().message());
    }

    auto queue = device.value().get_queue(vkb::QueueType::graphics);
    auto family = device.value().get_queue_index(vkb::QueueType::graphics);
    if (!queue || !family) return std::unexpected(std::string("No graphics queue available"));

    return DeviceBundle{
        .device = device.value(),
        .physicalDevice = physical.value().physical_device,
        .graphicsQueue = queue.value(),
        .graphicsQueueFamily = family.value(),
        .name = physical.value().name,
    };
}

} // namespace

bool VulkanContext::init(SDL_Window* window, Validation validation, std::string& error) {
    window_ = window;

    auto instance = makeInstanceAndSurface(window, validation);
    if (!instance) {
        error = instance.error();
        return false;
    }
    instance_ = instance->instance.instance;
    debugMessenger_ = instance->instance.debug_messenger;
    surface_ = instance->surface;

    auto device = makeDevice(instance->instance, surface_);
    if (!device) {
        error = device.error();
        return false;
    }
    device_ = device->device.device;
    physicalDevice_ = device->physicalDevice;
    graphicsQueue_ = device->graphicsQueue;
    graphicsQueueFamily_ = device->graphicsQueueFamily;
    deviceName_ = std::move(device->name);

    if (!createAllocator(error)) return false;
    if (!createFrameResources(error)) return false;
    if (!createUploadContext(error)) return false;

    return createSwapchain(error);
}

bool VulkanContext::createAllocator(std::string& error) {
    VmaVulkanFunctions const vulkanFunctions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };
    const VmaAllocatorCreateInfo allocatorInfo{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = physicalDevice_,
        .device = device_,
        .pVulkanFunctions = &vulkanFunctions,
        .instance = instance_,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    if (vmaCreateAllocator(&allocatorInfo, &allocator_) != VK_SUCCESS) {
        error = "vmaCreateAllocator failed";
        return false;
    }
    return true;
}

bool VulkanContext::createFrameResources(std::string& error) {
    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsQueueFamily_,
    };

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPools_.at(i)) != VK_SUCCESS) {
            error = "vkCreateCommandPool failed";
            return false;
        }
        const VkCommandBufferAllocateInfo cbInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPools_.at(i),
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (vkAllocateCommandBuffers(device_, &cbInfo, &commandBuffers_.at(i)) != VK_SUCCESS) {
            error = "vkAllocateCommandBuffers failed";
            return false;
        }
        imageAvailable_.at(i) = makeSemaphore(device_);
        inFlight_.at(i) = makeFence(device_, FenceState::Signalled);
    }
    return true;
}

bool VulkanContext::createUploadContext(std::string& error) {
    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsQueueFamily_,
    };
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &uploadPool_) != VK_SUCCESS) {
        error = "vkCreateCommandPool (upload) failed";
        return false;
    }

    const VkCommandBufferAllocateInfo uploadCbInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = uploadPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(device_, &uploadCbInfo, &uploadCmd_) != VK_SUCCESS) {
        error = "vkAllocateCommandBuffers (upload) failed";
        return false;
    }
    uploadFence_ = makeFence(device_, FenceState::Unsignalled);
    return true;
}

bool VulkanContext::createSwapchain(std::string& error) {
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w <= 0 || h <= 0) {
        error = "Window has zero size";
        return false;
    }

    vkb::SwapchainBuilder builder{
        physicalDevice_, device_, surface_, graphicsQueueFamily_, graphicsQueueFamily_};

    // UNORM rather than SRGB: the shaders write display-ready colours directly,
    // so an automatic linear-to-sRGB conversion would wash them out.
    auto swapRet =
        builder
            .set_desired_format(VkSurfaceFormatKHR{.format = VK_FORMAT_B8G8R8A8_UNORM,
                                                   .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            // Mailbox keeps latency low without tearing. FIFO is the required
            // fallback and is always present.
            .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
            .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(static_cast<uint32_t>(w), static_cast<uint32_t>(h))
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build();
    if (!swapRet) {
        error = "Swapchain creation failed: " + swapRet.error().message();
        return false;
    }

    vkb::Swapchain vkbSwapchain = swapRet.value();
    swapchain_ = vkbSwapchain.swapchain;
    swapchainFormat_ = vkbSwapchain.image_format;
    swapchainExtent_ = vkbSwapchain.extent;
    swapchainImages_ = vkbSwapchain.get_images().value();
    swapchainViews_ = vkbSwapchain.get_image_views().value();

    renderFinished_.resize(swapchainImages_.size());
    for (auto& sem : renderFinished_)
        sem = makeSemaphore(device_);

    if (!createDepthAttachment(error)) return false;

    swapchainDirty_ = false;
    return true;
}

// Split out of createSwapchain: the swapchain and its depth buffer are two
// resources with two failure modes, and one function doing both could only
// report them with the same undifferentiated `return false`.
bool VulkanContext::createDepthAttachment(std::string& error) {
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
    if (vmaCreateImage(
            allocator_, &depthInfo, &depthAlloc, &depthImage_, &depthAllocation_, nullptr) !=
        VK_SUCCESS) {
        error = "Depth image allocation failed";
        return false;
    }

    const VkImageViewCreateInfo depthViewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depthImage_,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = kDepthFormat,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
    };
    if (vkCreateImageView(device_, &depthViewInfo, nullptr, &depthView_) != VK_SUCCESS) {
        error = "Depth image view creation failed";
        return false;
    }
    return true;
}

void VulkanContext::destroySwapchain() noexcept {
    if (device_ == VK_NULL_HANDLE) return;

    if (depthView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, depthView_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, depthImage_, depthAllocation_);
        depthImage_ = VK_NULL_HANDLE;
        depthAllocation_ = nullptr;
    }
    for (VkSemaphore sem : renderFinished_)
        vkDestroySemaphore(device_, sem, nullptr);
    renderFinished_.clear();

    for (VkImageView view : swapchainViews_)
        vkDestroyImageView(device_, view, nullptr);
    swapchainViews_.clear();
    swapchainImages_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

bool VulkanContext::recreateSwapchain() {
    vkDeviceWaitIdle(device_);
    destroySwapchain();

    std::string error;
    if (!createSwapchain(error)) {
        SDL_Log("Swapchain rebuild failed: %s", error.c_str());
        return false;
    }
    return true;
}

std::optional<FrameContext> VulkanContext::beginFrame() {
    // A minimised window has a zero-size swapchain, which cannot be created.
    // Report no frame and let the caller idle.
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w <= 0 || h <= 0) return std::nullopt;

    if (swapchainDirty_ && !recreateSwapchain()) return std::nullopt;

    const uint32_t frame = frameIndex_;
    vkWaitForFences(device_, 1, &inFlight_.at(frame), VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    const VkResult acquire = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, imageAvailable_.at(frame), VK_NULL_HANDLE, &imageIndex);

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
    vkResetFences(device_, 1, &inFlight_.at(frame));

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
                    depthImage_,
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
        .imageView = swapchainViews_.at(imageIndex),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        // Deep space is not pure black: a very dark blue reads better than
        // #000000 once stars and a lit limb are in frame.
        .clearValue = {.color = {{0.004F, 0.006F, 0.012F, 1.0F}}},
    };
    const VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthView_,
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
        .semaphore = imageAvailable_.at(frame.frameIndex),
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkSemaphoreSubmitInfo signalInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = renderFinished_.at(frame.imageIndex),
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
    vkQueueSubmit2(graphicsQueue_, 1, &submit, inFlight_.at(frame.frameIndex));

    // A reference, so the const binds to the handle rather than to the
    // pointer typedef -- `const VkSemaphore` would mean a const pointer, which
    // is a different and more confusing thing.
    const VkSemaphore& presentWait = renderFinished_.at(frame.imageIndex);
    const VkPresentInfoKHR present{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &presentWait,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &frame.imageIndex,
    };
    const VkResult result = vkQueuePresentKHR(graphicsQueue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        swapchainDirty_ = true;
    }

    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
}

void VulkanContext::waitIdle() const {
    if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
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
            allocator_, &bufferInfo, &allocInfo, &buffer.handle, &buffer.allocation, &info) !=
        VK_SUCCESS) {
        return {};
    }
    buffer.mapped = info.pMappedData;
    return buffer;
}

void VulkanContext::destroyBuffer(Buffer& buffer) noexcept {
    if (buffer.handle != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, buffer.handle, buffer.allocation);
    }
    buffer = {};
}

bool VulkanContext::uploadBuffer(Buffer& dst,
                                 const void* data,
                                 VkDeviceSize size,
                                 std::string& error) {
    if (size == 0) return true;
    if (size > dst.size) {
        error = "Upload larger than destination buffer";
        return false;
    }

    // A host-visible destination can be written directly; the staging round
    // trip is only needed for device-local memory.
    if (dst.mapped != nullptr) {
        std::memcpy(dst.mapped, data, size);
        return true;
    }

    Buffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Memory::HostVisible);
    if (staging.handle == VK_NULL_HANDLE || staging.mapped == nullptr) {
        error = "Staging buffer allocation failed";
        destroyBuffer(staging);
        return false;
    }
    std::memcpy(staging.mapped, data, size);

    vkResetFences(device_, 1, &uploadFence_);
    vkResetCommandBuffer(uploadCmd_, 0);

    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(uploadCmd_, &begin);
    const VkBufferCopy copy{.size = size};
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
    vkQueueSubmit2(graphicsQueue_, 1, &submit, uploadFence_);
    vkWaitForFences(device_, 1, &uploadFence_, VK_TRUE, UINT64_MAX);

    destroyBuffer(staging);
    return true;
}

VkShaderModule VulkanContext::loadShaderModule(const std::filesystem::path& path,
                                               std::string& error) const {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "Cannot open shader: " + path.string();
        return VK_NULL_HANDLE;
    }

    const std::streamsize size = file.tellg();
    if (size <= 0 || size % 4 != 0) {
        error = "Shader is not valid SPIR-V (bad size): " + path.string();
        return VK_NULL_HANDLE;
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
    if (vkCreateShaderModule(device_, &info, nullptr, &module) != VK_SUCCESS) {
        error = "vkCreateShaderModule failed for " + path.string();
        return VK_NULL_HANDLE;
    }
    return module;
}

// ---------------------------------------------------------------------------

void VulkanContext::shutdown() {
    if (device_ == VK_NULL_HANDLE) {
        // Nothing was created, or shutdown already ran.
        if (instance_ != VK_NULL_HANDLE) {
            if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
            if (debugMessenger_ != VK_NULL_HANDLE) {
                vkb::destroy_debug_utils_messenger(instance_, debugMessenger_);
            }
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
            surface_ = VK_NULL_HANDLE;
            debugMessenger_ = VK_NULL_HANDLE;
        }
        return;
    }

    vkDeviceWaitIdle(device_);
    destroySwapchain();

    if (uploadFence_ != VK_NULL_HANDLE) vkDestroyFence(device_, uploadFence_, nullptr);
    if (uploadPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, uploadPool_, nullptr);

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (imageAvailable_.at(i) != VK_NULL_HANDLE)
            vkDestroySemaphore(device_, imageAvailable_.at(i), nullptr);
        if (inFlight_.at(i) != VK_NULL_HANDLE) vkDestroyFence(device_, inFlight_.at(i), nullptr);
        if (commandPools_.at(i) != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_, commandPools_.at(i), nullptr);
        imageAvailable_.at(i) = VK_NULL_HANDLE;
        inFlight_.at(i) = VK_NULL_HANDLE;
        commandPools_.at(i) = VK_NULL_HANDLE;
    }

    if (allocator_ != nullptr) {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }

    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;

    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (debugMessenger_ != VK_NULL_HANDLE) {
        vkb::destroy_debug_utils_messenger(instance_, debugMessenger_);
        debugMessenger_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

} // namespace orb::gfx
