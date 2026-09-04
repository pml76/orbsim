#pragma once
//
// Vulkan device, swapchain and frame pacing.
//
// This is the "graphics client" layer: everything Vulkan-specific lives behind
// it, and the simulation never sees a Vulkan type. Orbiter drew the same line,
// which is what let it swap renderers without touching the physics; keeping it
// from the start is cheaper than retrofitting it later.
//
// Targets Vulkan 1.3 core, so dynamic rendering and synchronization2 are used
// directly. There are no render pass or framebuffer objects anywhere.
//
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct SDL_Window;

namespace orb::gfx {

// Two frames in flight: enough to keep the GPU fed without adding a frame of
// input latency, which matters for flying a spacecraft by hand.
inline constexpr uint32_t kFramesInFlight = 2;

inline constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

// A GPU buffer together with the allocation that backs it.
struct Buffer {
    VkBuffer handle{VK_NULL_HANDLE};
    VmaAllocation allocation{nullptr};
    void* mapped{nullptr}; // non-null for host-visible buffers
    VkDeviceSize size{0};
};

// Everything a frame needs to record its commands. Handed out by beginFrame.
struct FrameContext {
    VkCommandBuffer cmd{VK_NULL_HANDLE};
    uint32_t imageIndex{0};
    uint32_t frameIndex{0}; // which of the kFramesInFlight slots
    VkExtent2D extent{};
};

class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // Returns false and fills `error` on failure; the caller reports it. No
    // exceptions, because a missing GPU feature is a normal outcome to explain
    // to the user, not an exceptional one.
    bool init(SDL_Window* window, bool enableValidation, std::string& error);
    void shutdown();

    // Acquires a swapchain image and opens a command buffer with the colour and
    // depth attachments already bound and cleared. Returns nullopt when the
    // swapchain was out of date and got rebuilt, in which case the caller
    // should simply skip the frame.
    std::optional<FrameContext> beginFrame();

    // Closes the render pass, transitions for presentation, submits, presents.
    void endFrame(const FrameContext& frame);

    void waitIdle() const;

    // Marks the swapchain for rebuild at the next beginFrame. Called on resize.
    void requestSwapchainRebuild() { swapchainDirty_ = true; }

    // --- resources ---------------------------------------------------------

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void destroyBuffer(Buffer& buffer) noexcept;

    // Uploads through a host-visible staging buffer. Synchronous: intended for
    // load-time data, not per-frame streaming.
    bool uploadBuffer(Buffer& dst, const void* data, VkDeviceSize size, std::string& error);

    // Loads a SPIR-V module from disk. Returns VK_NULL_HANDLE on failure.
    VkShaderModule loadShaderModule(const std::string& path, std::string& error) const;

    // --- accessors ---------------------------------------------------------

    VkDevice device() const { return device_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VmaAllocator allocator() const { return allocator_; }
    VkFormat colorFormat() const { return swapchainFormat_; }
    VkExtent2D extent() const { return swapchainExtent_; }
    const std::string& deviceName() const { return deviceName_; }

private:
    bool createSwapchain(std::string& error);
    void destroySwapchain() noexcept;
    bool recreateSwapchain();

    SDL_Window* window_{nullptr};

    VkInstance instance_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue graphicsQueue_{VK_NULL_HANDLE};
    uint32_t graphicsQueueFamily_{0};
    VmaAllocator allocator_{nullptr};
    std::string deviceName_;

    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkFormat swapchainFormat_{VK_FORMAT_UNDEFINED};
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainViews_;
    bool swapchainDirty_{false};

    VkImage depthImage_{VK_NULL_HANDLE};
    VmaAllocation depthAllocation_{nullptr};
    VkImageView depthView_{VK_NULL_HANDLE};

    // Per frame in flight.
    VkCommandPool commandPools_[kFramesInFlight]{};
    VkCommandBuffer commandBuffers_[kFramesInFlight]{};
    VkSemaphore imageAvailable_[kFramesInFlight]{};
    VkFence inFlight_[kFramesInFlight]{};

    // Per swapchain image. A present-wait semaphore must not be reused while a
    // previous present on the same image is still pending, and the swapchain
    // image count is not necessarily kFramesInFlight.
    std::vector<VkSemaphore> renderFinished_;

    // Immediate-submit context for uploads.
    VkCommandPool uploadPool_{VK_NULL_HANDLE};
    VkCommandBuffer uploadCmd_{VK_NULL_HANDLE};
    VkFence uploadFence_{VK_NULL_HANDLE};

    uint32_t frameIndex_{0};
};

// --- small helpers shared by the render code -------------------------------

// Records a synchronization2 image layout transition with conservative stage
// and access masks. Fine for the handful of transitions this renderer makes.
void transitionImage(VkCommandBuffer cmd,
                     VkImage image,
                     VkImageLayout from,
                     VkImageLayout to,
                     VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

} // namespace orb::gfx
