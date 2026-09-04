// The single translation unit that instantiates the Vulkan Memory Allocator.
// Isolated in its own file so the implementation is compiled exactly once and
// its warnings stay out of the rest of the build.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
