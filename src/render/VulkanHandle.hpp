#pragma once
//
// Move-only RAII wrappers for Vulkan handles.
//
// This header is the one place in the renderer that writes destructors and move
// operations by hand. That is the point: CODING_GUIDELINES.md section 4 asks for
// Rule of Zero, and the way you get it for a class holding nineteen C handles is
// not to write a nineteen-step teardown function -- it is to make each handle
// own itself, once, here.
//
// What that buys, concretely:
//
//   * VulkanContext declares no destructor, no copy and no move. The compiler
//     writes them and cannot get them wrong.
//   * Destruction order is reverse declaration order, which the *language*
//     enforces. Previously it was a hand-maintained sequence inside shutdown(),
//     and adding a handle meant remembering to edit two places.
//   * A failure part-way through initialisation unwinds correctly on its own.
//     There is no half-built object whose teardown has to check every handle
//     against VK_NULL_HANDLE.
//
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <utility>

namespace orb::gfx {

// A handle destroyed by a function shaped like
// Destroy(owner, handle, const VkAllocationCallbacks*) -- which is most of
// Vulkan: surfaces, swapchains, image views, semaphores, fences, command pools.
//
// `Destroy` is an `auto` non-type parameter so the calling convention
// (VKAPI_PTR) never has to be spelled out here.
template <typename Handle, typename Owner, auto Destroy> class OwnedHandle {
public:
    OwnedHandle() noexcept = default;

    OwnedHandle(Owner owner, Handle handle) noexcept : owner_(owner), handle_(handle) {}

    ~OwnedHandle() { reset(); }

    OwnedHandle(const OwnedHandle&) = delete;
    OwnedHandle& operator=(const OwnedHandle&) = delete;

    OwnedHandle(OwnedHandle&& other) noexcept
        : owner_(std::exchange(other.owner_, {})), handle_(std::exchange(other.handle_, {})) {}

    OwnedHandle& operator=(OwnedHandle&& other) noexcept {
        if (this != &other) {
            reset();
            owner_ = std::exchange(other.owner_, {});
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    [[nodiscard]] Handle get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != VK_NULL_HANDLE; }

    void reset() noexcept {
        if (handle_ != VK_NULL_HANDLE) {
            Destroy(owner_, handle_, nullptr);
            handle_ = VK_NULL_HANDLE;
        }
    }

private:
    Owner owner_{};
    Handle handle_{VK_NULL_HANDLE};
};

using UniqueSurface = OwnedHandle<VkSurfaceKHR, VkInstance, vkDestroySurfaceKHR>;
using UniqueSwapchain = OwnedHandle<VkSwapchainKHR, VkDevice, vkDestroySwapchainKHR>;
using UniqueImageView = OwnedHandle<VkImageView, VkDevice, vkDestroyImageView>;
using UniqueSemaphore = OwnedHandle<VkSemaphore, VkDevice, vkDestroySemaphore>;
using UniqueFence = OwnedHandle<VkFence, VkDevice, vkDestroyFence>;
using UniqueCommandPool = OwnedHandle<VkCommandPool, VkDevice, vkDestroyCommandPool>;

// The remaining four do not fit that shape, so each gets its own small type
// rather than a template contorted to cover them.

// vkDestroyInstance takes no owner.
class UniqueInstance {
public:
    UniqueInstance() noexcept = default;
    explicit UniqueInstance(VkInstance instance) noexcept : instance_(instance) {}

    ~UniqueInstance() { reset(); }

    UniqueInstance(const UniqueInstance&) = delete;
    UniqueInstance& operator=(const UniqueInstance&) = delete;

    UniqueInstance(UniqueInstance&& other) noexcept
        : instance_(std::exchange(other.instance_, VK_NULL_HANDLE)) {}

    UniqueInstance& operator=(UniqueInstance&& other) noexcept {
        if (this != &other) {
            reset();
            instance_ = std::exchange(other.instance_, VK_NULL_HANDLE);
        }
        return *this;
    }

    [[nodiscard]] VkInstance get() const noexcept { return instance_; }
    [[nodiscard]] explicit operator bool() const noexcept { return instance_ != VK_NULL_HANDLE; }

    void reset() noexcept {
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

private:
    VkInstance instance_{VK_NULL_HANDLE};
};

// vkDestroyDevice likewise.
class UniqueDevice {
public:
    UniqueDevice() noexcept = default;
    explicit UniqueDevice(VkDevice device) noexcept : device_(device) {}

    ~UniqueDevice() { reset(); }

    UniqueDevice(const UniqueDevice&) = delete;
    UniqueDevice& operator=(const UniqueDevice&) = delete;

    UniqueDevice(UniqueDevice&& other) noexcept
        : device_(std::exchange(other.device_, VK_NULL_HANDLE)) {}

    UniqueDevice& operator=(UniqueDevice&& other) noexcept {
        if (this != &other) {
            reset();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        }
        return *this;
    }

    [[nodiscard]] VkDevice get() const noexcept { return device_; }
    [[nodiscard]] explicit operator bool() const noexcept { return device_ != VK_NULL_HANDLE; }

    void reset() noexcept {
        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
    }

private:
    VkDevice device_{VK_NULL_HANDLE};
};

// The debug messenger is an extension object, so its destroy function has to be
// fetched through vkGetInstanceProcAddr. Doing that here rather than calling
// vkb::destroy_debug_utils_messenger keeps vk-bootstrap out of this header, and
// therefore out of every translation unit that includes it.
class UniqueDebugMessenger {
public:
    UniqueDebugMessenger() noexcept = default;
    UniqueDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) noexcept
        : instance_(instance), messenger_(messenger) {}

    ~UniqueDebugMessenger() { reset(); }

    UniqueDebugMessenger(const UniqueDebugMessenger&) = delete;
    UniqueDebugMessenger& operator=(const UniqueDebugMessenger&) = delete;

    UniqueDebugMessenger(UniqueDebugMessenger&& other) noexcept
        : instance_(std::exchange(other.instance_, VK_NULL_HANDLE)),
          messenger_(std::exchange(other.messenger_, VK_NULL_HANDLE)) {}

    UniqueDebugMessenger& operator=(UniqueDebugMessenger&& other) noexcept {
        if (this != &other) {
            reset();
            instance_ = std::exchange(other.instance_, VK_NULL_HANDLE);
            messenger_ = std::exchange(other.messenger_, VK_NULL_HANDLE);
        }
        return *this;
    }

    [[nodiscard]] VkDebugUtilsMessengerEXT get() const noexcept { return messenger_; }

    void reset() noexcept {
        if (messenger_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
            const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy != nullptr) destroy(instance_, messenger_, nullptr);
        }
        messenger_ = VK_NULL_HANDLE;
    }

private:
    VkInstance instance_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT messenger_{VK_NULL_HANDLE};
};

// Waits for the GPU to go idle when it is destroyed.
//
// This is not an owning handle -- it holds the device without destroying it.
// Its job is ordering. Declared as the LAST member of VulkanContext, it is
// therefore destroyed FIRST, which is exactly when the wait has to happen:
// every handle below it is still in use by in-flight work until the device
// goes quiet.
//
// The hand-written shutdown() this replaced opened with the same
// vkDeviceWaitIdle. Deleting shutdown() dropped it, and the validation layers
// caught that immediately -- "vkDestroyCommandPool(): VkCommandBuffer is in
// use". Encoding the requirement as a member means it cannot be dropped again.
class DeviceIdleGuard {
public:
    DeviceIdleGuard() noexcept = default;
    explicit DeviceIdleGuard(VkDevice device) noexcept : device_(device) {}

