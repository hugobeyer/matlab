I’ll research the geological-field and meshing approaches first, then outline the design before coding.


## Research findings

**Terminology:** implicit geological modelling in **curvilinear material coordinates**.
`GeologyUVW` describes the rock’s internal structure—not texture UVs.

One geological distinction: slate’s **cleavage/foliation** can differ from sedimentary bedding.
The first prototype will model bedding-aligned plates; independent cleavage can follow.

| Approach / source | Meaning and usefulness here | Decision |
|---|---|---|
| [SideFX Strata](https://www.sidefx.com/docs/houdini/heightfields_cop/strata.html) | Documented as modified-noise terrain layering, with tilt and base-height distortion. | Reference only; not our volumetric foundation. |
| [LoopStructural](https://gmd.copernicus.org/articles/14/3915/2021/) | Geological horizons are isosurfaces of continuous 3D scalar fields. | **Use this foundation.** |
| [Mallet’s geological space](https://doi.org/10.1023/B:MATG.0000016228.75495.7c) | Transformed depositional coordinates separate geology from its deformation. | Use the concept; accessible abstract, not a reproduced solver. |
| [LANL dfnWorks](https://dfnworks.lanl.gov/) | Discrete fractures are finite, intersecting 3D planar polygons. | **Use bounded joint patches**, not endless cuts. |
| [CGAL regular triangulations](https://doc.cgal.org/latest/Triangulation_3/index.html) | Power cells use distance² minus weight. Weights control size, not anisotropy. | Optional partitioning; not primary bedding. |
| [BSP / half-spaces](https://commons.apache.org/proper/commons-geometry/tutorials/bsp-tree.html) | Recursive planes partition regions and retain planar faces. | Useful for facets; naïve cuts overextend fractures. |
| [GPU Marching Cubes](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu) | Converts sampled scalar fields to triangles using local cell cases. | Good baseline; source is GPU/DX10, not OpenCL. |
| [Surface Nets](https://www.merl.com/publications/TR99-24) | A dual surface follows occupied-cell boundaries with constrained relaxation. | Simple variants suit GPUs; sharp creases are not guaranteed. |
| [Dual Contouring](https://www.cs.rice.edu/~jwarren/research/index.html) | Hermite intersections/normals and QEF fitting can preserve corners. | Best later sharp-feature candidate; more numerical/topology work. |
| [COP OpenCL](https://www.sidefx.com/docs/houdini/nodes/cop/opencl.html) | Supports actual volumes/VDBs and volume-driven execution. | **Use real 3D bindings**, not a slice atlas. |

### Recommended construction

- **Anisotropic power cells:** useful sizes/facets, but easily produce packed stones.
- **Randomized coordinate grids:** cheap, but risk masonry and repeated through-joints.
- **BSP:** sharp and controllable, but often splits more rock than intended.
- **Seeded finite fracture patches:** best control of spacing, persistence and termination.
- **Recommendation:** shared bedding slabs + bounded joint patches + restrained facet cuts.

### Field and coordinate representation

Use `F(P) < 0` for solid, `0` for surface, `> 0` for air.
Call it a **signed implicit field**, not an exact SDF after deformation/Booleans.

Construct an invertible world-to-material map from smooth shear stages, for example:

```text
a = x + A sin(kz z + ky y)
b = z + B sin(ka a)
c = [y − slope·a − C sin(ku a) cos(kv b)] / compression
U = a
V = b
W = stratigraphic_remap(c, U, V)
```

This changes all three coordinates through depth. Successive invertible shears avoid
inventing an arbitrary frame that cannot integrate into consistent coordinates.

Derive the frame from the coordinate Jacobian:

```text
N  = normalize(∇W)
T0 = normalize(∇V × ∇W)   // increasing-U tangent, orient consistently
T1 = N × T0
```

Do **not** define coordinates as `dot(P, spatiallyVaryingTangent)`.

### Strata, fractures and pieces

**Strata:** build ordered, shared boundaries `b_i(U,V)`, with positive thickness.
Between boundaries:

```text
W = i + (c − b_i) / (b_(i+1) − b_i)
layerId = floor(W)
layerPhase = W − floor(W)
```

This keeps `W` continuous and prevents independently randomized layers crossing.
Gradient-scaled boundary functions give approximate local distances.

**Joints:** intersect a thin plane slab with a finite elliptical/polygonal support.
Support spans a seeded range of `W` and `V`; taper aperture near its ends.
A single seed therefore persists across several beds without resetting per layer.

**Pieces:** retain parent-layer data; never randomize each fragment’s bedding.
Important: a finite internal crack may leave rock connected around its tip.
Plane-sign hashes are **region labels**, not necessarily real piece IDs.
Actual `pieceId` requires connected-component labelling within each parent layer.

**Delamination/chips:** boundary-local openings in `U/V`; thickness-limited 3D cuts.
Use `depth = thickness × ratio × (1 − exp(−strength))`, not a height subtraction.

### Meshing and first implementation

Keep field generation separate from extraction; begin with Houdini volume meshing.
Later compare uniform-grid DC against MC; avoid an adaptive GPU mesher initially.

Stage 1: modular OpenCL coordinate helpers, `128³`, UVW/frame diagnostics, XYZ slices.
Stage 2: shared variable-thickness boundaries, signed slabs, IDs/phases, cutaway mesh.
No structural FBM. Thin gaps must span several voxels to validate them reliably.

I’ll now build these first two stages in an isolated COP prototype, leaving Mixtormat untouched.
