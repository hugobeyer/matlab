# Layered rock: analytical depth prototype

`layeredrock3d.cl` is a standalone Houdini COP OpenCL kernel with one writable
float `height` layer. It does not replace `layeredformation.cl` or register an
Unreal generator. Bind an output image as for the original prototype.

## Shape model

- An integer bedding lattice generates elongated, irregular Voronoi footprints.
- Each footprint becomes a finite-depth prism with tilted planar cleavage faces.
- Vertical rays are clipped against side, top, bottom, bevel and removal bounds.
- Independent buried tiers supply visible rock when an upper slab is absent.
- Chips remove edge wedges; spalls remove nested thin laminae from the faces.
- Cross-cutting joints traverse several beds instead of following every cell edge.
- Fine surface grain is separate from large-scale slab presence and termination.

The result is the highest surviving surface along +Z, not a general-purpose 3D
mesh or SDF. XY-dependent surface cuts are evaluated along that vertical ray.
A single height channel cannot retain overhangs, hidden undersides or multiple
intersections. The kernel does not bake color, mineral veins, lighting or AO.

Valid hits are written without a global depth floor or 0–1 normalization;
negative heights are allowed. The bed fills only pixels with no surviving slab
hit. Previously, taking `max(bed, hit)` incorrectly flattened valid deeper rock
to the bed height. Half-space clipping still defines the finite slab geometry;
it is not an output-range clamp. A cut through a slab's bottom removes that slab
at the pixel, exposing another tier or the uncovered-pixel bed value.

## Correction after the first visual preview

The initial continuous sinusoidal coordinate warp bent both the outlines and
face planes, producing melted-looking slabs. Folding now moves the cell seeds;
faces and fracture planes remain rigid. Neighbor-search bounds include those
seed displacements. This intentionally changes the meaning of `fold_strength`
from continuous surface deformation to folded slab placement.

Grain originally started at frequencies that were heavily filtered at preview
resolutions. The new defaults start lower, strengthen angular facets and chips,
and reduce large empty gaps. Spalls, chips and edge noise now also fade when
unresolved. Hash arithmetic uses 64-bit intermediates to avoid overflow at high
noise frequencies. These changes have been source-reviewed, not render-verified.

## Existing Houdini nodes

Changing `#bind` defaults does not guarantee that an existing node's authored
parameter values change. Reset the relevant controls to the new source defaults
when comparing the revision. Important changes:

| Control | New default | Purpose |
|---|---:|---|
| `fold_strength` | 0.06 | Small, rigid bedding displacement in cell units |
| `depth_spacing` | 0.10 | Less excessive separation between exposed tiers |
| `height_variation` | 0.08 | Keep neighboring masses related |
| `face_tilt` | 0.35 | Broad cleavage planes rather than steep ramps |
| `missing_blocks` | 0.04 | Occasional missing mass, not pervasive holes |
| `facet_strength` | 0.08 | Intersecting angular face cuts |
| `grain_cells` | 8 | Surface detail visible at typical preview resolutions |
| `grain_height` | 0.003 | Small relief, not large-scale shape noise |
| `edge_jag_cells` | 5 | New control for resolvable boundary damage |
| `chip_density` | 0.55 | More frequent local edge events |
| `chip_width` / `chip_depth` | 0.022 / 0.045 | Local wedge removal |
| `chip_cells` | 4 | Larger, readable edge chips |
| `spall_coverage` / `spall_depth` | 0.55 / 0.025 | Thin nested face removals |
| `spall_cells` / `spall_bevel` | 2 / 0.02 | Broader flakes with narrower lips |

## Coordinates and limits

Lattice rows are `a = (joint_count, joint_turns)` and
`b = (diagonal_turns, layer_count)`. Their determinant must be nonzero;
a singular lattice deliberately outputs zero. Defaults `(3, 1)` and `(-3, 7)`
produce diagonal bedding. Counts are integer periodic modes, not degrees.

Tilting the lattice preserves periodicity through its integer translation
lattice; independently wrapping rotated X/Y coordinates would not. Geometry
uses a unit-square material domain, even for a non-square pixel resolution.
Widths and depths are in tile-width units unless stated otherwise.

Use small folding values (roughly 0–0.25) for fractured rock. Large values
increase neighbor-search cost substantially. `depth_layers` is bounded to 1–8;
start with the default 3 at preview resolution. Noise rates are bounded to 1–64,
grain octaves to 1–6 and spall layers to 1–8. Fine features below a pixel cannot
be recovered by raising their frequency. Filtering the detail does not fully
antialias discontinuous slab silhouettes.

This is an artistic geological approximation, not a fracture or erosion
simulation. The supplied reference images are visual targets, not a verified
match to the current output.
