#ifndef ORBSIM_RENDER_VULKANCONTEXT_HPP
#define ORBSIM_RENDER_VULKANCONTEXT_HPP
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
// Every Vulkan call that returns a VkResult is checked, and every function
// here that can fail says so in its return type. A dropped VkResult is how a
// lost device turns into a hang three frames later.
//
#include "render/VulkanHandle.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
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

// Not booleans. `create(window, true)` and `createBuffer(size, usage, true)`
// were mysteries at the call site, patched with /*name=*/ comments that the
// compiler could not check -- and that comment was the evidence the type was
// wrong. See CODING_GUIDELINES.md section 2.
enum class Validation {
    Disabled,
    Enabled,
};

enum class Memory {
    DeviceLocal, // fastest for the GPU; needs a staging copy to write
    HostVisible, // mappable, so the CPU can write it directly
};

// Why a string here, when the orbital core reports an enum: renderer failures
// are reported *by the driver*, and the useful part is its text. "No suitable
// GPU" tells a user less than the driver's own account of which feature was
// missing. Section 7 asks for one strategy per layer -- one way of signalling
// failure -- not one representation of it everywhere.
struct RenderError {
    std::string message;
};

// Everything a frame needs to record its commands. Handed out by beginFrame.
struct FrameContext {
    VkCommandBuffer cmd{VK_NULL_HANDLE};
    uint32_t imageIndex{0};
    uint32_t frameIndex{0}; // which of the kFramesInFlight slots
    VkExtent2D extent{};
};

// Rule of Zero. Every handle below owns itself (render/VulkanHandle.hpp), so
// this class declares no destructor, no copy and no move: the compiler writes
// them, destruction happens in reverse declaration order because the language
// says so, and there is no shutdown() to keep in step with the member list.
class VulkanContext {
public:
    // A factory, not a constructor followed by init(). Either you hold a fully
    // valid context or you hold an error; there is no half-built third state to
    // write defensive code against, and therefore none to forget to write
    // defensive code against (E.5, NR.5). A failure part-way through unwinds on
    // its own, because every handle built so far destroys itself.
    //
    // `validationErrors` is incremented for every error the validation layers
    // report, from whichever thread the driver reports it on. It is the
    // caller's and not a member because teardown is where validation errors
    // hide, and teardown is after this object is gone: the caller reads the
    // count once the context has been destroyed. Untouched when validation is
    // disabled or unavailable.
    [[nodiscard]] static std::expected<VulkanContext, RenderError>
    create(SDL_Window* window, Validation validation, std::atomic<uint32_t>& validationErrors);

    // Acquires a swapchain image and opens a command buffer with the colour and
    // depth attachments already bound and cleared.
    //
    // Two layers of outcome, deliberately. The outer expected is failure: the
    // device was lost, a fence could not be waited on, the swapchain could not
    // be rebuilt. The inner optional is "no frame this time": the window is
    // minimised, or the swapchain was out of date and has just been rebuilt.
    // The second is routine and the caller should idle; the first is not.
    [[nodiscard]] std::expected<std::optional<FrameContext>, RenderError> beginFrame();

    // Closes the render pass, transitions for presentation, submits, presents.
    [[nodiscard]] std::expected<void, RenderError> endFrame(const FrameContext& frame);

    [[nodiscard]] std::expected<void, RenderError> waitIdle() const;

    // Marks the swapchain for rebuild at the next beginFrame. Called on resize.
    void requestSwapchainRebuild() noexcept { swapchainDirty_ = true; }

    // --- resources ---------------------------------------------------------

