## Mixtormat — Unified Sources, Geometry and Albedo Architecture Audit

Read the attached "Wrinkle and Curvature Pinch — implementation plan" in full. Treat it as the proposed architectural direction, NOT implemented behavior.

Repository: https://github.com/hugobeyer/matlab

Audit the latest available source revision, starting with AGENTS.md and current subsystem documentation. Record the exact commit audited when available, and distinguish committed source from local/uncommitted differences. If revision or local-change information is unavailable under the permitted tools, state that limitation; do not run git or shell commands without explicit consent.

IMPORTANT: The Structural Warp/Height Push source picker has recently changed. It now contains "Choose source later" and no longer uses Advanced → Add unconnected. Verify current implementation before proposing UI changes.

### Objective

Combine the Sources shelf plan, Wrinkle/Curvature Pinch plan and procedural albedo/Color Ramp/Grade audits into one coherent architecture.

### Required architectural contracts

1. Sources is a non-compositing shelf for reusable generator outputs, Color Ramps and later Global Floats.

2. Generator sources publish typed fields, not direct material contributions.

3. A shared Color Ramp is reusable scalar-to-color mapping data. It does not independently publish a color texture or modify BaseColor.

4. Colorize/Gradient Map is a proposed consumer that accepts a compatible field and local/shared ramp, then produces color.

5. Preserve the existing Generator Height Color Ramp and bGeneratorAlbedo behavior.

6. ID colorization must retain discrete region identity and HSV jitter. Investigate deterministic per-ID ramp sampling and reusable palettes.

7. Grade remains a distinct color-adjustment stage. Audit modernization without conflating it with Colorize or changing existing serialized behavior.

8. Wrinkle and Curvature Pinch remain separate heightfield operations. Their final Height and any explicitly published derivative fields may become colorization drivers.

9. Preserve current target-first connection authoring. The proposed source-first Ctrl/Cmd-target-second workflow must use the same canonical validation/backend, not replace current UI.

10. Reuse existing scoped masks, Noise Gates, published-field references, instance systems and caching.

### Specific investigations

- Can Colorize reuse existing ramp GPU helpers without another competing implementation?
- Should Colorize be generator-owned, layer-owned or both?
- How should multiple Colorize operations compose without implicit last-wins behavior?
- How are Color, IDs and scalar fields transported or recomputed after structural UV warping?
- Which raw curvature, erosion and flow values are actually published versus temporary intermediates?
- How should flow vectors convert into stable ramp-driving scalars?
- Can HSV From IDs share ramp mapping data without corrupting discrete-ID semantics?
- Can Grade v2 reuse scalar curves while maintaining independent color-processing semantics?
- How can linked ramps preserve existing local editing, undo, instances, serialization and invalid-reference repair?
- Does the proposed Sources evaluation/scheduling contract support these consumers without cycles?
- Do shared ramps preserve existing interpolation, stop ordering, duplicate-position handling, alpha, HDR values and linear/sRGB conventions across CPU/UI/GPU paths?
- What is Colorize's explicit input contract for signed/unsigned fields, normalization, input ranges, clamping/wrapping and invalid/non-finite values? Separate display normalization from authored evaluation.
- Where are scoped masks and Noise Gates applied? Gate the consumer's contribution, not shared ramp data, and trace existing mask stages to prevent double masking.
- What explicit blend mode, strength, destination and execution order govern multiple Colorize operations? Preserve existing behavior while avoiding accidental last-wins semantics in new consumers.
- Which settings are shared versus placement/consumer-local? Shared ramp stops/interpolation must not inadvertently share input references, ranges, masks, blend settings or target GUIDs.
- Do ramp edits invalidate every dependent consumer through canonical cache keys/dependency tracking? Sources list reorder and collapse must not alter evaluation or invalidate material results solely due to presentation changes.
- Is per-ID ramp/palette assignment deterministic across seeds, palette edits, warping and resolution changes? Preserve discrete identity and invalid-ID semantics; document when palette changes intentionally recolor existing IDs.

### Output required

1. Compatibility with the uploaded implementation plan
2. Confirmed source findings with exact file:line references
3. Conflicts, omissions and required architectural amendments
4. Reuse opportunities versus genuinely new subsystems
5. One proposed combined field/source/color contract
6. Implementation dependency graph
7. Ordered minimal implementation phases
8. Explicit decisions still requiring user approval

Separate confirmed behavior, existing defects and proposed capabilities.

AUDIT ONLY. No implementation, file edits, tests, builds or diagnostics. Preserve all existing features and parameters. Do not propose removing functionality without explicit approval.