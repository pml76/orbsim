#include "render/VulkanContext.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>

#include <cstring>
#include <fstream>

namespace orb::gfx {
namespace {

// Reverse-Z: the depth buffer is cleared to 0.0 and the pipelines compare with
// GREATER. Spreading float precision evenly across a range that runs from a
// cockpit panel a metre away to a planet a hundred million kilometres out is
// only possible this way; a conventional 0..1 depth buffer z-fights badly long
// before it reaches those distances.
constexpr float kDepthClear = 0.0f;

VkSemaphore makeSemaphore(VkDevice device) {
    const VkSemaphoreCreateInfo ci{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore s = VK_NULL_HANDLE;
    vkCreateSemaphore(device, &ci, nullptr, &s);
    return s;
}

VkFence makeFence(VkDevice device, bool signalled) {
    const VkFenceCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = signalled ? VK_FENCE_CREATE_SIGNALED_BIT : VkFenceCreateFlags{0},
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
    VkImageMemoryBarrier2 barrier{
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

bool VulkanContext::init(SDL_Window* window, bool enableValidation, std::string& error) {
    window_ = window;

    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
    if (sdlExts == nullptr) {
        error = std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError();
        return false;
    }

    auto buildInstance = [&](bool validation) {
        vkb::InstanceBuilder builder;
        builder.set_app_name("orbsim").set_engine_name("orbsim").require_api_version(1, 3, 0);
        for (uint32_t i = 0; i < sdlExtCount; ++i)
            builder.enable_extension(sdlExts[i]);
        if (validation) builder.request_validation_layers().use_default_debug_messenger();
        return builder.build();
    };

    // Validation layers are part of the SDK, not the driver, so a machine with
    // only a runtime will not have them. Fall back rather than refusing to run.
    auto instRet = buildInstance(enableValidation);
    if (!instRet && enableValidation) {
        SDL_Log("Validation layers unavailable (%s); continuing without them.",
                instRet.error().message().c_str());
        instRet = buildInstance(false);
    }
    if (!instRet) {
        error = "Vulkan instance creation failed: " + instRet.error().message();
        return false;
    }

    const vkb::Instance vkbInstance = instRet.value();
    instance_ = vkbInstance.instance;
    debugMessenger_ = vkbInstance.debug_messenger;

    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        error = std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError();
        return false;
    }

    // Dynamic rendering and synchronization2 are the two 1.3 features this
    // renderer is built around; there is no fallback path for them.
    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .bufferDeviceAddress = VK_TRUE,
    };
    VkPhysicalDeviceFeatures features10{
        .fillModeNonSolid = VK_TRUE, // wireframe, for debugging meshes
        .wideLines = VK_FALSE,       // widely unsupported; lines stay 1px
    };

    vkb::PhysicalDeviceSelector selector{vkbInstance};
    auto physRet = selector.set_surface(surface_)
                       .set_minimum_version(1, 3)
                       .set_required_features(features10)
                       .set_required_features_12(features12)
                       .set_required_features_13(features13)
                       .select();
    if (!physRet) {
        error = "No suitable Vulkan 1.3 device: " + physRet.error().message();
        return false;
    }

    vkb::DeviceBuilder deviceBuilder{physRet.value()};
    auto deviceRet = deviceBuilder.build();
    if (!deviceRet) {
        error = "Vulkan device creation failed: " + deviceRet.error().message();
        return false;
    }

    const vkb::Device vkbDevice = deviceRet.value();
    device_ = vkbDevice.device;
    physicalDevice_ = physRet.value().physical_device;
    deviceName_ = physRet.value().name;

    auto queueRet = vkbDevice.get_queue(vkb::QueueType::graphics);
    auto familyRet = vkbDevice.get_queue_index(vkb::QueueType::graphics);
    if (!queueRet || !familyRet) {
        error = "No graphics queue available";
        return false;
    }
    graphicsQueue_ = queueRet.value();
    graphicsQueueFamily_ = familyRet.value();

    VmaVulkanFunctions vulkanFunctions{
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

    // Per-frame command pools and sync objects.
    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsQueueFamily_,
    };
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPools_[i]) != VK_SUCCESS) {
            error = "vkCreateCommandPool failed";
            return false;
        }
        const VkCommandBufferAllocateInfo cbInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPools_[i],
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (vkAllocateCommandBuffers(device_, &cbInfo, &commandBuffers_[i]) != VK_SUCCESS) {
            error = "vkAllocateCommandBuffers failed";
            return false;
        }
        imageAvailable_[i] = makeSemaphore(device_);
        inFlight_[i] = makeFence(device_, /*signalled=*/true);
    }

