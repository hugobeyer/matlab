# Material Lab Texture Source Layout

Material Lab source PNGs must stay inside the plugin so the shipped Fab package is self-contained.

## Surface textures

Place each surface family in its own `Source` folder:

```text
Plugins/MaterialLab/Content/Textures/<Family>/Source/*.png
```

Recommended families:

```text
Plugins/MaterialLab/Content/Textures/Metal/Source
Plugins/MaterialLab/Content/Textures/Rust/Source
Plugins/MaterialLab/Content/Textures/Dust/Source
Plugins/MaterialLab/Content/Textures/Wood/Source
Plugins/MaterialLab/Content/Textures/Paint/Source
Plugins/MaterialLab/Content/Textures/Fabric/Source
```

Each surface requires three PNGs with the same base name:

```text
TX_<Family>_<Subtype>_<Finish>_<Structure>_<Variant>_BC.png
TX_<Family>_<Subtype>_<Finish>_<Structure>_<Variant>_N.png
TX_<Family>_<Subtype>_<Finish>_<Structure>_<Variant>_RAM.png
# or, when authored blend height is available:
TX_<Family>_<Subtype>_<Finish>_<Structure>_<Variant>_RAMH.png
```

Example metal set:

```text
Plugins/MaterialLab/Content/Textures/Metal/Source/
├─ TX_Metal_Brass_Polished_Plain_01_BC.png
├─ TX_Metal_Brass_Polished_Plain_01_N.png
└─ TX_Metal_Brass_Polished_Plain_01_RAM.png
```

Texture channel contract:

```text
_BC     RGB = Base Color                         sRGB
_N      RGB = Tangent-space Normal               Linear / Normal compression
_RAM    R = Roughness, G = AO, B = Metallic                     Linear / Masks compression
_RAMH   R = Roughness, G = AO, B = Metallic, A = Blend Height   Linear / Masks compression
```

Use `_RAMH` only when alpha contains authored blend height. Legacy `_RAM` alpha is never
interpreted as height. Source now derives height when RAMH is unavailable; build and editor validation are pending.

The importer behavior is documented in `NormalDerivedHeightPlan.md`: `_RAM` sets receive an
importer-owned RAMH texture reconstructed from the normal map through FFT Poisson integration.
Authored `_RAMH` always wins. The ordered combined child mask remains the placement input to the
height-blend kernel.

Do not add `_MSK` files to surface sets. Material Lab derives cavity and convexity from normals.

## Mask textures

Place every reusable mask, including stain masks, directly in one source folder:

```text
Plugins/MaterialLab/Content/Textures/Masks/Source/*.png
```

Do not create category or Grunge subfolders. Use descriptive filenames instead:

```text
Plugins/MaterialLab/Content/Textures/Masks/Source/
├─ TX_Mask_Stain_Pooled_01.png
├─ TX_Mask_Stain_Streaked_01.png
├─ TX_Mask_Scratches_Fine_01.png
└─ TX_Mask_Pits_01.png
```

Masks are single PNG textures. They do not use `_BC`, `_N`, or `_RAM` suffixes.
Export masks as lossless 8-bit grayscale PNG data in Raw/Linear space, never sRGB.
The importer uses linear mask compression.

## Generated plugin assets

Do not manually place generated `.uasset` files in the source folders.

After **Reimport Shipped Library**, Material Lab generates and saves assets under:

```text
/MaterialLab/Textures/<Family>/Raw
/MaterialLab/Surfaces/<Family>
/MaterialLab/Materials/Instances/<Family>
/MaterialLab/Masks
```

These Unreal paths map to `Plugins/MaterialLab/Content`.

## Important rules

- Use PNG source files.
- All importer-owned and baked textures are marked `Never Stream`.
- Keep shipped sources under `Plugins/MaterialLab`.
- Keep all three surface maps together in one family `Source` folder.
- Use either `_RAM` or `_RAMH` per surface set, not both.
- Keep every mask PNG directly under `Textures/Masks/Source`.
- Do not use `Handoff` as an automatic shipping source.
- Do not modify the protected master material from C++.
