# M1-56 — Displacement and normals

Phase: C | Status: not started
Prerequisites: M1-51, M1-55
Decided by: [ADR 0015](../../adr/0015-skirts-and-morphing.md)

## Purpose

Where the elevation data becomes relief. Mountains get height, and — more
visibly from orbit — surfaces get normals that vary, so the terminator grows
long shadows and terrain stops looking painted on.

## What to implement

- **Vertex displacement** along the geodetic normal from M1-49, by the height
  sampled from the elevation tile. Not along the geocentric direction: the two
  differ by up to 11.5 arcminutes, and using the wrong one tilts every mountain
  slightly toward the equator.
- **No vertical exaggeration.** Real scale, stated in the header. Earth's relief
  is 0.3 % of its radius and looks it; exaggerating is a decision nobody took.
- **Normals from the height field**, computed by central differences using the
  259 × 259 tile's border cells, so a tile's normals are correct at its edges
  without its neighbours being resident. That is what the border exists for.
- **The morph target must displace too.** A vertex morphing between levels
  interpolates its *displaced* parent position, using the parent tile's
  elevation sample — otherwise the terrain pops in height exactly where M1-52
  stopped it popping in shape. This is the subtle part of the task and it is the
  reason displacement lands after morphing rather than before.
- **Skirt depth is revisited**, as M1-51 promised: it must now cover the worst
  height difference across a tile edge as well as the geometric sag. Derived
  from the tile's own height range, which the generator can record per tile.

## Out of scope

Shadow casting between terrain features — long shadows at the terminator here
come from the normals, not from ray casting. Tessellation shaders. Elevation for
any body but Earth.

## Tests

Headless first:

- **The budget: a displaced vertex's distance from the ellipsoid centre equals
  the ellipsoid radius plus the sampled height, within 1 m**, across a seeded
  sweep of positions — the end-to-end form of M1-55's budget, and the one that
  catches a normal-direction error.
- **Normals against finite differences** computed independently in the test from
  the source grid, to 1e-6 in direction, including at tile edges where the
  border cells are used.
- **Normals are continuous across a tile boundary**: the normal computed from
  two adjacent tiles at the same point on their shared edge agrees to 1e-9. A
  mismatch is a visible crease along every seam under raking light, which is
  exactly the lighting phase C's acceptance criterion asks for.
- **Morph consistency**: at morph factor 1, a displaced child vertex equals the
  displaced parent's interpolated position to 1e-9 m.
- **Flat is flat**: a constant height field produces vertices on an offset
  ellipsoid, and normals identical to the geodetic normals. A subtle sign error
  in the differencing survives everything else and fails this.

On the GPU:

- **Probe `terrain-terminator`**, with a golden: mountainous terrain at a low
  sun angle, at 10 km altitude.

## Frames to look at

`terrain-terminator.png`. Judge: ridges cast their shading the way the sun
direction says; the relief looks like the right scale rather than exaggerated or
absent; no creases along tile boundaries; no terracing from quantisation. This
is the frame phase C's criterion — *"mountains cast visible shadows at the
terminator"* — is actually about.

## Done when

- [ ] `check` green in both trees.
- [ ] The 1 m displacement budget holds end to end.
- [ ] Normals are continuous across seams, asserted numerically.
- [ ] Morph targets are displaced, and the factor-1 identity still holds.
- [ ] `terrain-terminator.png` approved and committed.
