# Next session: get back on plan (Generator layer)

Repo: `C:\Tools\MaterialLab\MatLab\Plugins\Mixtormat`.

## Read these first, in full
1. Memory: `C:\Users\hugob\.claude\projects\C--Tools-MaterialLab-MatLab-Plugins-Mixtormat\memory\MEMORY.md` and every file it links.
2. `AgentDocs/Prompts/00-Shared-Rules.md` (the hard rules override your defaults), then `02-Generator-Layer.md`, `03-Cleanup-Cross-Layer-IDs-Lattice.md`, `06-Generator-Offset-By-IDs.md` and `08-RockFormation-Upgrade.md`.
3. `git log --oneline -8` and `git status`. Commits since the plan started:
   - `c426485` prompts
   - `35f2ba7` rock fracture and gap
   - `b380bc2` nested ID groups

   There are uncommitted edits. Read them before changing anything.

## Hard rules (summary; 00 has them in full)
- **Static only.** Don't build, launch Unreal or run tests. Check shaders with dxc at HV 2018 and HV 2021, and compare the shader globals against `SHADER_PARAMETER` in both directions.
- **Never delete a user-facing feature unless the step names it.** Height Blending was wrongly deleted once and had to be restored. Save this as a feedback memory if it isn't saved yet.
- **No legacy code, no back-compat:** the plugin is unshipped. No clamps, only guards for undefined math.
- **No git commits** and no `git add`.
- **Report:** terse bullets plus a checklist for Hugo.
- **Parameter style** (memory `generator-param-principles`): normalised ranges, relative sizing, ratios instead of extra controls, pieces follow their parent.

## Where things went wrong
- **The plan was:** Step 1 (Height Op) → **Step 2, generators become a layer type** → Step 3 (delete generator children, cross-layer IDs, Lattice) → Step 6 (per-ID offsets).
- **What happened instead:** Step 2 was never started. `EMixtormatLayerType` is still `{Material, Fill}`, and generators (Strata, Cracks, Rock, Pebbles) are still **children** (`EMixtormatLayerChildType::Generator`). The pipeline still runs `AddGeneratorFieldPasses` and `AddGeneratorPasses`.
- **A later handoff went further off plan.** It asked for per-generator UV placement, per-ID offsets and per-generator blending on the child model. **Do not do that handoff.** In the layer model:
  - **Per-generator blend:** one layer holds one generator, so the layer's **Height Op** is the generator's blend. Don't add a second blend control.
  - **Generator placement:** the Generator layer's **UV transform moves the generator's geometry**, not just a texture lookup.
  - **Per-ID offsets:** that's Step 6. It reads IDs from a **layer below** through the 3b ID-source picker, so a generator never reads its own IDs and nothing loops back on itself.
  - **"UV From IDs does nothing to the generator":** expected on the child model. Don't patch it; the layer model fixes it.

## Keep (verify it exists, don't redo it)
- **Step 1, as restored:**
  - Height Blending (HMB) is intact, with every control: source, strength, contrast, biases, invert, contact AO, border normal, smoothing, reference layer, driver slot 2, and the debug previews HeightBlend, ContactAO and BorderNormal.
  - **Height Op** and **Softness** sit at the top of the COMPOSITION card. The default is Max at 0.1 (the old BLEND look); fill layers default to Replace.
  - **Occupancy (empty ground):** on bare ground, Min, Max and Difference behave like Replace.
  - With HMB on and Op = Replace, the result equals the old HMB exactly.
  - The generator blend enums and per-generator Amount fields stay deleted.
  - The details are in `AgentDocs/Prompts/HANDOFF-HeightBlend-Restore.md`. Finish its open items 1 to 5 first if they're still open: symbol cross-check, `GeneratorPasses` leftovers, badge comments, composition-control decision, prompt and memory updates.
- **Step 8 Rock work:**
  - normalised outputs (Height, Slope, Gap), Top/Chamfer/Wall masks and ramps, signed distance;
  - shader stages 0–3, 10 node-cache slots, 13 layer-memo slots.
  - Check it against `AgentDocs/Prototypes/RockFormation_EdgeToolkit.opencl.txt` and the defaults table in 08. The parked full shader is at `AgentDocs/Prototypes/MixtormatRockFormation_Step8.usf.txt`; diff it against the live `MixtormatRockFormation.usf` and port anything missing.
- **ID work from the later agent:**
  - typed output references, which are the base for 3b;
  - nested ID Groups;
  - ID indexing in Ramp/UV/Relief/Combine;
  - the `ValidateInsert -> int32` fix;
  - the completed `MixtormatIdGroupTests.cpp`.

## Do, in order (report and stop after each one for Hugo)
1. **Sanity pass.** Statically confirm the working tree is consistent. Every referenced symbol must exist and every shader must compile under dxc. Fix only real breaks.
2. **Step 2, the Generator layer** (`02-Generator-Layer.md`), **plus this addition** to that prompt and to the code:
   - A Generator layer's own UV transform (offset, rotation, scale, tiling) transforms the **generator's evaluation coordinate**, so geometry, height, IDs, masks and boundaries all move together.
   - Rotation must keep tiling: integer tiling, and quarter turns or the lattice snap the Strata direction uses.
   - Only the generator's input coordinate changes, never the published outputs afterwards.
3. **Step 3a:** delete generator children entirely. That removes the child-model code the off-plan handoff wanted to patch.
4. **Step 3b:** cross-layer IDs and the ID-source picker, built on the typed output references that already exist. Fold in "UV From IDs after deferred Breakup/Combine", which is a scheduling and dependency job, not a fallback.
5. **Step 6:** per-ID offsets for Strata and Cracks, **and Rock**: each region gets a whole-rock offset from IDs below, applied to the evaluation coordinate before the field.
6. **Then 3c (Lattice), 07 (shared edge toolkit for Cracks) and 04 (Freeze).**

## Checklist to hand Hugo at the end of each step
- Adding a Generator layer for each kind works, and the layer's UV transform visibly moves the generator's geometry.
- The Height Op on a Generator layer blends it with the stack. No per-generator blend rows exist.
- Height Blending and all its controls still work.
- Rock at the 08 defaults matches the Copernicus proto.
- Nothing has been built yet. Build and run the Composition, ID Group, layer-preview and runoff tests.
