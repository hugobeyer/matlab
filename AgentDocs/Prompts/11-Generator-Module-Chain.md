# Step 11: Generator layer as a module chain (replaces 02 follow-up, 03a and 06)

Paste `00-Shared-Rules.md` above this. **Never delete a user-facing feature unless this prompt names it.** Run the parts in order and stop after each one for Hugo.

## The model (Hugo, 2026-10-01)
A **Generator layer** holds an **ordered chain of generator modules**: Rock, Strata, Cracks, Pebbles, and later Lattice.
- **Top to bottom is dependency order.** A module can use the outputs of modules **above** it in the same layer: Strata per rock, cracks per rock, rocks split along strata beds, and so on.
- **Each module has its own blend** into the layer's running height. The finished layer then meets the stack below through the layer's **Height Op**.
- **Material layers no longer host generators.** Generators exist only inside Generator layers.

## What already exists (Step 2, commits 7e87c29 / 3fa6ea4 / 31e4d29). Reuse it.
- Generator layer type, `bGeneratorDrivesCoverage`, `FGeneratorBundle`, `AddGeneratorLayerPasses`.
- Placement: `MixtormatGeneratorPlacement.ush` and `FillGeneratorPlacement`. The layer UV moves all geometry.
- Publish-after-flow, the layer-level preview and Copy Output, and Strata's Height Follow reading the composite below.
- The old child-generator chain (`AddGeneratorPasses`, which carves `LayerInputHeight` in order).
- **To replace:** the single `FMixtormatLayer::Generator` payload, and the gather trick that appends it as a fake child.

## Part 1: container, per-module blend, coverage, outputs
- **Data:**
  - Delete `FMixtormatLayer::Generator`; its replacement is named here, so this deletion is allowed. A Generator layer's modules are its generator children (`EMixtormatLayerChildType::Generator`).
  - Keep `bGeneratorDrivesCoverage` on the layer.
- **Per-module blend:** add to `FMixtormatGenerator` (the child wrapper):
  - `BlendOp`: the same op list as `EMixtormatHeightOp`, reusing that enum;
  - `BlendSoftness`;
  - `BlendAmount` (0..1).
  - Defaults: the first module Replace, others Add.
  - One shared combine pass: `running = lerp(running, op(running, module), amount × module coverage)`. Reuse the composite's `ApplyHeightOp` maths (move it to a shared `.ush` so both use one copy). Don't touch each generator's shader.
- **Coverage merge** follows the op, with no extra control:
  - Replace/Add/Max/AddSub/Overlay extend coverage: `max(cov, moduleCov)`.
  - Subtract/Min/Multiply/Difference only change height inside the existing coverage.
  - The layer's coverage is the merged result, and `bGeneratorDrivesCoverage` uses it.
- **Outputs:**
  - Every module publishes under its own name, e.g. "Rock › IDs", "Strata › Bed Position", keyed by the module's child index.
  - The layer's default IDs are the **last** module in the chain that produces IDs.
  - Existing ID consumers in the layer keep the "nearest above" rule.
- **Material layers:** remove "Generator" from their add menu. The pipeline evaluates generator children only on Generator layers; generator children on Material layers are not gathered (unshipped, so no migration).
- **Editor:**
  - The Generator layer's add menu lists the modules.
  - Each module panel gets a BLEND row (Op, Softness, Amount) at the top.
  - Badges show each module's op.
- **Caching:** each module keeps its node-cache key. Modules don't read each other yet (Part 2).
- **Docs:** add a Generator layer section.

## Part 2: Region input (modules read IDs from modules above)
- Each module gets a **Region** input: a picker listing only modules **above** it in the same layer that publish IDs (with CentreUV and orientation). Default **None**.
- **When set, before field evaluation:**
  - the module's evaluation coordinate gets a per-region offset and a per-region rotation (Strata snaps to its tileable lattice angles);
  - its per-region randoms are re-seeded from the region ID;
  - one switch, **Stay Inside Region**: the module's coverage is clipped to the region, so a pattern never crosses a rock border.
- Implement for Strata and Cracks first, then Rock and Pebbles.
- **At None, output is bit-identical to Part 1:** branch, don't multiply by zero.
- **Cache key:** includes the upstream module's key when Region is set.
- **No cycles:** the picker can't list the module itself or modules below it.

## Part 3: Flow and Mask inputs
- **Flow:** a picker over modules above that have flow tools. The module's evaluation coordinate is warped by that module's flow (its `WarpedUV` / flow direction output), so modules can follow a rock's flow.
- **Mask:** a picker over named masks from modules above (e.g. "Rock › Top"), gating where this module acts (multiplies its blend amount). Optional invert.
- Defaults None, bit-identical at None, upstream keys in the cache key, no cycles.

## Static checks (every part)
- dxc on every touched shader, at HV 2018 and HV 2021.
- Shader globals vs `SHADER_PARAMETER`.
- Grep that `FMixtormatLayer::Generator` has zero references after Part 1.

## Checklist for Hugo (per part)
- **Part 1:**
  - A Generator layer with Rock, then Cracks (Add), then Pebbles (Max): each module's blend works.
  - Coverage merges, and the gaps show the layer below.
  - Each module's outputs preview by name.
  - Material layers no longer offer generators.
- **Part 2:** Strata with Region = Rock gives each rock its own beds, and with Stay Inside the beds stop at the rock edges. Cracks with Region = Rock gives cracks per rock. At None, nothing changes.
- **Part 3:** a module follows Rock's flow; Cracks with Mask = Rock › Top only cracks the tops.
