# Volumetric geology: OpenCL stages 1–2

Standalone research prototype. No Mixtormat registration, shader integration, Houdini
scene edits, dependencies, host runner, or unit tests. These are portable OpenCL C 1.2
kernels, not drop-in Houdini `@KERNEL` snippets. A COP adapter will need explicit 3D
volume bindings; Houdini volume strides must not be assumed to match these buffers.

**Status:** source implementation only. Not compiled, dispatched, visually validated,
or meshed yet. The design has not yet passed the requested visual acceptance gates.

## Files

| File | Responsibility |
| --- | --- |
| `GeologyMath.clh` | Deterministic hashing, XYZ indexing, parameter checks, box distance |
| `GeologyCoordinates.clh` | Invertible world-to-material deformation and analytic gradients |
| `StratigraphicCoordinates.clh` | Shared ordered boundaries, continuous W, bedding frame |
| `StrataField.clh` | Volumetric slab occupancy and boundary-distance estimates |
| `VolumeCombine.clh` | Signed Boolean intersection with block and optional cutaway |
| `Stage1Coordinates.cl` | 3D UVW/frame diagnostic kernel |
| `Stage2Strata.cl` | 3D signed-solid, layer ID, phase and thickness kernel |
| `DiagnosticSlices.cl` | Orthogonal views sampled from existing 3D buffers |

Build each `.cl` translation unit with this directory on the OpenCL include path.
Do not enable relaxed/finite-only math: invalid inputs deliberately produce NaNs.

## Representation

- `density < 0`: solid; `density = 0`: surface; `density > 0`: air.
- Despite its output name, `density` is a signed implicit field, not smoke density.
- Gradient-corrected distances are first-order estimates, **not an exact SDF**.
- Never sphere-trace this field as if it had a guaranteed unit Lipschitz bound.
- All construction samples XYZ. There is no heightmap input or primary height output.
- Coordinates/IDs extend through air; use `density < 0` when selecting solid material.
- V is a genuine second material coordinate, initially aligned with **negative Z**.
- Positive Y is the undeformed bedding normal. The resulting frame is right-handed.

## Buffer and dispatch contract

Dispatch field kernels with a **three-dimensional NDRange** `(Nx, Ny, Nz)`.
Padded global sizes are supported: each kernel checks all three bounds.

```text
index = x + Nx * (y + Ny * z)
P = domain_min.xyz + (float3(x,y,z) + 0.5) * voxel_size.xyz
```

Use `size_t` allocation arithmetic. Every output needs `Nx * Ny * Nz` elements.
Kernel `uint4`/`float4` arguments occupy 16 bytes; slice `float2` ranges occupy 8.
All floats are 32-bit; ID buffers contain signed 32-bit integers. Buffer arguments
must be valid, sufficiently sized, and non-overlapping. `.w` of UVW/frame outputs
is reserved and zero; it is not a fourth geological coordinate.

Stage 1 writes four `float4` buffers (64 bytes/voxel):

- `geologyUVW`: `(U, V, W, 0)`.
- `beddingNormal`: `(N, 0)`.
- `beddingTangent`: `(T0, 0)`; recover `T1 = cross(N, T0)`.
- `coordinateDiagnostics`: `(H, localThickness, JacobianDeterminant, abs(dot(N,T0)))`.

Stage 2 writes five scalar buffers (20 bytes/voxel):

- `density`, `layerId`, `layerPhase`, `boundaryDistance`, `localThickness`.
- `boundaryDistance` is distance to the nearest **unopened bedding boundary**,
  not distance to the external block or its cutaway.
- `localThickness = 1 / length(gradient(W))` is the full bed's first-order
  normal thickness; it is not the remaining thickness after opening a gap.

Together the buffers need approximately 168 MiB at 128³, 567 MiB at 192³,
and 1344 MiB at 256³, excluding meshing, driver and display overhead.
Stage 2 can run without allocating Stage 1 outputs. Both kernels evaluate the
same shared coordinate code; they must receive identical material parameters.

## Initial parameter preset

Values are passed in the order declared in each kernel signature.

