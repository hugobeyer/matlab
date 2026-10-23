# Scatter Carve — minimal Houdini COP OpenCL prototype

One node, analytic finite-cutter geometry projection, subtract or union with a chamfer Boolean.
The Boolean Ops generator in Mixtormat ports this kernel to Unreal as Scatter Carve.
No mesh input, combined-surface SDF rebuild, or simulation.
Uses the same `#runover layer` / named-layer bindings as `../Behaviorsopencls/`.

## Setup

1. Paste `scatter_carve.cl` into an OpenCL COP and generate its bindings.
2. Connect a scalar `height` layer. Start with constant **0** or existing relief.
3. Optionally connect a scalar `mask`; unbound means full effect.
4. View `geometry_front` with `geometry_mask` to see the projected seeds alone.
5. View `height_out` for the combined surface; `cut_depth` is positive for removal.
6. For an obvious first scoop: height **0**, count **1**, radius **0.2**,
   radius_variation **0**, center_depth **0**, depth_variation **0**,
   depth_scale **2**, operation **0**. The center floor is approximately **-0.4**
   (pixel sampling may not land exactly on the seed center).
7. Drag `center_depth` to move the geometry through the surface. Use operation
   **1** to project the union instead: the same ellipsoid rises to about **+0.4**.

`size_ref` is optional, matching the neighboring prototypes. Use matching input
and output resolutions. View signed height without clamping negatives away.

## Controls

| Parameter | Meaning |
|---|---|
| `seed` | Repeatable scatter; changing other controls keeps seed positions fixed. |
| `count` | Number of ellipsoid seeds, 0–512; increasing it preserves existing seeds. |
| `radius` | Maximum radius, in normalized material-space units. |
| `radius_variation` | 0 = equal radii; 1 = random radii between zero and `radius`. |
| `center_depth` | Positive = inside the sampled surface plus offset; negative = in front. |
| `depth_variation` | Symmetric random center offset; magnitude in height units. |
| `reference_height` | Extra height offset added to the surface sampled at each seed center. |
| `depth_scale` | Normal-axis radius multiplier. Increase to make geometry deeper/taller. |
| `aspect` | V-axis radius multiplier; U radius remains unchanged. |
| `operation` | 0 = subtract geometry; 1 = union/project geometry over the target. |

With `aspect=1` and `depth_scale=1`, the geometry is spherical. All three
radii vary together per seed; changing depth or aspect does not move seed centers.
Input height can use **[-1, 1]**, but neither input nor output is normalized or
clamped by the kernel. `center_depth` and `reference_height` are in height units.
There is no iso parameter. Each seed is anchored at `height(seedUV)` plus
`reference_height - center_depth` and its random depth offset. Height is sampled
at the seed center, not separately across its footprint, so the ellipsoid stays
rigid rather than bending to match the relief. Axes remain material U/V/depth;
seeds are not rotated to the local slope.

U and V each span one material unit. Height is displacement along the material
normal, in those same units: **not world up**. Square material-domain metric;
non-square physical domains are not aspect-corrected in this first prototype.
Scatter wraps at U/V boundaries. Seamless output also requires seamless inputs.

## What the subtraction does

The input defines a solid `z <= height(u,v)`, extending indefinitely inward.
Each ellipsoid intersects a pixel's normal ray in a depth interval. The kernel
subtracts their union and resolves the frontmost remaining surface analytically;
there is no marching step size and no ordinary height-stamp subtraction.
Overlapping intervals are revisited until no further surface can be removed.

An ellipsoid entirely buried beneath an intact surface produces **no visible cut**.
An ellipsoid entirely in front also leaves it unchanged in subtraction mode.
Move it through the surface to open a scoop. Its curved floor is not a rounded
Boolean edge blend: the rim is still a hard intersection.

`geometry_front` is the frontmost ellipsoid hit, `geometry_back` the rearmost,
and `geometry_mask` indicates whether the ray hits any seed. Outside that mask,
both depths are zero (not a surface hit). These outputs use the target height
for seed anchoring but ignore target clipping and masking, showing the geometry itself.
At overlaps, front/back are the outer envelope; they do not encode internal gaps.
Subtraction uses every seed's interval internally, not that simplified envelope.

A rough target can produce irregular intersection rims even though the cutters
remain ellipsoids. Inspect `geometry_front` to distinguish geometry shape from
its intersection with the target. Fractional masks also alter the final profile.

Only visible height survives the resolve. Hidden cavities and overhangs cannot
be stored in this output or carried into a later node. A partial mask blends
original and resolved heights, not cutter volume. No IDs, coverage, or boundary
SDF are produced for the combined surface; `geometry_mask` is only the seed
footprint. Do not treat upstream geometry companions as updated outputs.

## Deliberately deferred

- Rotation, noise distortion, chamfer and rounded edge blends.
- Intersection mode, arbitrary mesh projection, gravity, slope gates, cached scatter passes.

Cost: ellipsoid setup is linear in seed count per pixel; overlap resolution is
quadratic in the worst case. The fixed 512-interval private arrays can also cost
GPU occupancy. Start at 16 seeds and modest resolution; realtime dragging is
**not measured or guaranteed**. This favors a small, clear concept over tuning.

Source reviewed only; not compiled or executed in Houdini.
