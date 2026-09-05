# ADR 0004: The Vulkan headers are pinned; the SDK supplies only the loader and glslc

Status: accepted (2026-09-05; the build has done this since the first commit)

## Decision

`CMakeLists.txt` fetches Vulkan-Headers at a pinned tag with `FetchContent`,
before `find_package(Vulkan)` runs, so that the `Vulkan::Headers` target is
ours and `FindVulkan` will not replace it. The installed SDK contributes only
`vulkan-1.lib` (the loader) and `glslc` (the shader compiler). vk-bootstrap,
VulkanMemoryAllocator and SDL3 are fetched at pinned tags the same way, and
all of them are included as `SYSTEM` so their warnings are not ours.

The ordering of the two `FetchContent_MakeAvailable` calls is what makes this
true rather than merely intended, and the comment in `CMakeLists.txt` says
so.

## What we considered

**Headers from the installed SDK.** The default, and what `find_package`
does. Two developers with two SDK versions compile against two sets of
headers, and a struct that gained a field between them is a designated
initialiser that compiles on one machine and not the other. Worse, an SDK
upgrade silently changes what the project compiles against with no commit
recording it.

**Vendoring the headers into the repository.** Reproducible, and a few
megabytes of someone else's code in every diff. Pinning a tag gives the
same reproducibility without the weight, and the tag *is* the record.

**A package manager (vcpkg, Conan).** Reasonable for a larger dependency
set. Four dependencies at pinned tags is under the threshold where a
manager's own configuration pays for itself; revisit if the set grows.

## Why

Reproducibility. A build that depends on what happens to be installed is a
build that cannot be bisected: `git bisect` can revert the code but not the
SDK, so a failure introduced by an SDK change looks like a failure introduced
by a commit. Pinning every dependency puts every input to the build under
version control, which is the property section 17 of the guidelines is
about.

The loader and glslc are the exception because they are not inputs to the
*compile*: the loader is resolved at link time against a stable ABI, and
glslc's output is validated SPIR-V that any conforming compiler would also
produce. Neither changes what the source means.