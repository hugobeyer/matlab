# External audit prompts — professional Surfacing

## Agent 1 — Skin realism, layered appearance, and scientific controls

```text
Audit Skin surfacing in this Unreal Engine material-authoring plugin:
C:\Tools\MaterialLab\MatLab\Plugins\Mixtormat

Read AGENTS.md first, then the relevant AgentDocs routing links.
This is an AUDIT AND DESIGN PROPOSAL, not implementation.

Rules:
- Read-only. Do not edit/delete/move files or change material assets.
- No Git, terminal/shell commands, builds, installs, tests, or diagnostics.
- Respect AGENTS exclusions, including Content, Saved, binaries, and old_docs.
- Do not modify or reconstruct Config/MixtormatParameterAuthoring.json.
- Public web research is wanted. Do not upload project source or use credentials.
- Preserve existing features, bindings, outputs, and shader behavior.
- Cite current file:line evidence. Do not treat comments as proof of behavior.

Goal:
Recommend a professional, controllable Skin system, not more arbitrary noise sliders.
The artist wants layered appearance controls for veins, vascularity, pores, age,
dryness, fullness, underlying tissue, bluish veins, and fine hair-like detail.
Challenge those suggestions where a better physical/artistic model exists.

Starting points (search symbols before reading large files):
- Source/MixtormatRuntime/Public/MixtormatSurfacingTypes.h
- Source/MixtormatRuntime/Private/MixtormatSurfacingTypes.cpp
- Shaders/Private/MixtormatSkin.usf
- Shaders/Private/MixtormatSkinSolve.usf
- Source/MixtormatShaders/Private/MixtormatGpuSurfacingPasses.cpp
- Source/MixtormatShaders/Private/Compositing/MixtormatSurfacingGather.cpp
- Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorSurfacing.cpp

Investigate:
1. Inventory current tissue, fold, pore, pigment, vessel, roughness, fuzz, and
   subsurface controls. Classify each requested capability as present/partial/missing.
2. Trace what drives each channel. Which variations share meaningful fields, and
   which are unrelated noise or fixed shader constants?
3. Separate epidermal pigmentation, capillary perfusion, larger subdermal veins,
   vessel depth/diameter, and tissue thickness. Blue-looking veins must not be
   described as blue blood. Explain a defensible real-time approximation.
4. Propose the smallest useful set of color ramps and scalar response curves.
   For EACH: input field/domain, output meaning/channel, blend rule/order,
   neutral state, artist benefit, and whether it needs a new generated field.
5. Separate hydration, sebum/oil, surface water, scaling/flaking, and roughness.
   Avoid a universal Dryness knob that silently conflates all of them.
6. Separate intrinsic aging, photoaging, and anatomical/body-site differences.
   Avoid pretending a single age number uniquely predicts all skin appearance.
7. Distinguish bulk relief, micro-normal detail, follicle marks, and real vellus
   hair geometry. What can a baked material actually achieve at different distances?
8. Suggest improvements beyond the artist's list: spatial scale, directional
   organization, feature size distributions, anatomical profiles, coherent variation,
   and resolution-aware filtering. Do not bake lighting/AO into albedo by default.
9. Identify useful artist output masks, not a public solver-debug browser.
10. Identify integration costs: structs, gather, binding, shader stages, ramps,
    instance/group behavior, cache hashing, presets, and source-contract metadata.

Research:
Find primary papers, measurements, or authoritative datasets on skin optics,
reflectance, pigment/blood variation, blue veins, roughness, and microstructure.
Useful starting references, not permission to cite unread details:
- NVIDIA GPU Gems 3, Chapter 14, Advanced Techniques for Realistic Real-Time Skin Rendering.
- Weyrich et al., Analysis of Human Faces Using a Measurement-Based Skin Reflectance Model.
- Donner and Jensen, A Spectral BSSRDF for Shading Human Skin.
- Jacques, Optical Properties of Biological Tissues: A Review.

For every numerical recommendation, label it as:
A) measured/reported, B) derived, or C) proposed artist tuning range.
State units, color space, wavelength/illumination, population/body site, and model
where relevant. Do not invent universal skin RGB albedo or roughness values.
Do not equate measured BRDF parameters with Unreal perceptual roughness.
Separate diffuse reflectance, single-scattering albedo, MFP, and geometric thickness.
State whether you read full text, an abstract, or metadata only; include URLs/DOIs.

Deliver:
- Executive summary: at most 8 short bullets.
- Current-code matrix with evidence and confidence.
- Proposed artist controls/ramps table with causal channel mapping.
- Scientific reference and quantitative-range table.
- P0/P1/P2 plan: immediate correctness, high-value controls, optional advanced work.
- Risks, unsupported claims, and things NOT to build.

SSS rendering-path verification is assigned to another agent. Describe Skin's
optical requirements, but do not duplicate the whole master-material investigation.
Do not claim the current screenshot proves that SSS is disabled.
```

