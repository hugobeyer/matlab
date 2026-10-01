# Step 11b: one Height Blend block for layers and generator modules

Paste `00-Shared-Rules.md` above this. Run it after Step 11 Part 1, before Parts 2 and 3.

**Never delete a user-facing feature.** Every existing Height Blending control must survive. It moves; it doesn't disappear.

## Why
Height is currently set in four separate places: the layer Height Op, layer Height Blending (HMB, a toggle plus a dozen controls), the per-module generator blend, and Hugo's runover formula (below). Hugo wants **one** block that means the same thing everywhere.

## Target
- **One struct, `FMixtormatHeightBlend`,** used by `FMixtormatLayer` (composition against the stack) and by `FMixtormatGenerator` (a module against the layer's running height).
- **One shader function** in `MixtormatHeightOps.ush`, used by the composite and by the generator bundle combine (stage 9).
- **Op list (`EMixtormatHeightOp`):** Replace, Add, Subtract, Multiply, Min, Max, Difference, plus **Height Blend** as a new value. Unshipped, so the enum can be reordered.
- **Common fields:** Op, Softness (Min/Max), Amount.
- **Height Blend fields** (shown only with that op): Strength, Threshold, Softness, Base Bias, Blend Bias.
  - **Layer-only extras** under Height Blend, all kept: Height Source, Contrast, Invert, Mask Height Influence, Contact AO (amount and width), Border Normal (lift, width, strength, smoothing), Smooth Radius/Amount, Reference Layer, and the driven Blend Strength (driver slot 2).
  - The Contact AO, Border Normal and Height Blend debug previews stay.

## Height Blend maths (Hugo's Copernicus "runover" kernel; use it verbatim)
```
a = base + base_bias
b = blend + blend_bias
m = saturate(mask * strength)
s = max(softness, 1e-6)
t = smoothstep(threshold - s, threshold + s, m + b - a)
height = lerp(a, b, t);  coverage/blend mask = t
```
- **For layers:** `base` is the height below (or the Reference Layer's snapshot when set), `blend` is the layer's incoming height (through Height Source, Contrast and Invert as today), and `mask` is the layer's placement mask (with Smooth Radius/Amount applied as today).
  - `t` is the coverage every channel uses (colour, roughness, AO, metallic, normal), exactly as the HMB weight is today.
  - Contact AO and Border Normal read `t` the way they read the HMB weight now.
- **For modules:** `base` is the running layer height, `blend` is the module height, and `mask` is the module coverage times the Part 3 mask input, once that exists.
- **Bare ground** (occupancy): Min, Max, Difference and Height Blend fall back to Replace where nothing is below.

## Migration of data (unshipped: no back-compat code, just map the fields)
- Delete `bHeightBlendEnabled`: choosing Op = Height Blend replaces it. The struct move is named here, so this is allowed.
- Map `HeightBlendAmount` to Strength, `HeightThreshold` to Threshold, `HeightRange` to Softness, `HeightBias` to Base Bias, and `HeightOffset` to Blend Bias. Move every other HMB field into the struct unchanged.
- Delete the layer's `HeightOp`/`HeightSoftness` and the module's `BlendOp`/`BlendSoftness`/`BlendAmount`, since they're now inside the struct. Defaults:
  - layers: Max, Softness 0.1;
  - fill layers: Replace;
  - the first module: Replace;
  - later modules: Add.

## UI
- **One BLEND row** (Op, Softness or Amount) at the top of every layer's COMPOSITION card and every module panel.
- The Height Blend settings open below it only when Op = Height Blend. For layers, the former Height Blending card becomes this section, with its eye on the header.
- **Badges** show the op; Height Blend shows as `HB`.

## Static checks
- dxc on the composite, the generator bundle and anything including `MixtormatHeightOps.ush`, at HV 2018 and HV 2021.
- Shader globals vs `SHADER_PARAMETER`.
- Grep for zero references to `bHeightBlendEnabled`, the old layer `HeightOp`/`HeightSoftness`, and the module `Blend*` fields.
- Grep that **every** former HMB control is still gathered, bound and shown.

## Checklist for Hugo
- **A layer at Op = Height Blend:**
  - painting the mask pushes the layer through (runover);
  - Threshold and Softness shape the edge;
  - Base and Blend Bias shift the contest;
  - Contact AO, Border Normal, Smoothing and Reference Layer all still work.
- Other ops behave as before, and the first layer on bare ground still acts like Replace.
- A generator module at Height Blend runs over the modules above it the same way.
- The same BLEND row appears on layers and modules.
