# Material Lab — Normal-Derived Height Plan

Status: C++ build succeeded; global-shader, import, automation, and visual validation pending.

## Goal

When a complete surface ships with `_BC`, `_N`, and `_RAM` but no authored `_RAMH`,
Material Lab should reconstruct a usable spatial height field from the tangent-space normal map.
This is an editor import/reimport operation. It must not run during preview, baking, or runtime.

The result is an approximation for materials where authored height is unavailable. Authored
`_RAMH.A` remains authoritative whenever it exists.

## User experience

- No button, inspector control, or per-layer setup.
- **Reimport Shipped Library** performs reconstruction automatically for complete `_RAM` sets.
- Existing `_RAMH` sets bypass reconstruction completely.
- Preview, compositing, baking, and displacement consume authored and derived height identically.
- The inspector may report `Derived from Normal` for provenance, but exposes no generation control.

## Source precedence

```text
Authored _RAMH exists
    → use _RAMH.A unchanged

Only _RAM exists and _N is valid
    → reconstruct Height from _N
    → generate importer-owned RAMH texture

Normal is flat
    → generated Height is neutral 0.5

Reconstruction fails or input dimensions are unsupported
    → report the exact surface and reason
    → do not claim the surface has spatial height
```

Never reinterpret legacy `_RAM.A` as height. Never overwrite source PNG files.

## Asset and metadata contract

Keep `UMaterialLabSurface::bHasBlendHeight` for compatibility. Add explicit append-only provenance:

```text
None
DerivedFromNormal
AuthoredRAMH
```

`bHasBlendHeight` is true for `DerivedFromNormal` and `AuthoredRAMH`.
The provenance value decides inspector wording and prevents derived data from being described as authored.

For a `_RAM` set, create or update an importer-owned packed texture beside the raw imports:

```text
/MaterialLab/Textures/<Family>/Raw/TX_<Identity>_RAMH_Derived
```

Generated channel layout:

```text
R = original RAM roughness
G = original RAM AO
B = original RAM metallic
A = normalized reconstructed height
```

The generated texture must preserve source resolution, remain linear, use masks compression,
retain alpha, and be marked `Never Stream`. Reimport updates the same generated asset in place.
If authored `_RAMH` is later added, the surface switches to it. Do not automatically delete the
old generated asset.

## Reconstruction algorithm

### 1. Decode tangent-space normals

Read the imported normal source in linear space and decode:

```text
N = NormalRGB * 2 - 1
```

Apply the importer-defined normal-Y convention before integration. Use one shared convention so
normal shading and reconstructed height do not disagree.

Convert the normal to an image-space gradient:

```text
if abs(N.z) <= epsilon:
    dHdx = 0
    dHdy = 0
else:
    dHdx = -N.x / N.z
    dHdy = -N.y / N.z
```

Flat or invalid normals produce zero gradient. Clamp pathological grazing values before the FFT.

### 2. Forward 2D FFT

Run a forward FFT independently for `dHdx` and `dHdy`, producing complex spectra:

```text
Gx(k) = FFT(dHdx)
Gy(k) = FFT(dHdy)
```

The initial implementation targets the shipped square power-of-two source sizes: 1024, 2048,
and 4096. Unsupported dimensions must report an exact import error rather than silently resizing.

### 3. Frequency-domain Poisson integration

For normalized signed frequency `k = (kx, ky)`:

```text
H(k) = -i * (kx * Gx(k) + ky * Gy(k))
       / (2π * (kx² + ky²))
```

Set the zero-frequency/DC coefficient to zero. The solver assumes periodic texture boundaries,
which matches tiling Material Lab surfaces and avoids introducing edge padding.

### 4. Inverse 2D FFT

```text
HeightRaw = real(IFFT(H))
```

Discard residual imaginary error after validating it remains within numerical tolerance.

### 5. Deterministic normalization

Normal integration cannot recover absolute scale or DC height. Normalize symmetrically around the
neutral displacement center:

```text
Centered = HeightRaw - median(HeightRaw)
Extent = percentile99(abs(Centered))
Height = Extent > epsilon
    ? saturate(0.5 + 0.5 * Centered / Extent)
    : 0.5
```

This keeps a flat normal neutral, reduces sensitivity to isolated spikes, and preserves valleys
below `0.5` and ridges above `0.5`. The viewport `ML_HeightAmount` parameter controls visual
amplitude later; reconstruction does not bake a user-facing amount.

## Unreal implementation direction

- Implemented as editor-only Unreal global compute shaders/HLSL.
- Use RDG textures/buffers and explicit forward/inverse FFT passes.
- Read back through an R16F UAV target supported by the existing compositor path.
- Perform reconstruction only in `MaterialLabSurfaceImporter` during import/reimport.
- Read back or copy the normalized result only when creating/updating the generated texture asset.
- Cache the generated asset; never run FFT work per layer, preview refresh, bake, or runtime frame.
- Do not add an external package or modify `M_MaterialLab_Substrate`.
- Preserve shared 1K/2K/4K composition resolution with 2K default.
- Do not add fallback asset destinations.

## Limitations

- Reconstructed height is not authored physical height.
- Absolute scale and broad low-frequency shape cannot be recovered from normals.
- Compression artifacts and noisy normals can create ringing or relief noise.
- Periodic FFT integration is best for tileable surfaces.
- The result is suitable for scratches, grain, pits, dents, and material microrelief.
- Authored `_RAMH` should replace derived height when physically meaningful displacement matters.

## Validation

Add deterministic editor tests before enabling the importer path:

1. Flat normal produces exactly neutral `0.5` height.
2. A known analytic periodic height reconstructs within tolerance.
3. Normal-Y orientation produces the expected ridge/valley direction.
4. Generated RGB matches the original RAM source pixels.
5. Generated alpha is non-uniform for a non-flat normal.
6. Authored `_RAMH` always wins over generated height.
7. Reimport updates the same generated asset path.
8. Unsupported dimensions report the exact surface and resolution.
9. Preview and bake consume the generated alpha as layer height.
10. Final baked `RAM.A` remains dielectric F0; displacement remains `T_<Name>_H`.

Do not run builds, Unreal, or tests without explicit permission.