## Agent 2 — Moss procedural masks, growth forms, bulges, and appearance

```text
Audit Moss surfacing in this Unreal Engine material-authoring plugin:
C:\Tools\MaterialLab\MatLab\Plugins\Mixtormat

Read AGENTS.md first and follow relevant AgentDocs routing links.
This is an AUDIT AND DESIGN PROPOSAL, not implementation.

Rules:
- Read-only. No code/asset edits, deletions, or migrations.
- No Git, terminal/shell commands, builds, installs, tests, or diagnostics.
- Respect excluded Content, Saved, binary files, and old_docs.
- Do not modify/reconstruct Config/MixtormatParameterAuthoring.json.
- Research public papers online; do not upload source or expose credentials.
- Preserve both existing Moss cushion approaches and optional bitmap workflows.
- Cite current file:line evidence; mark unverified visual explanations as hypotheses.

User goals:
- Professional Moss controls for age, plumpness/bulbness, cavities, grunge, and color.
- A default procedural distribution/mask, without requiring bitmap inputs.
- The rounded bulky Moss look seems lost or confined to the 'Legacy' mode.
  Both rounded/inflated and newer growth approaches must remain available.
- Users need useful material/mask outputs, NOT solver inspection controls.
- Moss Height and leaf detail normals are intentional. Do not remove them.

Starting points:
- Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h (FMixtormatMoss)
- Source/MixtormatRuntime/Private/MixtormatSurfacingTypes.cpp
- Source/MixtormatShaders/Private/Compositing/MixtormatSurfacingGather.cpp
- Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp
- Shaders/Private/MixtormatMoss.usf
- Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp
- Existing generated-mask/Noise/reference-input owners, discovered through AGENTS.

Investigate:
1. Verify both cushion solvers, default selection, inspector exposure, and presets.
   Explain deposition vs distance-field inflation in artist terms. Recommend names
   that communicate shape, not development history. Do not delete either mode.
2. Trace loss-of-bulge possibilities: profile, growth maturity, thickness budget,
   normalization, coverage, masks, HeightScale, resolution, and substrate influence.
   Distinguish proven code behavior from causes requiring a visual comparison.
3. Does the default already generate procedural seeds? Are bitmap inputs optional?
   Evaluate mathematically what happens on a flat substrate with no external mask.
4. Separate three roles: initial seeds, habitat/growth permission, and final
   application coverage. Do not design one ambiguous mask that does all three.
5. Recommend the smallest canonical procedural distribution solution. Compare
   improving built-in seeding against reusing an existing Noise/generated-mask
   module/output. Avoid hidden duplicate generators, new fallback systems, or a
   mandatory bitmap. Keep artist-supplied masks as intentional overrides.
6. Inventory current age/moisture/coverage/biomass/cavity/color/roughness fields.
   Explain which current ramp inputs are entangled and should be separable.
7. Propose useful ramps/curves for living growth, senescence, moisture, canopy
   position, exposure, deposits/dirt, and colony variation. For each specify driver,
   units/domain, channel effects, neutral state, cost, and purpose.
8. Distinguish self-shadow/AO from pigment. 'Grunge' should have a mechanism:
   deposited dust, substrate showing through, trapped debris, damage, or dead tissue.
9. Consider botanical growth forms beyond generic balls: cushions, tufts, mats,
   creeping/feathery growth, leaf curl, shoot orientation, tip/base variation,
   dry/wet structural change, sparse margins, and species-level presets.
10. Identify what a height-field/normal material cannot reproduce: silhouettes,
    overhangs, actual leaf transmission, and macro geometry at close range.
11. Recommend customer output masks; keep Chemical A/B, Activation, and similar
    solver state internal unless a distinct artist need is demonstrated.
12. List gather/cache/dispatch/AA/parameter/serialization implications and costs.
13. Audit substrate-aware crack filling and canopy bridging. The user supplied a
    render where moss follows deep cracks instead of covering them and explicitly
    wants intelligent filling. The render alone does not establish active settings
    or whether every gap is displacement versus actual missing geometry.
    Implementation update (source changes only; no build/render verification):
    - MixtormatMoss.usf Stage 13 now resolves an opposing-bank canopy after
      normalization/scaling, within existing coverage and sampled scope gates.
      CushionSize bounds the UV span; positive HeightScale fades the correction.
      Audit support sampling, deep/narrow cracks, edges, units, and GPU cost.
    - Stage 7 now exchanges bounded slump between provisional deposited states
      instead of discarding downhill thickness. Audit conservation of this
      exchange separately from growth, creep, halo decay, and half storage.
    - MixtormatGpuGeneratorPasses.cpp EvaluateMoss now includes UpstreamDelta
      in substrate analysis and routes the corrected relief to ApplySurfacingFields.
    - GrowthCoverage now gates Moss ramp color and roughness through optional
      FSurfacingFields::MaterialCoverage. Height and leaf normals already contain
      coverage; do not multiply them again. Published material fields remain raw.
    - Both cushion modes, existing masks, and preset fields are preserved.
      See AgentDocs/SHADERS.md, Moss canopy and material coverage.
    Verify the current implementation rather than repeating the original defects.
    Explain why cavity attraction is not geometric filling, why increasing relief
    alone may retain the crack profile, and how coverage/gates limit bridging.
    Compare substrate coating, cavity accumulation, and bounded canopy bridging
    as distinct behaviors compatible with BOTH cushion solvers. Evaluate local
    support-envelope/morphological-closing approaches versus transport-based
    filling; do not recommend a blind max/boolean of relative height fields.
    Define common height datum/units, maximum span and depth, biomass/thickness
    budget, support and habitat constraints, crack-width sensitivity, and edge
    blending. Preserve intentional open gaps and artist application masks.
    Keep color, roughness, AO, derived height normals, and leaf detail normals
    coherent with the resulting coverage and surface. Check downstream layer
    composition cannot reintroduce cracks after the local solve.
    State height-field limits: a continuous top envelope can visually cover a
    depression, but cannot represent an underside/overhang or reconnect missing
    mesh topology. Propose artist-facing fill/span controls, cost, and a minimal
    implementation sequence; distinguish verified code from render hypotheses.

Scientific research:
Find accessible peer-reviewed moss/bryophyte work on hydration, growth forms,
water retention/capillarity, pigments/senescence, and spectral reflectance.
Use species/context-specific evidence; do not equate dry with dead or old with dry.
Do not assume all species prefer identical shade, substrate, moisture, or slope.
Separate leaf reflectance from canopy reflectance and reflectance factor from
hemispherical albedo. Measurements in NIR are not visible RGB material colors.

For values/ranges, distinguish measured/reported, derived, and artist-proposed.
Record species, wetness state, wavelength, geometry, units, and source URL/DOI.
Do not fabricate a universal moss RGB albedo or roughness interval.
State full-text vs abstract vs metadata-only access.

Deliver:
- At most 8 short executive-summary bullets.
- Existing/partial/missing capability matrix with current source anchors.
- Both-solver comparison and bulge diagnosis hypotheses.
- Procedural-mask architecture recommendation with explicit seed/habitat/gate roles.
- Artist controls/ramps + useful outputs table.
- Scientific references/ranges and prioritized P0/P1/P2 implementation plan.
- Limits and things not worth adding.
```

