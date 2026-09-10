# Material Lab — Priority 2.4 Bake Validation Plan

Date: 2026-08-31

Do not run this plan without explicit permission to launch Unreal and run tests.

## Test matrix

Run the full matrix at 1024, 2048, and 4096 using one saved recipe and one fixed destination.

For each resolution:

1. Bake to new outputs.
2. Record all four object paths.
3. Keep the material instance assigned to a test actor.
4. Change a visible recipe value.
5. Use `Re-bake` from the result dialog.
6. Confirm the same assets update in place.
7. Undo and redo any actor assignment separately.

Expected output names:

```text
T_<Name>_BC
T_<Name>_N
T_<Name>_RAM
T_<Name>_H
MI_<Name>
```

## Acceptance checks

### Exact output resolution

- `Source.GetSizeX()` and `Source.GetSizeY()` equal the selected shared resolution.
- BC, Normal, RAM, and Height dimensions match each other.
- Expected dimensions are exactly 1024², 2048², or 4096².

### BC settings

- `SRGB == true`.
- `CompressionSettings == TC_Default`.
- `LODGroup == TEXTUREGROUP_World`.

### Normal settings

- `SRGB == false`.
- `CompressionSettings == TC_Normalmap`.
- `LODGroup == TEXTUREGROUP_WorldNormalMap`.

### RAM settings

- `SRGB == false`.
- `CompressionSettings == TC_Masks`.
- `LODGroup == TEXTUREGROUP_World`.

### Height settings and displacement data

- `SRGB == false`.
- Source format is 16-bit grayscale.
- `CompressionSettings == TC_Grayscale`.
- `LODGroup == TEXTUREGROUP_World`.
- A RAMH or Constant Height recipe produces matching normalized source pixels.
- A recipe without authored height produces the neutral `0.5` field.
- Height remains separate from RAM.A.

### RAM.A dielectric F0

Use a deterministic single Fill layer with IOR `1.5` and neutral composition.

```text
F0 = ((IOR - 1) / (IOR + 1))²
F0 = ((1.5 - 1) / (1.5 + 1))² = 0.04
```

- Read uncompressed RAM source pixels.
- Every unaffected pixel alpha should be `0.04` within BGRA8 quantization tolerance.
- Expected 8-bit alpha is approximately `10 / 255`.
- Repeat with one non-default IOR to prove RAM.A is data, not a fixed fallback.

### Mip generation

- `MipGenSettings == TMGS_FromTextureGroup` for all four outputs.
- Built texture resources contain a complete mip chain.
- Expected mip counts: 1024 → 11, 2048 → 12, 4096 → 13.
- Inspect color, normal, and packed RAM mips for channel or gamma corruption.

### Existing-output update safety

Before Re-bake, retain references to all four object paths.

After Re-bake:

- All object paths are unchanged.
- `MI_<Name>` still references the same BC, Normal, RAM, and optional `ML_Height` paths.
- `UMaterialLabMaterial` baked references still point to those five assets.
- Previously assigned actors still reference `MI_<Name>`.
- Visible pixels change when the recipe changes.
- No duplicate or redirected assets are created.

### Save-failure paths

For every failed save, the message must contain the exact object path that failed.

Check failures independently for:

- `T_<Name>_BC`
- `T_<Name>_N`
- `T_<Name>_RAM`
- `T_<Name>_H`
- `MI_<Name>`
- the source `UMaterialLabMaterial` recipe

The dialog must not report only a package, folder, stage, or generic “Save failed” message.

## Existing source coverage

`MaterialLabBakeService.cpp` currently configures:

- exact render-target dimensions for all texture sources;
- dedicated normalized Height output and `T_<Name>_H` asset creation;
- BC as sRGB with `TC_Default`;
- Normal as linear with `TC_Normalmap`;
- RAM as linear with `TC_Masks`;
- `TMGS_FromTextureGroup` mip generation;
- in-place texture and material-instance loading by object path;
- exact `Asset->GetPathName()` text for per-asset save failures.

GPU compositor tests already cover default and overridden IOR conversion to RAM.A.
They have not been run for this validation pass.