| Argument | Value | Meaning |
| --- | --- | --- |
| `resolution` | `(128,128,128,0)` | Voxel dimensions |
| `domain_min` | `(-1,-1,-1,0)` | Minimum corner of the sampled volume |
| `voxel_size` | `(0.015625,0.015625,0.015625,0)` | 2/128 per axis |
| `shear_fold` | `(0.12,0.10,0.16,2.40)` | Primary shear, depth shear, fold amplitude, U frequency |
| `bedding` | `(0.08,-0.04,1.00,1.90)` | U slope, V slope, positive H scale, V frequency |
| `thickness` | `(0.20,0.10,0.04,1.40)` | Nominal thickness, jitter fraction, wave fraction, wave frequency |
| `seed` | `1729` | Deterministic shared geology seed |
| `gap_fraction` | `0.25` | Diagnostic full aperture / nominal H thickness |
| `block` | `(0.86,0.80,0.85,0)` | Centered block half-extents; w unused |
| `cutaway` | `(0.05,-0.25,0.05,1)` | Remove x>0.05 AND y>-0.25 AND z>0.05; w enables |

Gaps are deliberately exaggerated for low-resolution inspection. The nominal H
aperture is 0.05 here; its world thickness varies with deformation. Measure its
narrowest visible sampling before accepting the result; keep at least 2–3 voxels
across gaps, and several across remaining rock. Increase resolution for thin slate.
This uniform inspection opening is **not Stage 5 delamination**.

Use `cutaway.w = 0` for the complete block. With `gap_fraction = 0`, adjacent beds
touch: their union is the complete block. The implementation removes interior
zero sheets in this mode so meshing does not invent internal air surfaces.

Flat preset: set the first three `shear_fold` components, both bedding slopes,
and `thickness.z` to zero. Set `thickness.y = 0` too for uniform thickness.
Keep `bedding.z = 1`. Smaller positive `bedding.z` compresses the strata.

Parameter restrictions:

- All used values must be finite; voxel sizes, nominal thickness, scale and block
  half-extents must be positive.
- `thickness.y,z >= 0`; their sum must be below 0.45.
- `thickness.w >= 0`.
- `0 <= gap_fraction < 1 - 2*(thickness.y + thickness.z)`.
- `abs(H / nominalThickness) <= 1,000,000`; use normalized domains near the origin.
- These are diagnostic-scale kernels, not numerically hardened for extreme warps.
- Invalid parameters write NaNs and ID `INT_MIN`, rather than silently clamping
  geometry. Slice colors expose non-finite values as magenta.

## Geological construction

`GeologyCoordinates` composes smooth invertible shears. Its material coordinates
are `(U,V,H)`; H is an implicit coordinate evaluated at XYZ, **not Height(X,Z)**.
U depends on Y and Z; V depends on U and Z; H then depends on U, V and Y.
The map's determinant is `1 / bedding.z`, so positive compression preserves
orientation without arbitrary, potentially non-integrable frame fields.

Each shared boundary has the form:

```text
b_i(U,V) = h * [i + jitter*r_i + wave*sin(f*U+a_i)*cos(0.83*f*V+c_i)]
```

`r_i` lies in [-1,1]. The same boundary ID always produces the same surface,
regardless of which neighbor queries it. Adjacent boundaries are separated by
at least `h * (1 - 2*(jitter+wave))`, preventing crossing or negative thickness.
The boundary table is procedural: there is no stored per-layer lookup buffer.

For the interval containing H:

```text
phase = (H - b_i) / (b_(i+1) - b_i)
W = i + phase
N = normalize(gradient(W))
T0 = normalize(cross(gradient(V), gradient(W)))
T1 = cross(N, T0)
```

W is continuous at every boundary; its derivative may change there because
neighboring beds have different thicknesses. Within intervals, analytic gradients
supply the frame and local metric. A smooth gradient across contacts would require
a different monotone remapping; it is not silently approximated here.

The scalar slab is negative between its lower/upper boundaries, after half an
opening is removed from each. `max` intersects the result with the exterior block;
`max(field, -cutter)` subtracts the optional three-plane corner cutaway.
This preserves broad surfaces without structural FBM or independent fragment warps.

## Slice inspection

The slice kernels have a 2D dispatch **only because they display already-generated
3D buffers**. They neither construct nor extrude the field.

| axis | Fixed coordinate | Image size | Horizontal / vertical |
| --- | --- | --- | --- |
| 0 | X | Nz × Ny | Z / Y |
| 1 | Y | Nx × Nz | X / Z |
| 2 | Z | Nx × Ny | X / Y |