## Agent 3 — SSS, physical calibration, and production reliability

```text
Audit Surfacing production readiness in this Unreal Engine plugin:
C:\Tools\MaterialLab\MatLab\Plugins\Mixtormat

Read AGENTS.md first, then relevant AgentDocs/COMPOSITION.md and UI routing links.
This is READ-ONLY investigation and recommendations, not implementation.

Rules:
- No edits, deletion, migration, Git, terminal commands, builds, tests, or diagnostics.
- Never inspect excluded binary assets, Content, Saved, or old_docs.
- Do not modify/rebuild Config/MixtormatParameterAuthoring.json.
- Do not execute Tools scripts or change project rendering settings.
- Public official documentation research is allowed; keep source/credentials private.
- Cite current file:line evidence and distinguish proven defects from hypotheses.

Context:
The user reports Skin SSS not visibly working, lost saved Skin developer ranges,
and preset-loading errors at Skin.GravityHold / Skin.SurfaceTiling. These are
separate systems; do not assume they share a cause. The JSON developer-range file
exists in the workspace but a prior inspection found no Skin/Surfacing entries.
No restoration/history operation is authorized. Do not invent the lost values.

Skin and Moss realism/control designs are assigned to other agents. Your scope:
1. Trace SSS end to end: defaults -> gather/sanitize -> cache -> CPU shader binding
   -> optical outputs -> masks/layer composition -> finalization -> material binding
   -> preview/bake contract. Include the role of bWriteSSS and optical enable flags.
2. Verify units and semantics: MFP vs material thickness vs displacement scale;
   linear color vs sRGB; texture formats and import flags; channel packing.
3. Inspect source references and Tools/upgrade_surface_optics.py/.md to establish
   required master-material connections. Do not run it or assume generated assets
   match the script. Parameter presence alone does not prove graph wiring.
4. Consult official Unreal documentation for supported Substrate SSS/diffusion,
   GBuffer modes, fuzz coexistence, platform limitations, and version differences.
   Plugin targets UE 5.8; state if documentation is for another version.
5. Identify what cannot be proven without the selected binary material, runtime
   configuration, or a render. Return the smallest specific evidence request needed.
6. Audit how new surfacing fields/ramps affect strict preset JSON decoding and
   shader/authoring metadata. Explain how to keep developer assets current without
   adding speculative compatibility layers or silently accepting corrupt data.
7. Audit developer-range load/save safety: cached state, overwrite scope, failed
   reads/writes, owner/family identity, and possible Skin/Moss key collisions.
   A plausible loss path is not proof that it caused this user's missing entries.
8. Identify professional release requirements: explicit unsupported-feature
   feedback, safe persistence, deterministic presets, physical scale, stable output
   semantics, filtering, memory/performance budgets, and customer/dev separation.

Important decisions:
- UI STYLE and parameter-range editing remain ON in private developer builds,
  but are unavailable in customer packages. Normal JSON reading/bindings remain.
- Customers do not need a public solver-debug browser.
- Preserve both Moss cushion modes, real Height contributions, and leaf normals.
- Do not 'fix' SSS by just boosting color, blurring albedo, or adding fake glow.
- Do not 'fix' preset errors by merely suppressing the menu messages.

Deliver:
- At most 8 short executive-summary bullets.
- SSS pipeline/contract table: verified source behavior vs unresolved external state.
- Unit/color-space/range pitfalls, with official documentation URLs.
- Persistence/preset risk table with exact source anchors and confidence.
- P0 correctness blockers vs P1 improvements vs optional work.
- A proposed manual acceptance matrix (do not execute it): shader outputs, lighting,
  physical scale, mask isolation, preset round-trips, save safety, and packaged UI.
- Minimal next evidence needed, not a speculative rewrite.
```

## How to combine the reports

- Require one recommendation per issue, not three competing architectures.
- Keep **source facts**, **research evidence**, and **design proposals** visibly separate.
- Every ramp needs a meaningful driver; do not add a ramp for every descriptive noun.
- Preserve both Moss shapes and optional art-directed inputs.
- Treat new settings, persisted presets, and developer-range data as one change contract.
- Do not claim a renderer issue is fixed until its actual material/configuration is checked.
- Label numerical ranges honestly; reference measurements are not universal presets.
- Return a short ranked summary, with the detailed evidence in the reports.
