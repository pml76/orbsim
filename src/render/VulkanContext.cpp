#include "render/VulkanContext.hpp"
#include "core/Contract.hpp"
#include "render/ResolvePass.hpp"
#include "render/VulkanHandle.hpp"
#include "view/RenderQuality.hpp"
#include "view/SceneClear.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vulkan/utility/vk_format_utils.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Vulkan's and VMA's C structs are written here as designated initialisers
// that name the fields that matter and leave the rest -- pNext, flags, often a
// dozen more -- value-initialised to zero or null, which is what the
// specification asks of a field not in use: a null pNext, reserved flags of 0,
// a feature not requested left VK_FALSE. -Wmissing-designated-field-initializers
// reports every one of those, 28 when it was switched on (2026-09-11), so it is
// off for this file -- the one that fills in Vulkan's structs -- and on
// everywhere else (ADR 0017). The cost: a struct of our own initialised in
// this file goes unchecked too.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif

namespace orb::gfx {
namespace {

enum class FenceState : std::uint8_t {
    Unsignalled,
    Signalled,
};

[[nodiscard]] std::unexpected<RenderError> fail(std::string message) {
    return std::unexpected(RenderError{.message = std::move(message)});
}

[[nodiscard]] std::unexpected<RenderError> failSdl(std::string_view what) {
    return fail(std::string(what) + " failed: " + SDL_GetError());
}

// Vulkan-Utility-Libraries names every result, including the ones added after
// this was written. It replaced a hand-written switch here on 2026-09-16: that
// named nineteen and printed a bare number for anything else, which is the
// class of thing better maintained by the people who add the enumerators than
// by us. The switch also needed a -Wswitch-enum exemption and an #ifdef
// __clang__ around it, and both went with it.

// Every Vulkan and VMA call that returns a VkResult goes through here, so that
// none is dropped. This is rule 7 of the Power of Ten -- check the return
// value of every non-void function -- for the one API that [[nodiscard]]
// cannot reach. The name of the result is the useful part of the message:
// VK_ERROR_DEVICE_LOST and VK_ERROR_OUT_OF_DEVICE_MEMORY call for different
// responses.
[[nodiscard]] std::expected<void, RenderError> vkCheck(VkResult result, std::string_view what) {
    if (result == VK_SUCCESS) return {};
    return fail(std::string(what) + " failed: " + string_VkResult(result));
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
// Vulkan's *FlagBits enums carry a VK_..._MAX_ENUM = 0x7FFFFFFF enumerator,
// which forces a signed underlying type, while the matching VkFlags typedef is
// the unsigned bitmask type the API actually combines them in. Converting first
// keeps the arithmetic unsigned and names the type that owns the result.
[[nodiscard]] constexpr VkDebugUtilsMessageSeverityFlagsEXT
severityBits(VkDebugUtilsMessageSeverityFlagBitsEXT bit) noexcept {
    return static_cast<VkDebugUtilsMessageSeverityFlagsEXT>(bit);
}

VKAPI_ATTR VkBool32 VKAPI_CALL onValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                   void* userData) {
    if ((severityBits(severity) & severityBits(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) !=
        0) {
        auto* errors = static_cast<std::atomic<uint32_t>*>(userData);
        if (errors != nullptr) errors->fetch_add(1, std::memory_order_relaxed);
    }
    // SDL's C logging API is variadic, and the message is the loader's C
    // string, which the specification makes null-terminated but whose bounds
    // no type can carry; -Wunsafe-buffer-usage-in-format-attr-call reports
    // exactly that, and is off for this call (ADR 0017).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-format-attr-call"
#endif
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) -- SDL's C logging API is variadic
    SDL_Log("[validation] %s", data != nullptr ? data->pMessage : "(no message)");
#ifdef __clang__
#pragma clang diagnostic pop
#endif
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
        // SDL hands the extensions back as a pointer and a count, and a span
        // of the two is the only way to give them bounds; the two-argument
        // constructor is what -Wunsafe-buffer-usage-in-container reports, and
        // it is off for this line (ADR 0017).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
        const std::span extensions(sdlExts, sdlExtCount);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
        for (const char* const extension : extensions) {
            builder.enable_extension(extension);
        }
        if (requested == Validation::Enabled) {
            builder.request_validation_layers()
                .set_debug_callback(onValidationMessage)
                .set_debug_callback_user_data_pointer(&validationErrors)
                .set_debug_messenger_severity(
                    severityBits(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) |
                    severityBits(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT))
                // Synchronization validation (M1-14). The base layers check
                // each command on its own and do not see an image read before
                // the write it depends on has finished -- a hazard M1-14
                // creates for the first time, when the resolve pass reads what
                // the scene wrote. On wherever validation is, so orbsim_smoke
                // checks every barrier on every `check` rather than on one
                // manual run. It costs nothing without --validate.
                .add_validation_feature_enable(
                    VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
        }
        return builder.build();
    };

    // Validation layers ship with the SDK, not the driver, so a machine with
    // only a runtime will not have them. Fall back rather than refusing to run.
    auto built = build(validation);
    if (!built && validation == Validation::Enabled) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) -- SDL's C logging API is variadic
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