`slice` is a voxel index, not a normalized position. Try 32, 64 and 96 on every
axis at 128³. Allocate width×height `float4` pixels per slice. Kernels use nearest
voxel values; in particular, no interpolation corrupts categorical layer IDs.

- `geology_slice_vector`: component 0/1/2 of `geologyUVW` displays U/V/W.
- Start with ranges `[-1.3,1.3]` for U/V and `[-7,7]` for W.
- Use component 2 of `coordinateDiagnostics` to inspect positive Jacobians.
- `geology_slice_scalar`: use `[-0.08,0.08]` for density; negative is blue.
- Phase range is `[0,1]`; thickness range can begin at `[0.05,0.30]`.
- `geology_slice_ids`: bind Stage 2 IDs and density; `mask_air=1` shows solid only.
- The ID viewer can later accept real piece IDs, but no piece IDs exist yet.

## Procedural acceptance gates (not yet executed)

1. Compare U/V/W at multiple depths in all three orientations; reject extrusion.
2. Confirm finite values, positive Jacobians and near-zero frame dot products.
3. Confirm W is continuous at shared boundaries and every bed has positive thickness.
4. Inspect layer IDs and phases together; neighboring IDs share one authoritative boundary.
5. Extract the zero surface from `density` without changing its sign convention.
6. Inspect the mesh from X/Y/Z and in the cutaway; reject unresolved gaps and false bridges.
7. Re-run at higher resolution before accepting thinner layers or topology changes.

Houdini volume/VDB meshing is the intended first extraction path. Preserve voxel
center transforms on import, keep the surface inside the sampled domain, and do
not mislabel the field as a metric VDB level set without distance rebuilding.
Field generation is deliberately independent of the mesher.

## Deferred stages

Proceed only after the Stage 1/2 visual gates:

3. `JointField`: finite plane patches with V/W support and tapered apertures;
   shared fracture seeds span multiple beds, then terminate.
4. `PiecePartition`: connectivity constrained by parent layers and actual cuts.
   A bounded crack does not automatically disconnect the rock around its tip.
   Plane-sign hashes must not masquerade as connected-piece IDs.
5. `Delamination`: boundary-local, spatially varying openings, not uniform gaps.
6. `FacetCuts` / `ChipCuts`: finite 3D cuts with depth smoothly bounded by local
   remaining plate thickness. No per-piece reorientation of parent geology.
7. Detail only after structural validation; generic FBM must not build the geology.

No `pieceId`, fracture, delamination or chip outputs are fabricated as placeholders.
This is not yet the final jointed slate block or rough-rock version.

## Research basis

- [SideFX Strata](https://www.sidefx.com/docs/houdini/heightfields_cop/strata.html):
  public description is modified-noise terrain layering, not this volume model.
- [LoopStructural](https://gmd.copernicus.org/articles/14/3915/2021/): geological
  scalar fields and structural coordinate systems.
- [Mallet, geological space](https://doi.org/10.1023/B:MATG.0000016228.75495.7c):
  depositional-space concept; only the public abstract was reviewed.
- [LANL dfnWorks](https://dfnworks.lanl.gov/): finite planar fracture networks.
- [CGAL regular triangulations](https://doc.cgal.org/latest/Triangulation_3/index.html):
  power cells. Scalar weights affect size, not directional anisotropy.
- [BSP half-spaces](https://commons.apache.org/proper/commons-geometry/tutorials/bsp-tree.html):
  cuts partition entire tree regions; this can overextend a physical fracture.
- [GPU Marching Cubes](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu):
  straightforward extraction baseline; implementation discussed there is not OpenCL.
- [Surface Nets](https://www.merl.com/publications/TR99-24): constrained dual surfaces;
  simple variants can round features and are not equivalent to the original method.
- [Dual Contouring](https://www.cs.rice.edu/~jwarren/research/index.html): Hermite/QEF
  fitting for sharper features, with additional numerical and topology complexity.
- [COP OpenCL](https://www.sidefx.com/docs/houdini/nodes/cop/opencl.html): supports
  actual volumes and VDBs; a future binding adapter must respect native storage.

Recommended later segmentation: shared bedding slabs plus finite seeded joint
patches. Anisotropic power cells are useful as optional partitions but can resemble
packed stones; randomized grids risk masonry; BSP retains facets but tends to
split too much. Slate cleavage can differ from bedding: this prototype currently
models bedding-aligned plates, not an independent metamorphic cleavage field.