    // Immediate-submit context, used only for load-time uploads.
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
    vkAllocateCommandBuffers(device_, &uploadCbInfo, &uploadCmd_);
    uploadFence_ = makeFence(device_, /*signalled=*/false);

    return createSwapchain(error);
}

bool VulkanContext::createSwapchain(std::string& error) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w <= 0 || h <= 0) {
        error = "Window has zero size";
        return false;
    }

    vkb::SwapchainBuilder builder{
        physicalDevice_, device_, surface_, graphicsQueueFamily_, graphicsQueueFamily_};

    // UNORM rather than SRGB: the shaders write display-ready colours directly,
    // so an automatic linear-to-sRGB conversion would wash them out.
    auto swapRet = builder
                       .set_desired_format(VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM,
                                                              VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
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

    // Depth attachment, sized to match.
    const VkImageCreateInfo depthInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = kDepthFormat,
        .extent = {swapchainExtent_.width, swapchainExtent_.height, 1},
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
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
    };
    if (vkCreateImageView(device_, &depthViewInfo, nullptr, &depthView_) != VK_SUCCESS) {
        error = "Depth image view creation failed";
        return false;
    }

    swapchainDirty_ = false;
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
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w <= 0 || h <= 0) return std::nullopt;

    if (swapchainDirty_ && !recreateSwapchain()) return std::nullopt;

    const uint32_t frame = frameIndex_;
    vkWaitForFences(device_, 1, &inFlight_[frame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    const VkResult acquire = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, imageAvailable_[frame], VK_NULL_HANDLE, &imageIndex);

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
    vkResetFences(device_, 1, &inFlight_[frame]);

    VkCommandBuffer cmd = commandBuffers_[frame];
    vkResetCommandBuffer(cmd, 0);

    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);

    transitionImage(cmd,
                    swapchainImages_[imageIndex],
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    transitionImage(cmd,
                    depthImage_,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT);

    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainViews_[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        // Deep space is not pure black: a very dark blue reads better than
        // #000000 once stars and a lit limb are in frame.
        .clearValue = {.color = {{0.004f, 0.006f, 0.012f, 1.0f}}},
    };
    const VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthView_,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {kDepthClear, 0}},
    };

    const VkRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, swapchainExtent_},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };
    vkCmdBeginRendering(cmd, &rendering);

    // Viewport is flipped vertically so that +Y is up in clip space, matching
    // the maths convention used throughout the sim rather than Vulkan's.
    const VkViewport viewport{
        .x = 0.0f,
        .y = static_cast<float>(swapchainExtent_.height),
        .width = static_cast<float>(swapchainExtent_.width),
        .height = -static_cast<float>(swapchainExtent_.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const VkRect2D scissor{{0, 0}, swapchainExtent_};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    return FrameContext{cmd, imageIndex, frame, swapchainExtent_};
}

void VulkanContext::endFrame(const FrameContext& frame) {
    vkCmdEndRendering(frame.cmd);

    transitionImage(frame.cmd,
                    swapchainImages_[frame.imageIndex],
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    vkEndCommandBuffer(frame.cmd);

    const VkSemaphoreSubmitInfo waitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = imageAvailable_[frame.frameIndex],
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkSemaphoreSubmitInfo signalInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = renderFinished_[frame.imageIndex],
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
    vkQueueSubmit2(graphicsQueue_, 1, &submit, inFlight_[frame.frameIndex]);

    const VkPresentInfoKHR present{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinished_[frame.imageIndex],
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

Buffer VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    Buffer buffer;
    buffer.size = size;

    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_AUTO};
    if (hostVisible) {
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

    Buffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, /*hostVisible=*/true);
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

VkShaderModule VulkanContext::loadShaderModule(const std::string& path, std::string& error) const {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "Cannot open shader: " + path;
        return VK_NULL_HANDLE;
    }

    const std::streamsize size = file.tellg();
    if (size <= 0 || size % 4 != 0) {
        error = "Shader is not valid SPIR-V (bad size): " + path;
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
        error = "vkCreateShaderModule failed for " + path;
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
        if (imageAvailable_[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
        if (inFlight_[i] != VK_NULL_HANDLE) vkDestroyFence(device_, inFlight_[i], nullptr);
        if (commandPools_[i] != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_, commandPools_[i], nullptr);
        imageAvailable_[i] = VK_NULL_HANDLE;
        inFlight_[i] = VK_NULL_HANDLE;
        commandPools_[i] = VK_NULL_HANDLE;
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
