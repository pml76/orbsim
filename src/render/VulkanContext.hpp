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
#include "view/RenderQuality.hpp"

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

// Reverse-Z (ADR 0003), which is three constants and they live together: the
// depth buffer is 32-bit float, cleared to 0.0 -- infinity, under the
// projection in view/Projection.hpp, which maps the near plane to 1.0 -- and
// every pipeline keeps the fragment with the *greater* depth. Spreading float
// precision evenly across a range that runs from a cockpit panel a metre away
// to a planet a hundred million kilometres out is only possible this way; a
// conventional 0-to-1 depth buffer z-fights badly long before it reaches those
// distances.
//
// **The comparison is not a pipeline setting.** render/Pipeline.cpp reads it
// from here and nowhere else, and GraphicsPipelineDesc has no field for it. A
// pipeline created with LESS out of habit draws nothing at all, which is a
// symptom that points nowhere near its cause, so the one way to get it wrong
// is to edit this line.
inline constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
inline constexpr float kDepthClear = 0.0F;
inline constexpr VkCompareOp kDepthCompareOp = VK_COMPARE_OP_GREATER;

// The linear HDR target the scene is drawn into (ADR 0014, M1-14): light, in
// a floating-point format, never display-ready colour. Nothing but the
// resolve pass (render/ResolvePass.hpp) writes the swapchain, and it is the
// one place the sRGB encode happens.
//
// **A named constant rather than something asked of the device**, because the
// Vulkan specification makes this format mandatory for exactly the two uses
// here -- a colour attachment and an image a shader reads -- and Khronos's
// desktop baseline profiles 2022 to 2026 and Android's 2021 baseline all list
// it (checked 2026-09-24 against the SDK's profile files). So there is no
// second format to fall back to that a conformant device could need; create()
// still asks the device and reports a missing feature by name rather than
// assuming, and a scene pipeline reads this constant, so it cannot be built
// for the swapchain's format by mistake -- a mismatch Vulkan only reports
// once something is drawn.
//
// Sixteen bits per channel: 11 significant bits, 0.05 % relative, which is
// the quantisation floor M1-18's 0.5 % radiometry budget is stated against.
inline constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// Not booleans. `create(window, true)` and `createBuffer(size, usage, true)`
// were mysteries at the call site, patched with /*name=*/ comments that the
// compiler could not check -- and that comment was the evidence the type was
// wrong. See CODING_GUIDELINES.md section 2.
enum class Validation : std::uint8_t {
    Disabled,
    Enabled,
};

enum class Memory : std::uint8_t {
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

class ResolvePass; // render/ResolvePass.hpp; endFrame records it

// Everything a frame needs to record its commands. Handed out by beginFrame.
// What is recorded into `cmd` between beginFrame and endFrame draws into the
// HDR target, never into the swapchain.
struct FrameContext {
    VkCommandBuffer cmd{VK_NULL_HANDLE};
    uint32_t imageIndex{0};
    uint32_t frameIndex{0}; // which of the kFramesInFlight slots
    VkExtent2D extent{};

    // The quality settings this frame draws at, snapshotted by value (M1-12,
    // ADR 0007). **Nothing reads it yet**, and nothing will until M1-46 gives
    // the struct its first field; it is here now because threading a settings
    // value through a renderer built for one fixed configuration is a
    // retrofit, and this is the moment when nothing is drawn and it costs
    // nothing.
    //
    // **beginFrame fills it, from its argument** (register decision 150,
    // which settled decision 127's open half). Until M1-13 the caller
    // assigned it after the fact, and a frame path that forgot drew at the
    // default; now a frame cannot begin without somebody saying at what
    // quality, and the compiler is what asks.
    orb::view::RenderQuality quality{};
};

// Rule of Zero. Every handle below owns itself (render/VulkanHandle.hpp), so
// this class declares no destructor, no copy and no move: the compiler writes
// them, destruction happens in reverse declaration order because the language
// says so, and there is no shutdown() to keep in step with the member list.
// What a buffer allocation asks for. A struct rather than three parameters
// because VkDeviceSize and VkBufferUsageFlags are both unsigned integers and
// convert into one another, so `createBuffer(usage, size, ...)` compiled --
// which bugprone-easily-swappable-parameters reports since
// SuppressParametersUsedTogether was switched off on 2026-09-20. A strong
// `Bytes` type would be the other answer.
//
// **It was waiting for Count<Derived>, which arrived with M1-12 on 2026-09-23,
// and the wait is over without the answer being obvious.** `Count` holds a
// `std::uint32_t`, and `VkDeviceSize` is a `uint64_t` -- checked, not assumed.
// A `Bytes` built on `Count` would cap at 4,294,967,295 bytes, which is less
// than the memory on the card this project targets, so it would be a type that
// cannot express a buffer this renderer will legitimately want. The three ways
// out -- widen `Count`, give it a 64-bit sibling, or keep `VkDeviceSize` here
// and accept that the struct's designated initialisers are what makes the call
// site readable -- are a decision rather than a tidy-up, and the owner's.
// Recorded in docs/STATUS.md's open row rather than settled here.
struct BufferRequest {
    VkDeviceSize size{};
    VkBufferUsageFlags usage{};
    Memory memory{Memory::DeviceLocal};
};

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