    // Always the discrete GPU where there is one.
    //
    // vk-bootstrap already *prefers* discrete, but it also allows any other
    // type by default, and that preference is not strong enough to matter on a
    // laptop where both devices satisfy every requirement: this project spent
    // its first sixteen commits rendering on an Intel UHD with an RTX A2000
    // sitting idle beside it. allow_any_gpu_device_type(false) is what turns
    // the preference into a requirement.
    // Not a bool. `pick(true)` and `pick(false)` at the call sites below were a
    // mystery that only the lambda's parameter name resolved, which is exactly
    // the shape non-negotiable 2 names -- clang-tidy does not see it because it
    // is a lambda, and the rule applies anyway.
    enum class DeviceChoice : std::uint8_t {
        DiscreteOnly,
        AnyType,
    };

    std::string selectionError;
    const auto pick = [&](DeviceChoice choice) -> std::optional<vkb::PhysicalDevice> {
        vkb::PhysicalDeviceSelector selector{instance};
        auto result = selector.set_surface(surface)
                          .set_minimum_version(1, 3)
                          .set_required_features(features10)
                          .set_required_features_12(features12)
                          .set_required_features_13(features13)
                          .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                          .allow_any_gpu_device_type(choice == DeviceChoice::AnyType)
                          .select();
        if (!result) {
            selectionError = result.error().message();
            return std::nullopt;
        }
        return result.value();
    };

    auto physical = pick(DeviceChoice::DiscreteOnly);
    if (!physical) {
        // A machine with only an integrated GPU is an ordinary machine, and the
        // simulator should still run on it. Say which way it went, because
        // "why is this slow" is otherwise a long afternoon.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) -- SDL's C logging API is variadic
        SDL_Log("No suitable discrete GPU (%s); falling back to any device type.",
                selectionError.c_str());
        physical = pick(DeviceChoice::AnyType);
    }
    if (!physical) return fail("No suitable Vulkan 1.3 device: " + selectionError);

    vkb::DeviceBuilder const deviceBuilder{*physical};
    auto device = deviceBuilder.build();
    if (!device) return fail("Vulkan device creation failed: " + device.error().message());

    auto queue = device.value().get_queue(vkb::QueueType::graphics);
    auto family = device.value().get_queue_index(vkb::QueueType::graphics);
    if (!queue || !family) return fail("No graphics queue available");

