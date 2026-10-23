# Foundation-first layered rock

`rockformation.cl` is the new standalone Houdini COP OpenCL snippet. It writes a
float layer named `height`. It does not modify `layeredformation.cl`,
`layeredrock3d.cl`, existing Houdini nodes, or Unreal runtime generators.

Status: source implementation and static mathematical review only. No compilation,
render, performance measurement, or visual match to the references is claimed.

## Construction

1. Broad piecewise-planar formation fields create a continuous rock mass.
2. Ordered shared boundaries define physical beds in `W = p.y + fold + depth_beds*z`.
3. Hardness varies along and between beds, driving differential exposure.
4. Bedding wedges, local chips, shallow spalls and cross-joints remove material.
5. Fault displacement moves the entire mass and its beds together.
6. Analytic vertical-ray intersections return the first surviving surface.

There are no independently scattered Voronoi prisms and no flat bed fallback.
Shared-boundary construction follows `Geology/StratigraphicCoordinates.clh`;
tile-safe lattice hashing and ray clipping adapt the earlier COP helpers.
The source is self-contained so it can be used in a COP code parameter.

## Removal and source-level findings

Cellular relief and grain have been removed, including their bindings, parameter
checks, helpers, evaluation and cellular search-bound expansion. This includes
the cellular metric and ridge controls. Other features and their defaults are
unchanged so this pass isolates that removal; it is not a claimed visual fix.

The remaining source has three structural issues worth isolating:

1. **Triangle-grid normals:** `rf_noise(..., angular=1)` interpolates each grid
   cell as two planes. Foundation, additional facets and folds all use it.
   Height is continuous at grid boundaries, but its gradient changes abruptly;
   the resulting normal image therefore exposes the interpolation triangles.
   These planes come from a sampling grid, not geological fracture geometry.
2. **Spall discontinuities:** the spall noise seed contains `floor(p.x)`.
   Crossing an integer along-bed coordinate abruptly replaces the entire noise
   field. With active spalls this can create seams unrelated to the actual,
   jagged joint position. This source behavior has not been changed in this pass.
3. **Depth scale versus bed spacing:** nominal vertical bed spacing is
   `1/depth_beds`, about 0.071 at the default 14. A 0.060 bedding recess reaches
   nearly that entire spacing; differential erosion is 0.075. Also, the visible
   bed phase includes `depth_beds * foundation`, so the default 0.20 foundation
   amplitude contributes up to 2.8 bed intervals before additional facets.
   Small formation changes can therefore expose different beds and repeated
   ledges. This is parameter/representation coupling, not a grain problem.

These are source-level findings. Their individual contribution to the supplied
normal-map preview has not been isolated in Houdini; its normal strength and
current parameter values were not inspected. A normal image alone does not
establish the actual displaced depth.

For a foundation-only comparison using existing controls, temporarily set
`differential_erosion`, `bedding_recess`, `joint_depth`, `cross_depth` and
`fault_throw` to zero, and disable chips and spalls. The remaining bed intervals
unite to output the foundation directly. No preset or live node was changed.

## Depth and tiling

Output is raw signed height times `relief`: no 0–1 normalization or global depth
floor. The search bound follows the authored maximum displacements and removals;
it is never substituted for an actual hit. A single height output still cannot
retain hidden undersides, caves, or multiple surfaces at one pixel.

Lattice rows `(joint_count, joint_turns)` and `(diagonal_turns, layer_count)` must
be independent. Integer winding preserves UV tile translations and bed IDs.
Physical domain is a unit square; non-square image resolution does not change
its aspect ratio.

`thickness_variation + edge_jaggedness` must be below 0.45 to keep boundaries
ordered. `depth_beds` and all width controls must be positive; removal depths
must be nonnegative. Coverage controls are 0–1. The source checks count limits
before evaluation; invalid supported-domain inputs output NaN rather than
silently changing geometry. Keep all authored scalar values finite and material
coordinates near the origin. Detail-frequency fading does not fully antialias
hard silhouettes or discontinuities.

## Use

Existing Houdini nodes may retain stale cellular/grain spare parameters until
their bindings are refreshed; this kernel no longer reads them. Other authored
node values are not reset by this source change. The main structural controls
remain `formation_height`, `formation_facets`, `depth_beds`,
`differential_erosion`, `bedding_recess`, and `joint_depth`.
