#ifndef ORBSIM_VIEW_FRAME_HPP
#define ORBSIM_VIEW_FRAME_HPP
//
// The frames a render-side transform maps between (M1-09, ADR 0021).
//
// **This lives in `src/view/` and not in `core/`, deliberately.** `src/astro/`
// already speaks frames -- `earthFixedFromInertial` is a frame conversion, and
// ICRF and ITRF are astronomy rather than rendering -- so there is a real case
// for one shared vocabulary. It is not taken yet, because moving it down means
// starting the conversation ADR 0019 deliberately left open: whether `Vec3`
// and `Quat` carry frames too. Milestone 1's scope fence keeps the core
// frame-free, and the day a second layer wants this enum is the day it moves,
// not before -- the same rule `render/VulkanHandle.hpp` states for its own
// macro.
//
// **A closed set, extended when a task needs a value.** Three is what
// milestone 1 uses; `EarthFixed` arrives with the planetary grid and a tile
// frame with the quadtree, each with the task that needs it
// (CODING_GUIDELINES section 17 -- write the specific thing).
//
namespace orb::view {

enum class Frame : unsigned char {
    // The inertial world the simulation computes in. Metres from the origin
    // the scenario names.
    World,
    // Camera-relative: the world rotated and translated so the camera is at
    // the origin looking down -z. M1-11's camera defines it.
    View,
    // After the projection. Its w is metres, which is what makes the
    // perspective divide land in dimensionless coordinates.
    Clip,
};

// A frame, possibly dualised.
//
// **Why a struct and not just the enum**: the transpose of a matrix is not
// another matrix between the same two frames -- if M maps a to b, then M^T
// maps b* to a*, the dual (covector) spaces. Carrying that in the type is
// what makes `transpose(A B) == transpose(B) transpose(A)` typecheck in
// general rather than only for affine matrices, because both sides then agree
// on which dual space they live in. Dualising twice is the identity, so
// `transpose(transpose(M))` comes back to M's own type.
//
// Structural -- public members, literal type -- so it can be a non-type
// template parameter. Measured on clang 23.1.0, gcc-14 and MSVC 19.51 before
// the design was chosen.
struct FrameTag {
    Frame frame{};
    bool dual{};
};

// Comparison at namespace scope rather than as a defaulted member, and the
// reason is worth knowing because it is not obvious.
// `misc-non-private-member-variables-in-classes` ignores a class whose member
// variables are all public -- but only while the class declares no member
// *function*. Measured 2026-09-20 on a three-struct probe: the same two public
// members are reported when `operator==` is a defaulted member and silent when
// it is a free function. FrameTag's members have to stay public, because a
// non-type template parameter must be a structural type, so the comparison is
// what moves.
[[nodiscard]] constexpr bool operator==(const FrameTag& left, const FrameTag& right) noexcept {
    return left.frame == right.frame && left.dual == right.dual;
}

[[nodiscard]] constexpr FrameTag dualOf(FrameTag tag) noexcept {
    return {.frame = tag.frame, .dual = !tag.dual};
}

inline constexpr FrameTag kWorld{.frame = Frame::World, .dual = false};
inline constexpr FrameTag kView{.frame = Frame::View, .dual = false};
inline constexpr FrameTag kClip{.frame = Frame::Clip, .dual = false};

static_assert(dualOf(dualOf(kWorld)) == kWorld, "dualising twice is the identity");
static_assert(dualOf(kWorld) != kWorld, "and once is not");
static_assert(kWorld != kView, "two frames are two frames");

} // namespace orb::view

#endif // ORBSIM_VIEW_FRAME_HPP