    return DeviceBundle{
        .device = device.value(),
        .physicalDevice = physical->physical_device,
        .graphicsQueue = queue.value(),
        .graphicsQueueFamily = family.value(),
        .name = physical->name,
    };
}

// Whether the device can draw into kHdrFormat and let a shader read it. Both
// are mandatory in the Vulkan specification, so a conformant device always
// passes; the device is asked anyway, once, because "mandatory" is a claim
// about the device and the device is the one to ask (M1-14).
[[nodiscard]] std::expected<void, RenderError> checkHdrFormat(VkPhysicalDevice physicalDevice) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, kHdrFormat, &properties);
    const VkFormatFeatureFlags have = properties.optimalTilingFeatures;
    const auto lacks = [have](VkFormatFeatureFlagBits feature) {
        return (have & static_cast<VkFormatFeatureFlags>(feature)) == 0;
    };
    if (lacks(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
        return fail(std::string("The GPU cannot render into ") + string_VkFormat(kHdrFormat) +
                    ", which the Vulkan specification makes mandatory");
    }
    if (lacks(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
        return fail(std::string("The GPU cannot read ") + string_VkFormat(kHdrFormat) +
                    " in a shader, which the Vulkan specification makes mandatory");
    }
    // Since M1-16 the target is also copied out, by a probe's readback
    // (register decision 188).
    if (lacks(VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) {
        return fail(std::string("The GPU cannot copy out of ") + string_VkFormat(kHdrFormat) +
                    ", which a probe's readback needs");
    }
    return {};
}

// Whether the device can draw into a probe display format and copy it out
// (M1-16). kProbeDisplayFormat8's uses are mandatory in the specification and
// kProbeDisplayFormat16's are not, so both are asked, and a missing one is
// reported by name rather than drawn wrong.
[[nodiscard]] std::expected<void, RenderError>
checkProbeDisplayFormat(VkPhysicalDevice physicalDevice, VkFormat format) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
    const VkFormatFeatureFlags have = properties.optimalTilingFeatures;
    const auto has = [have](VkFormatFeatureFlagBits feature) {
        return (have & static_cast<VkFormatFeatureFlags>(feature)) != 0;
    };
    if (!has(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) || !has(VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) {
        return fail(std::string("The GPU cannot render into and copy out of ") +
                    string_VkFormat(format) + ", which a probe's display image needs");
    }
    return {};
}

// The swapchain formats this renderer can present, best first. Every one is
// UNORM in the sRGB colour space, because tonemap.frag writes the sRGB encode
// itself (ADR 0014); an _SRGB format would have the hardware encode a second
// time and wash the image out. Which of them a surface offers depends on the
// GPU, the driver and the display, and the list is what lets the renderer run
// where the first is missing: 8-bit BGRA is what Windows drivers offer first,
// 8-bit RGBA is common elsewhere, and the two 10-bit layouts serve displays
// that offer only those.
constexpr std::array kPresentableFormats{
    VkSurfaceFormatKHR{
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    },
    VkSurfaceFormatKHR{
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    },
    VkSurfaceFormatKHR{
        .format = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    },
    VkSurfaceFormatKHR{
        .format = VK_FORMAT_A2R10G10B10_UNORM_PACK32,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    },
};

// **vk-bootstrap does not refuse when none of the list is offered**: it takes
// whatever the driver lists first (find_best_surface_format, v1.3.302), which
// may be an _SRGB format or an HDR colour space. So what came back is checked
// against Vulkan-Utility-Libraries' format table -- an opinion formed without
// this code -- and refused by name rather than presented wrong.
[[nodiscard]] std::expected<void, RenderError> checkPresentableFormat(VkFormat format,
                                                                      VkColorSpaceKHR colourSpace) {
    if (colourSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return fail(std::string("The display offers no sRGB swapchain; the closest was ") +
                    string_VkFormat(format) + " in " + string_VkColorSpaceKHR(colourSpace));
    }
    if (!vkuFormatIsUNORM(format) || vkuFormatIsSRGB(format)) {
        return fail(std::string("The display offers no UNORM swapchain format; the closest was ") +
                    string_VkFormat(format) + ", which would apply the sRGB encode a second time");
    }
    return {};
}

// Hands vk-bootstrap kPresentableFormats in order: the first as the one
// wanted, the rest as fallbacks it tries in turn.
void requestPresentableFormats(vkb::SwapchainBuilder& builder) {
    builder.set_desired_format(kPresentableFormats.front());
    for (const VkSurfaceFormatKHR& fallback : std::span(kPresentableFormats).subspan(1)) {
        builder.add_fallback_format(fallback);
    }
}

// Both passes cover the whole target, and the viewport and scissor are
// dynamic in every pipeline (render/Pipeline.cpp), so each pass sets them.
//
// **Not flipped.** The one vertical flip lives in the projection matrix, as a
// single negated entry (view/Projection.hpp, which says so and is tested for
// it). This viewport used to flip as well, with a negative height, and the
// two together would have drawn every frame upside down -- invisible while
// nothing was drawn, and found by reading the two side by side before M1-19
// draws the first line (register decision 154).
void setFullViewport(VkCommandBuffer cmd, VkExtent2D extent) {
    const VkViewport viewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    };
    const VkRect2D scissor{.offset = {.x = 0, .y = 0}, .extent = extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

// What the scene draws into: an HDR target and a depth image of one extent.
// The window's frame and a probe's both begin their scene with this, so the
// two are cleared and set up by the same code (register decision 187).
struct SceneTargets {
    VkImageView hdr{VK_NULL_HANDLE};
    VkImageView depth{VK_NULL_HANDLE};
    VkExtent2D extent{};
};

// Begins the scene's rendering, both attachments already in their attachment
// layouts, and sets the viewport over the whole target.
void beginSceneRendering(VkCommandBuffer cmd, const SceneTargets& targets) {
    // The scene clears to radiance, not to a display colour: zero, where
    // nothing is drawn (view/SceneClear.hpp says why it is not a tuning
    // constant).
    constexpr orb::view::LinearRgba kClear = orb::view::kSceneClear;
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = targets.hdr,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {{kClear.red, kClear.green, kClear.blue, kClear.alpha}}},
    };
    const VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = targets.depth,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {.depth = kDepthClear, .stencil = 0}},
    };

    const VkRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {.x = 0, .y = 0}, .extent = targets.extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };
    vkCmdBeginRendering(cmd, &rendering);
    setFullViewport(cmd, targets.extent);
}

// The scene's two images, by name: both are VkImage, and reversed each would
// be moved into the other's layout (non-negotiable 1).
struct SceneImages {
    VkImage hdr{VK_NULL_HANDLE};
    VkImage depth{VK_NULL_HANDLE};
};

