#ifndef ORBSIM_CORE_ATTRIBUTES_HPP
#define ORBSIM_CORE_ATTRIBUTES_HPP
//
// Attributes one compiler knows and the others reject, spelled so that each
// compiler sees only what it understands.
//
// [[clang::lifetimebound]], and nothing at all on a compiler that does not know
// it. Added 2026-09-14, when `windows-msvc` became the third implementation and
// the first non-clang compiler ever to build the renderer. MSVC calls the
// attribute C5030, "not recognized", and /WX makes it fatal: it reported the
// first site -- OwnedHandle's constructor in render/VulkanHandle.hpp -- in each
// of four translation units and stopped there, so the other sixteen were never
// reached. gcc reports the same thing as -Wattributes wherever it is written,
// with its default flags (measured 2026-09-11), so the next compiler would
// have said so too.
//
// A macro rather than seventeen guarded sites. The alternatives were measured
// and are worse: `#pragma warning(push/disable/pop)` around each use is 51 lines
// of preprocessor threaded through the wrappers, and a project-wide /wd5030
// switches the diagnostic off for code nobody has written yet -- which is the
// thing CODING_GUIDELINES section 1 reserves for a library's interface, and this
// attribute is ours, not a library's.
//
// **Here since M1-19** (the owner's ruling, 2026-10-04). It lived in
// render/VulkanHandle.hpp while both its users were in src/render/, with a note
// that it would move down the day a second layer wanted it: view/LineBatch.hpp
// hands out a view of its vertices, which clang asks to carry the attribute,
// and src/view/ cannot include src/render/. In core, at the bottom of the
// layers, every library can reach it. It depends on nothing.
//
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) -- an attribute cannot be a
// function, and the whole job is to vanish on one compiler and not another.
#ifdef __clang__
#define ORBSIM_LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define ORBSIM_LIFETIMEBOUND
#endif

#endif // ORBSIM_CORE_ATTRIBUTES_HPP