    // Acquires a swapchain image and opens a command buffer with the HDR
    // target and the depth attachment already bound and cleared.
    //
    // Two layers of outcome, deliberately. The outer expected is failure: the
    // device was lost, a fence could not be waited on, the swapchain could not
    // be rebuilt. The inner optional is "no frame this time": the window is
    // minimised, or the swapchain was out of date and has just been rebuilt.
    // The second is routine and the caller should idle; the first is not.
    //
    // `quality` is taken by value and copied into the frame: a trivially
    // copyable aggregate, so a later adaptive controller can change it between
    // frames without anything holding a reference to what it changed.
    [[nodiscard]] std::expected<std::optional<FrameContext>, RenderError>
    beginFrame(orb::view::RenderQuality quality);

    // Closes the scene's rendering, runs the resolve pass from the HDR target
    // into the swapchain image, transitions it for presentation, submits and
    // presents. The resolve pass is a parameter rather than something a
    // caller may remember to record, so a frame cannot reach the display
    // without its one encode.
    [[nodiscard]] std::expected<void, RenderError> endFrame(const FrameContext& frame,
                                                            const ResolvePass& resolve);

    [[nodiscard]] std::expected<void, RenderError> waitIdle() const;

    // Marks the swapchain for rebuild at the next beginFrame. Called on resize.
    void requestSwapchainRebuild() noexcept { swapchainDirty_ = true; }

    // --- resources ---------------------------------------------------------

    [[nodiscard]] std::expected<UniqueBuffer, RenderError>
    createBuffer(const BufferRequest& request);

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
    // The swapchain's format, which only the resolve pass draws into. Named
    // for what it is since M1-14; it was colorFormat(), and the scene
    // pipelines used it, which stopped being right the day the scene moved
    // to kHdrFormat.
    [[nodiscard]] VkFormat swapchainFormat() const noexcept { return swapchainFormat_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return swapchainExtent_; }
    // A reference into this context, and marked so, which lets clang report
    // one held past the context's lifetime where it can follow the two.
    [[nodiscard]] const std::string& deviceName() const noexcept ORBSIM_LIFETIMEBOUND {
        return deviceName_;
    }

private:
    VulkanContext() = default;

    [[nodiscard]] std::expected<void, RenderError> createAllocator();
    [[nodiscard]] std::expected<void, RenderError> createFrameResources();
    [[nodiscard]] std::expected<void, RenderError> createUploadContext();
    [[nodiscard]] std::expected<void, RenderError> createSwapchain();
    [[nodiscard]] std::expected<void, RenderError> createDepthAttachment();
    [[nodiscard]] std::expected<void, RenderError> createHdrTarget();
    void releaseSizedResources() noexcept;
    [[nodiscard]] std::expected<void, RenderError> recreateSwapchain();
    void beginSceneRendering(VkCommandBuffer cmd) const;
    void recordResolve(const FrameContext& frame, const ResolvePass& resolve) const;

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

    // The linear HDR target: swapchain-sized, recreated with it, and one for
    // all frames in flight, as the depth image is -- the barriers order the
    // frames on the one queue, so a second image would buy only overlap that
    // those barriers already give away.
    UniqueImage hdrImage_;
    UniqueImageView hdrView_;

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

// Where a layout transition starts and where it ends. A struct rather than two
// VkImageLayout parameters because reversed they describe the opposite
// transition, and Vulkan accepts it -- the validation layers complain about the
// image's actual layout three calls later, if at all.
// bugprone-easily-swappable-parameters reports the pair since
// SuppressParametersUsedTogether was switched off on 2026-09-20.
struct LayoutTransition {
    VkImageLayout from{VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageLayout to{VK_IMAGE_LAYOUT_UNDEFINED};
};

// Records a synchronization2 image layout transition with conservative stage
// and access masks. Fine for the handful of transitions this renderer makes.
void transitionImage(VkCommandBuffer cmd,
                     VkImage image,
                     const LayoutTransition& layouts,
                     VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

} // namespace orb::gfx

#endif // ORBSIM_RENDER_VULKANCONTEXT_HPP