// The HDR target and the depth image, from nothing into their attachment
// layouts. From UNDEFINED, because the scene clears both: what a target held
// before is discarded, which is also what makes a frame independent of the
// one before it.
void prepareSceneTargets(VkCommandBuffer cmd, const SceneImages& images) {
    transitionImage(cmd,
                    images.hdr,
                    {
                        .from = VK_IMAGE_LAYOUT_UNDEFINED,
                        .to = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    });
    transitionImage(cmd,
                    images.depth,
                    {
                        .from = VK_IMAGE_LAYOUT_UNDEFINED,
                        .to = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    },
                    VK_IMAGE_ASPECT_DEPTH_BIT);
}

// What one resolve draws into, and what it reads.
struct ResolveTarget {
    VkImage display{VK_NULL_HANDLE};
    VkImageView displayView{VK_NULL_HANDLE};
    VkImageView hdr{VK_NULL_HANDLE}; // already in SHADER_READ_ONLY_OPTIMAL
    VkExtent2D extent{};
};

// One resolve: the display image from nothing into an attachment, cleared,
// and covered by the resolve pass's triangle. The window's frame and a
// probe's both display through this (register decision 187).
void recordResolveInto(VkCommandBuffer cmd,
                       const ResolveTarget& target,
                       const ResolvePass& resolve,
                       uint32_t frameIndex) {
    transitionImage(cmd,
                    target.display,
                    {
                        .from = VK_IMAGE_LAYOUT_UNDEFINED,
                        .to = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    });
    // Cleared to black before the triangle covers it. The triangle writes
    // every pixel, so the clear is never seen -- unless the triangle is ever
    // wrong, and then the uncovered part is black on every frame rather than
    // whatever the image last held.
    const VkRenderingAttachmentInfo displayAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = target.displayView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {{0.0F, 0.0F, 0.0F, 1.0F}}},
    };
    const VkRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {.x = 0, .y = 0}, .extent = target.extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &displayAttachment,
    };
    vkCmdBeginRendering(cmd, &rendering);
    setFullViewport(cmd, target.extent);
    resolve.record(cmd, frameIndex, target.hdr);
    vkCmdEndRendering(cmd);
}

// What a copy out reads and writes: the whole of a colour image, in the
// layout it was left in, into a buffer tightly packed, rows top to bottom.
// **The layout it is in is part of the request**, because moving an image to
// TRANSFER_SRC_OPTIMAL from UNDEFINED would let the driver discard exactly the
// contents about to be copied.
struct CopyOut {
    VkImage image{VK_NULL_HANDLE};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkBuffer buffer{VK_NULL_HANDLE};
    VkExtent2D extent{};
};

void copyImageToBuffer(VkCommandBuffer cmd, const CopyOut& copy) {
    transitionImage(cmd,
                    copy.image,
                    {
                        .from = copy.layout,
                        .to = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    });
    const VkBufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,   // 0: tightly packed, the image's own width
        .bufferImageHeight = 0, // and its own height
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .imageOffset = {.x = 0, .y = 0, .z = 0},
        .imageExtent = {.width = copy.extent.width, .height = copy.extent.height, .depth = 1},
    };
    vkCmdCopyImageToBuffer(
        cmd, copy.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, copy.buffer, 1, &region);
}

// The copies' writes made visible to the host. A fence's signal makes the
// device's writes available but not visible to the host; this barrier is the
// dependency the specification's synchronisation chapter asks for before the
// host reads memory a transfer wrote.
void makeWritesVisibleToHost(VkCommandBuffer cmd) {
    const VkMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
    };
    const VkDependencyInfo dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace

// ---------------------------------------------------------------------------

void transitionImage(VkCommandBuffer cmd,
                     VkImage image,
                     const LayoutTransition& layouts,
                     VkImageAspectFlags aspect) {
    const VkImageLayout from = layouts.from;
    const VkImageLayout to = layouts.to;
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

    if (auto hdr = checkHdrFormat(ctx.physicalDevice_); !hdr) return std::unexpected(hdr.error());

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

    releaseSizedResources();

    vkb::SwapchainBuilder builder{
        physicalDevice_, device_.get(), surface_.get(), graphicsQueueFamily_, graphicsQueueFamily_};

    // UNORM, never _SRGB: the resolve pass writes the sRGB encode itself, once
    // (ADR 0014), and an _SRGB swapchain would encode a second time. A list,
    // because what a surface offers depends on the GPU, the driver and the
    // display; see kPresentableFormats.
    requestPresentableFormats(builder);
    auto built =
        builder
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
    // Checked before anything is built on it. The new swapchain is not wrapped
    // yet, so a refusal destroys it here; the retired one stays with
    // swapchain_ and is destroyed with it.
    if (auto presentable =
            checkPresentableFormat(vkbSwapchain.image_format, vkbSwapchain.color_space);
        !presentable) {
        vkb::destroy_swapchain(vkbSwapchain);
        return presentable;
    }
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
    if (auto hdr = createHdrTarget(); !hdr) return hdr;

    swapchainDirty_ = false;
    return {};
}