    ~DeviceIdleGuard() {
        if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
    }

    DeviceIdleGuard(const DeviceIdleGuard&) = delete;
    DeviceIdleGuard& operator=(const DeviceIdleGuard&) = delete;

    DeviceIdleGuard(DeviceIdleGuard&& other) noexcept
        : device_(std::exchange(other.device_, VK_NULL_HANDLE)) {}

    DeviceIdleGuard& operator=(DeviceIdleGuard&& other) noexcept {
        if (this != &other) {
            if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        }
        return *this;
    }

private:
    VkDevice device_{VK_NULL_HANDLE}; // non-owning; UniqueDevice owns it
};

// vmaDestroyAllocator takes only the allocator.
class UniqueAllocator {
public:
    UniqueAllocator() noexcept = default;
    explicit UniqueAllocator(VmaAllocator allocator) noexcept : allocator_(allocator) {}

    ~UniqueAllocator() { reset(); }

    UniqueAllocator(const UniqueAllocator&) = delete;
    UniqueAllocator& operator=(const UniqueAllocator&) = delete;

    UniqueAllocator(UniqueAllocator&& other) noexcept
        : allocator_(std::exchange(other.allocator_, nullptr)) {}

    UniqueAllocator& operator=(UniqueAllocator&& other) noexcept {
        if (this != &other) {
            reset();
            allocator_ = std::exchange(other.allocator_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] VmaAllocator get() const noexcept { return allocator_; }
    [[nodiscard]] explicit operator bool() const noexcept { return allocator_ != nullptr; }

    void reset() noexcept {
        if (allocator_ != nullptr) {
            vmaDestroyAllocator(allocator_);
            allocator_ = nullptr;
        }
    }

private:
    VmaAllocator allocator_{nullptr};
};

// A VMA image is an image and its allocation, freed together.
class UniqueImage {
public:
    UniqueImage() noexcept = default;
    UniqueImage(VmaAllocator allocator, VkImage image, VmaAllocation allocation) noexcept
        : allocator_(allocator), image_(image), allocation_(allocation) {}

    ~UniqueImage() { reset(); }

    UniqueImage(const UniqueImage&) = delete;
    UniqueImage& operator=(const UniqueImage&) = delete;

    UniqueImage(UniqueImage&& other) noexcept
        : allocator_(std::exchange(other.allocator_, nullptr)),
          image_(std::exchange(other.image_, VK_NULL_HANDLE)),
          allocation_(std::exchange(other.allocation_, nullptr)) {}

    UniqueImage& operator=(UniqueImage&& other) noexcept {
        if (this != &other) {
            reset();
            allocator_ = std::exchange(other.allocator_, nullptr);
            image_ = std::exchange(other.image_, VK_NULL_HANDLE);
            allocation_ = std::exchange(other.allocation_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] VkImage get() const noexcept { return image_; }
    [[nodiscard]] explicit operator bool() const noexcept { return image_ != VK_NULL_HANDLE; }

    void reset() noexcept {
        if (image_ != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, image_, allocation_);
            image_ = VK_NULL_HANDLE;
            allocation_ = nullptr;
        }
    }

private:
    VmaAllocator allocator_{nullptr};
    VkImage image_{VK_NULL_HANDLE};
    VmaAllocation allocation_{nullptr};
};

} // namespace orb::gfx