    [[nodiscard]] std::expected<UniqueBuffer, RenderError>
    createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Memory memory);

    // Uploads through a host-visible staging buffer, or directly when the
    // destination is host-visible itself. Synchronous: intended for load-time
    // data, not per-frame streaming.
    [[nodiscard]] std::expected<void, RenderError> uploadBuffer(UniqueBuffer& dst,
                                                                std::span<const std::byte> data);

    // Loads a SPIR-V module from disk.
    //
    // std::filesystem::path, not std::string: on Windows the native encoding is
    // wchar_t, and a narrow string works right up until a user's account name
    // steps outside it -- then it fails in a way nobody can diagnose from a bug
    // report.
    [[nodiscard]] std::expected<UniqueShaderModule, RenderError>
    loadShaderModule(const std::filesystem::path& path) const;

    // --- accessors ---------------------------------------------------------

    [[nodiscard]] VkDevice device() const noexcept { return device_.get(); }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
    [[nodiscard]] VmaAllocator allocator() const noexcept { return allocator_.get(); }
    [[nodiscard]] VkFormat colorFormat() const noexcept { return swapchainFormat_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return swapchainExtent_; }
    [[nodiscard]] const std::string& deviceName() const noexcept { return deviceName_; }

private:
    VulkanContext() = default;

    [[nodiscard]] std::expected<void, RenderError> createAllocator();
    [[nodiscard]] std::expected<void, RenderError> createFrameResources();
    [[nodiscard]] std::expected<void, RenderError> createUploadContext();
    [[nodiscard]] std::expected<void, RenderError> createSwapchain();
    [[nodiscard]] std::expected<void, RenderError> createDepthAttachment();
    [[nodiscard]] std::expected<void, RenderError> recreateSwapchain();
    void beginRendering(VkCommandBuffer cmd, uint32_t imageIndex) const;

    // Non-owning: SDL owns the window, and it outlives this object.
    SDL_Window* window_{nullptr};

    // DECLARATION ORDER IS DESTRUCTION ORDER, REVERSED. The instance must
    // outlive the surface and the messenger; the device must outlive everything
    // allocated from it. Do not reorder these without understanding that.
    UniqueInstance instance_;
    UniqueDebugMessenger debugMessenger_;
    UniqueSurface surface_;
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE}; // owned by the instance
    UniqueDevice device_;
    VkQueue graphicsQueue_{VK_NULL_HANDLE}; // owned by the device
    uint32_t graphicsQueueFamily_{0};
    std::string deviceName_;
    UniqueAllocator allocator_;

    UniqueSwapchain swapchain_;
    VkFormat swapchainFormat_{VK_FORMAT_UNDEFINED};
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_; // owned by the swapchain
    std::vector<UniqueImageView> swapchainViews_;
    bool swapchainDirty_{false};

    UniqueImage depthImage_;
    UniqueImageView depthView_;

    // Per frame in flight. std::array, not a C array: it knows its own size,
    // and .at() bounds-checks the handful of runtime-indexed accesses in the
    // .cpp, none of which are in a hot path.
    std::array<UniqueCommandPool, kFramesInFlight> commandPools_;
    std::array<VkCommandBuffer, kFramesInFlight> commandBuffers_{}; // freed with the pool
    std::array<UniqueSemaphore, kFramesInFlight> imageAvailable_;
    std::array<UniqueFence, kFramesInFlight> inFlight_;

    // Per swapchain image. A present-wait semaphore must not be reused while a
    // previous present on the same image is still pending, and the swapchain
    // image count is not necessarily kFramesInFlight.
    std::vector<UniqueSemaphore> renderFinished_;

    // Immediate-submit context for uploads.
    UniqueCommandPool uploadPool_;
    VkCommandBuffer uploadCmd_{VK_NULL_HANDLE}; // freed with uploadPool_
    UniqueFence uploadFence_;

    uint32_t frameIndex_{0};

    // LAST, deliberately: destroyed first, so the GPU is idle before any handle
    // above it is destroyed. See DeviceIdleGuard in render/VulkanHandle.hpp.
    DeviceIdleGuard idleGuard_;
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

#endif // ORBSIM_RENDER_VULKANCONTEXT_HPP