// An image of any format and size, device-local, with a view onto all of it.
// The window's HDR target and depth image and a probe's own are all made
// here, so a probe's are the window's, only sized differently (register
// decision 187). Split out of createSwapchain long ago for the reason its
// callers keep: each resource reports its own failure.
std::expected<ImageAndView, RenderError>
VulkanContext::createImage(const ImageRequest& request) const {
    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = request.format,
        .extent = {.width = request.extent.width, .height = request.extent.height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = request.usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocInfo{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    const std::string what = std::string(" (") + string_VkFormat(request.format) + ")";
    if (auto ok = vkCheck(
            vmaCreateImage(allocator_.get(), &imageInfo, &allocInfo, &image, &allocation, nullptr),
            "vmaCreateImage" + what);
        !ok) {
        return std::unexpected(ok.error());
    }
    ImageAndView made{.image = UniqueImage{allocator_.get(), image, allocation}, .view = {}};

    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = made.image.get(),
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = request.format,
        .subresourceRange =
            {
                .aspectMask = request.aspect,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
    VkImageView view = VK_NULL_HANDLE;
    if (auto ok = vkCheck(vkCreateImageView(device_.get(), &viewInfo, nullptr, &view),
                          "vkCreateImageView" + what);
        !ok) {
        return std::unexpected(ok.error());
    }
    made.view = UniqueImageView{device_.get(), view};
    return made;
}

namespace {

// The depth image's request, for any extent: 32-bit float, reverse-Z (ADR
// 0003).
[[nodiscard]] ImageRequest depthRequest(VkExtent2D extent) {
    return {
        .format = kDepthFormat,
        .extent = extent,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
    };
}

// The HDR target's request, for any extent (ADR 0014): drawn into, read by the
// resolve pass -- the two uses kHdrFormat's comment says the specification
// guarantees -- and, since M1-16, copied out by a probe's readback. **One
// description for the window's target and a probe's** (register decision
// 188), so a probe reads back the application's HDR target and not a
// look-alike.
[[nodiscard]] ImageRequest hdrRequest(VkExtent2D extent) {
    return {
        .format = kHdrFormat,
        .extent = extent,
        .usage = VkImageUsageFlags{VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT} |
                 VkImageUsageFlags{VK_IMAGE_USAGE_SAMPLED_BIT} |
                 VkImageUsageFlags{VK_IMAGE_USAGE_TRANSFER_SRC_BIT},
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

// A probe's display image: drawn into by the resolve pass, then copied out.
[[nodiscard]] ImageRequest probeDisplayRequest(VkFormat format, VkExtent2D extent) {
    return {
        .format = format,
        .extent = extent,
        .usage = VkImageUsageFlags{VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT} |
                 VkImageUsageFlags{VK_IMAGE_USAGE_TRANSFER_SRC_BIT},
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

} // namespace

std::expected<void, RenderError> VulkanContext::createDepthAttachment() {
    auto depth = createImage(depthRequest(swapchainExtent_));
    if (!depth) return std::unexpected(depth.error());
    depth_ = std::move(*depth);
    return {};
}

std::expected<void, RenderError> VulkanContext::createHdrTarget() {
    auto hdr = createImage(hdrRequest(swapchainExtent_));
    if (!hdr) return std::unexpected(hdr.error());
    hdr_ = std::move(*hdr);
    return {};
}

// Everything sized to the swapchain, released before it is rebuilt. Children
// before parents: the image views reference the swapchain, and each view its
// image; Vulkan wants them gone before the objects they were created from.
// Everything here is idle -- recreateSwapchain waited for the device -- so
// the order is the only thing that matters. Split out of createSwapchain,
// which the HDR target pushed past readability-function-size (M1-14).
void VulkanContext::releaseSizedResources() noexcept {
    swapchainViews_.clear();
    renderFinished_.clear();
    depth_.view.reset();
    depth_.image.reset();
    hdr_.view.reset();
    hdr_.image.reset();
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

std::expected<std::optional<FrameContext>, RenderError>
VulkanContext::beginFrame(orb::view::RenderQuality quality) {
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

    prepareSceneTargets(cmd, {.hdr = hdr_.image.get(), .depth = depth_.image.get()});
    beginSceneRendering(cmd,
                        {
                            .hdr = hdr_.view.get(),
                            .depth = depth_.view.get(),
                            .extent = swapchainExtent_,
                        });

    return FrameContext{
        .cmd = cmd,
        .imageIndex = imageIndex,
        .frameIndex = frame,
        .extent = swapchainExtent_,
        .quality = quality,
    };
}

// The finished HDR target becomes an image a shader reads, and the swapchain
// image becomes the one thing the resolve pass draws into. The first of these
// barriers is the project's first read-after-write dependency between two
// passes, which synchronization validation checks under --validate.
void VulkanContext::recordResolve(const FrameContext& frame, const ResolvePass& resolve) const {
    transitionImage(frame.cmd,
                    hdr_.image.get(),
                    {
                        .from = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .to = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    });
    recordResolveInto(frame.cmd,
                      {
                          .display = swapchainImages_.at(frame.imageIndex),
                          .displayView = swapchainViews_.at(frame.imageIndex).get(),
                          .hdr = hdr_.view.get(),
                          .extent = swapchainExtent_,
                      },
                      resolve,
                      frame.frameIndex);
}

std::expected<void, RenderError> VulkanContext::endFrame(const FrameContext& frame,
                                                         const ResolvePass& resolve) {
    vkCmdEndRendering(frame.cmd);
    recordResolve(frame, resolve);

    transitionImage(frame.cmd,
                    swapchainImages_.at(frame.imageIndex),
                    {
                        .from = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .to = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    });
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

std::expected<UniqueBuffer, RenderError> VulkanContext::createBuffer(const BufferRequest& request) {
    const VkDeviceSize size = request.size;
    const VkBufferUsageFlags usage = request.usage;
    const Memory memory = request.memory;
    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_AUTO};
    // As above: VmaAllocationCreateFlags is the unsigned type these bits are
    // meant to be combined in; the FlagBits enum itself is signed.
    constexpr auto kMapped =
        static_cast<VmaAllocationCreateFlags>(VMA_ALLOCATION_CREATE_MAPPED_BIT);
    switch (memory) {
    case Memory::DeviceLocal:
        break;
    case Memory::HostVisible:
        allocInfo.flags = static_cast<VmaAllocationCreateFlags>(
                              VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT) |
                          kMapped;
        break;
    case Memory::HostReadback:
        // Random access, which VMA answers with cached memory where there is
        // some: memory made for writing in sequence is slow to read back.
        allocInfo.flags =
            static_cast<VmaAllocationCreateFlags>(VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT) |
            kMapped;
        break;
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
    //
    // VMA hands mapped memory back as a bare pointer, and the way to write
    // through one is a C library copy, which -Wunsafe-buffer-usage-in-libc-call
    // reports; it is off for the two copies (ADR 0017). The bound is checked
    // above: the data is no larger than either buffer.
    if (dst.mapped() != nullptr) {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-libc-call"
#endif
        std::memcpy(dst.mapped(), data.data(), data.size());
#ifdef __clang__
#pragma clang diagnostic pop
#endif
        return {};
    }

    auto staging = createBuffer({
        .size = data.size(),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .memory = Memory::HostVisible,
    });
    if (!staging) return std::unexpected(staging.error());
    if (staging->mapped() == nullptr) return fail("Staging buffer was not mapped");
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-libc-call"
#endif
    std::memcpy(staging->mapped(), data.data(), data.size());
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    // Complete before return, not merely submitted: the staging buffer
    // destroys itself then.
    const VkBufferCopy copy{.srcOffset = 0, .dstOffset = 0, .size = data.size()};
    return submitImmediate(
        [&](VkCommandBuffer cmd) { vkCmdCopyBuffer(cmd, staging->get(), dst.get(), 1, &copy); });
}

// Records into the immediate-submit command buffer, submits it alone, and
// waits for it to finish. For uploads, and since M1-16 for a probe's frame:
// work that must be complete, not merely submitted, when the caller goes on.
// The fence is waited on without a timeout, for the reason beginFrame's is:
// a lost device reports itself through the wait's result rather than a
// timeout's guess, and nothing here reads a clock.
std::expected<void, RenderError>
VulkanContext::submitImmediate(const std::function<void(VkCommandBuffer)>& record) {
    VkFence fence = uploadFence_.get();
    if (auto ok = vkCheck(vkResetFences(device_.get(), 1, &fence), "vkResetFences (immediate)");
        !ok) {
        return ok;
    }
    if (auto ok = beginOneTimeCommandBuffer(uploadCmd_); !ok) return ok;
    record(uploadCmd_);
    if (auto ok = vkCheck(vkEndCommandBuffer(uploadCmd_), "vkEndCommandBuffer (immediate)"); !ok) {
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
    if (auto ok = vkCheck(vkQueueSubmit2(graphicsQueue_, 1, &submit, fence),
                          "vkQueueSubmit2 (immediate)");
        !ok) {
        return ok;
    }
    return vkCheck(vkWaitForFences(device_.get(), 1, &fence, VK_TRUE, UINT64_MAX),
                   "vkWaitForFences (immediate)");
}

namespace {

// The bytes of one pixel of each image a probe reads back, and so each
// readback buffer's size per pixel.
constexpr VkDeviceSize kHdrBytesPerPixel = 8;       // four binary16 channels
constexpr VkDeviceSize kDisplay8BytesPerPixel = 4;  // four 8-bit channels
constexpr VkDeviceSize kDisplay16BytesPerPixel = 8; // four 16-bit channels

// A readback buffer's contents, once the GPU's writes are visible to the host
// (makeWritesVisibleToHost, then the fence): invalidated first, because
// memory that is not host-coherent may hold stale cache lines until it is,
// and then copied out whole.
template <typename Value>
[[nodiscard]] std::expected<std::vector<Value>, RenderError> readBack(VmaAllocator allocator,
                                                                      const UniqueBuffer& buffer) {
    if (auto ok = vkCheck(vmaInvalidateAllocation(allocator, buffer.allocation(), 0, VK_WHOLE_SIZE),
                          "vmaInvalidateAllocation (readback)");
        !ok) {
        return std::unexpected(ok.error());
    }
    if (buffer.mapped() == nullptr) return fail("Readback buffer was not mapped");
    std::vector<Value> values(static_cast<std::size_t>(buffer.size()) / sizeof(Value));
    // VMA hands mapped memory back as a bare pointer, and the way to read
    // through one is a C library copy, which -Wunsafe-buffer-usage-in-libc-call
    // reports; off for this copy, as for uploadBuffer's two (ADR 0017). The
    // bound is the buffer's own size, which the vector was sized from.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-libc-call"
#endif
    std::memcpy(values.data(), buffer.mapped(), values.size() * sizeof(Value));
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    return values;
}

} // namespace

// Everything one probe frame is drawn into and read back through, made for
// the frame and released with it.
struct VulkanContext::OffscreenTargets {
    ImageAndView hdr;
    ImageAndView depth;
    ImageAndView display8;
    ImageAndView display16;
    UniqueBuffer readHdr;
    UniqueBuffer read8;
    UniqueBuffer read16;
};

namespace {

// The frame's commands: the scene, both resolves, and the three copies, in
// one command buffer. Frame slot 0's descriptor set in each resolve pass,
// which is safe because a probe's passes are its own and nothing else is in
// flight.
void recordOffscreen(VkCommandBuffer cmd,
                     const VulkanContext::OffscreenTargets& targets,
                     VkExtent2D extent,
                     const SceneRecorder& scene,
                     const ProbeResolves& resolves) {
    prepareSceneTargets(cmd,
                        {
                            .hdr = targets.hdr.image.get(),
                            .depth = targets.depth.image.get(),
                        });
    beginSceneRendering(cmd,
                        {
                            .hdr = targets.hdr.view.get(),
                            .depth = targets.depth.view.get(),
                            .extent = extent,
                        });
    scene(cmd);
    vkCmdEndRendering(cmd);

    transitionImage(cmd,
                    targets.hdr.image.get(),
                    {
                        .from = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .to = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    });
    recordResolveInto(cmd,
                      {
                          .display = targets.display8.image.get(),
                          .displayView = targets.display8.view.get(),
                          .hdr = targets.hdr.view.get(),
                          .extent = extent,
                      },
                      *resolves.eightBit,
                      0);
    recordResolveInto(cmd,
                      {
                          .display = targets.display16.image.get(),
                          .displayView = targets.display16.view.get(),
                          .hdr = targets.hdr.view.get(),
                          .extent = extent,
                      },
                      *resolves.sixteenBit,
                      0);

    copyImageToBuffer(cmd,
                      {
                          .image = targets.hdr.image.get(),
                          .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          .buffer = targets.readHdr.get(),
                          .extent = extent,
                      });
    copyImageToBuffer(cmd,
                      {
                          .image = targets.display8.image.get(),
                          .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          .buffer = targets.read8.get(),
                          .extent = extent,
                      });
    copyImageToBuffer(cmd,
                      {
                          .image = targets.display16.image.get(),
                          .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          .buffer = targets.read16.get(),
                          .extent = extent,
                      });
    makeWritesVisibleToHost(cmd);
}

} // namespace

std::expected<VulkanContext::OffscreenTargets, RenderError>
VulkanContext::createOffscreenTargets(VkExtent2D extent) {
    auto hdr = createImage(hdrRequest(extent));
    if (!hdr) return std::unexpected(hdr.error());
    auto depth = createImage(depthRequest(extent));
    if (!depth) return std::unexpected(depth.error());
    auto display8 = createImage(probeDisplayRequest(kProbeDisplayFormat8, extent));
    if (!display8) return std::unexpected(display8.error());
    auto display16 = createImage(probeDisplayRequest(kProbeDisplayFormat16, extent));
    if (!display16) return std::unexpected(display16.error());

    const VkDeviceSize pixels = VkDeviceSize{extent.width} * extent.height;
    const auto readback = [&](VkDeviceSize bytesPerPixel) {
        return createBuffer({
            .size = pixels * bytesPerPixel,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .memory = Memory::HostReadback,
        });
    };
    auto readHdr = readback(kHdrBytesPerPixel);
    if (!readHdr) return std::unexpected(readHdr.error());
    auto read8 = readback(kDisplay8BytesPerPixel);
    if (!read8) return std::unexpected(read8.error());
    auto read16 = readback(kDisplay16BytesPerPixel);
    if (!read16) return std::unexpected(read16.error());

    return OffscreenTargets{
        .hdr = std::move(*hdr),
        .depth = std::move(*depth),
        .display8 = std::move(*display8),
        .display16 = std::move(*display16),
        .readHdr = std::move(*readHdr),
        .read8 = std::move(*read8),
        .read16 = std::move(*read16),
    };
}

std::expected<OffscreenFrame, RenderError> VulkanContext::renderOffscreen(
    VkExtent2D extent, const SceneRecorder& scene, const ProbeResolves& resolves) {
    ORBSIM_EXPECTS(resolves.eightBit != nullptr && resolves.sixteenBit != nullptr);
    ORBSIM_EXPECTS(extent.width > 0 && extent.height > 0);
    for (const VkFormat format : {kProbeDisplayFormat8, kProbeDisplayFormat16}) {
        if (auto ok = checkProbeDisplayFormat(physicalDevice_, format); !ok) {
            return std::unexpected(ok.error());
        }
    }
    auto targets = createOffscreenTargets(extent);
    if (!targets) return std::unexpected(targets.error());

    if (auto ok = submitImmediate(
            [&](VkCommandBuffer cmd) { recordOffscreen(cmd, *targets, extent, scene, resolves); });
        !ok) {
        return std::unexpected(ok.error());
    }

    auto hdrHalf = readBack<std::uint16_t>(allocator_.get(), targets->readHdr);
    if (!hdrHalf) return std::unexpected(hdrHalf.error());
    auto eightBit = readBack<std::uint8_t>(allocator_.get(), targets->read8);
    if (!eightBit) return std::unexpected(eightBit.error());
    auto sixteenBit = readBack<std::uint16_t>(allocator_.get(), targets->read16);
    if (!sixteenBit) return std::unexpected(sixteenBit.error());
    return OffscreenFrame{
        .hdrHalf = std::move(*hdrHalf),
        .display8 = std::move(*eightBit),
        .display16 = std::move(*sixteenBit),
    };
}

DeviceDescription VulkanContext::deviceDescription() const {
    // driverID is set to a value its enum defines, which zero is not; the
    // query overwrites it.
    VkPhysicalDeviceDriverProperties driver{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
        .driverID = VK_DRIVER_ID_MAX_ENUM,
    };
    VkPhysicalDeviceProperties2 properties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &driver,
    };
    vkGetPhysicalDeviceProperties2(physicalDevice_, &properties);
    // The driver's two names are fixed-size, null-terminated char arrays.
    const auto text = [](const auto& chars) {
        return std::string(std::ranges::begin(chars), std::ranges::find(chars, '\0'));
    };
    return {
        .name = text(properties.properties.deviceName),
        .vendorId = properties.properties.vendorID,
        .deviceId = properties.properties.deviceID,
        .driverName = text(driver.driverName),
        .driverInfo = text(driver.driverInfo),
        .driverVersion = properties.properties.driverVersion,
        .apiVersion = properties.properties.apiVersion,
    };
}

std::expected<UniqueShaderModule, RenderError>
VulkanContext::loadShaderModule(const std::filesystem::path& path) const {
    // Opened binary and seeked, rather than `binary | ate`: std::ios::openmode
    // is a signed bitmask type, and one seek says the same thing.
    std::ifstream file(path, std::ios::binary);
    if (!file) return fail("Cannot open shader: " + path.string());

    file.seekg(0, std::ios::end);
    const std::streamsize size = file.tellg();
    if (size <= 0 || size % 4 != 0) {
        return fail("Shader is not valid SPIR-V (bad size): " + path.string());
    }

    std::vector<uint32_t> code(static_cast<size_t>(size) / 4);
    file.seekg(0);
    // The buffer is uint32_t because that is what VkShaderModuleCreateInfo::pCode
    // takes, and reading into it needs char* because std::istream::read accepts
    // nothing else. Reading into a byte buffer instead would move the cast to the
    // other side, onto a pointer with weaker alignment.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
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

#ifdef __clang__
#pragma clang diagnostic pop
#endif
