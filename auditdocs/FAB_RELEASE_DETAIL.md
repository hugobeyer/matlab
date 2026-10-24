# Mixtormat FAB Release — Detail Doc (exact paths)

- Git stays private. All STRIP below = pack-time exclude via `Config/FilterPlugin.ini` + `RunUAT BuildPlugin`, not git deletes.

- Counts (today): `Source/` 325 files, `Shaders/` 93, `Resources/` 86, `Content/` 871.
- Rule: FAB zip = `.uplugin + Config + Resources + Shaders + Source` + curated `Content/`.
- Everything else below = strip.

## KEEP — ship as-is
- `Mixtormat.uplugin` — 3 modules, UE 5.8, Win64, Runtime + 2x Editor-only.
- `Config/DefaultMixtormat.ini` (240 B).
- `Config/FilterPlugin.ini`.
- `Config/MixtormatParameterAuthoring.json` (28 KB) — defaults source.
- `Config/UIStyleTheme.json` (28 KB) — theme source.
- `Source/` — all 325 files (`MixtormatRuntime` + `MixtormatShaders` + `MixtormatEditor`).
- `Shaders/Private/*.usf|*.ush` — all 93 files.
- `Resources/Icon128.png` — FAB icon (whitelisted in `.gitignore`).
- `Resources/Icons/*.png` — only referenced icons (see `UI/Atoms/MixtormatIcons.*`).
- `Resources/Fonts/` — only shipped fonts.
- `Docs/ThirdPartyNotices.txt` — move to zip root if FAB requires it.

## STRIP — whole dirs, never in zip
- `AgentDocs/` — canonical 9 only at root (`ARCHITECTURE`, `COMPOSITION`, `CONVENTIONS`, `FILE_MAP`, `GENERATORS`, `HELPERS`, `ICONS`, `SHADERS`, `UI`); everything else consolidated under `AgentDocs/old_docs/` on 2026-10-23 (all pack-excluded).
- `auditdocs/` (`ui-style-token-audit.md`, `workspace-layout/`).
- `Concepts/` (`mixtormat-generator-ux/`, `ChatGPT Image*.png`, `image.jpg`, `image (1).jpg`).
- `Docs/` (`docs-assets/`, `ui-prototype/` + `ui-prototype.zip`, `index.html`, `mixtormat-ui-prototype.html`, `mixtormat-prototype-tokens.json`).
  - Note: `Docs/shadertoyerosion.md` is already gitignored as third-party ref — keep out.
- `Tools/` (`*.py`, `*.vbs`, `*.html`, `__pycache__/`, `upgrade_surface_optics.md`).
- `Binaries/` (`Binaries/Win64/`).
- `Intermediate/` (`AgentSanityStep1-20261001/`, `AuditBuild/`, `Build/`).
- `Saved/`, `DerivedDataCache/` (when present).
- Dot roots: `.agents/`, `.claude/worktrees`, `.gemini/`, `.zed/`, `.stakpak/`, `.github/`, `.vs/`, `.git/`.
- Root singles: `AGENTS.md`, `CLAUDE.md`, `FAB_RELEASE_PLAN.md`, this file, `mixtormat-icon.ico`, `audit.zip` (3.5 MB), `mixtormat_behavior_v2_audit.zip`.

## CURATE — `Content/` (871 local files, gitignored)
- Today: `Content/Effects/`, `Examples/`, `Lighting/`, `Masks/`, `Materials/`, `Meshes/`, `Surfaces/`, `Textures/`, `Thumbnails/`.
- Problem: `Content/Lighting/*.hdr` (+ `.uasset` twins, 6 HDRs) are heavy; `Content/Masks/TX_Masks_*.uasset` are many.
- Ship max 2–3 demo materials + 1 sample mesh + used masks only:
  - Keep candidate: `Content/Examples/MxT_Peeled_Worned_Wall_Sample.uasset`.
  - Keep candidates: `Content/Effects/Peeling/DA_Peeling_Standard_01.uasset`, `Content/Effects/Stain/DA_Stain.uasset`.
  - Drop or move rest to demo project download, not plugin zip.
- Rebuild `Thumbnails/` after curate; don't ship stale thumbs.

## FIX — small exact edits
- `Mixtormat.uplugin`: buyer-line `Description`, confirm `Win64`-only, verify `DocsURL` + `SupportURL`.
- `Config/FilterPlugin.ini`: confirm it doesn't whitelist trash dirs back in.
- `.gitignore`: already excludes `Binaries/`, `Intermediate/`, `Saved/`, `Content/`, `*.uasset`, `*.hdr`, `*.png` (except `Resources/Icon128.png`, `Resources/Icons/*.png`) — keep, add `FAB_RELEASE_PLAN.md` if internal-only.
- Delete stale: `AgentDocs/new iconography/New folder`, `New folder (2)/`.

## IRRELEVANT DOCS — full list (pack-exclude, git stays)
- Consolidated 2026-10-23: all files below now live under `AgentDocs/old_docs/` (same names, e.g. `AgentDocs/REVIEWS.md` → `AgentDocs/old_docs/REVIEWS.md`, `AgentDocs/Prototypes/...` → `AgentDocs/old_docs/Prototypes/...`).
- `AgentDocs/` root (35): ARCHITECTURE.md, BEHAVIOR_V2_FIX_TRACKER.md, BOOLEAN_OPS_MODULES.md, BOOLEAN_STACK.md, COMPOSITION.md, CONVENTIONS.md, FIELD_CONTRACT_P0.md, FILE_MAP.md, FINAL_BEHAVIOR_PLAN.md, FLOW_SCULPT_SHADER_HANDOFF.md, GENERATOR_SHADER_IMPROVEMENT_PLAN.md, GENERATORS.md, HANDOFF_IMP.md, HELPERS.md, ICONS.md, LAYER_SOURCE_MODEL.md, MIXTORMAT_ARCHITECTURAL_REVIEW.md, Mixtormat_ColorRamp_Field_Plan.md, Mixtormat_Field_Processing_Plan.md, mixtormat_generator_interaction_v1_implementation_plan.md, Mixtormat_Master_Roadmap_ForwardOnly.md, Mixtormat_Unified_Layer_Module_Plan.md, MOSS_REWRITE_HANDOFF.md, NOISE_FLOW_P1.md, NOISE_FLOW_P2.md, NOISE_V2.md, REVIEWS.md, SHADERS.md, SHARED_BLUR_SMEAR_PLAN.md, SKIN_INSPECTOR_AUDIT.md, SURFACING_PLAN.md, SURFACING_RUNTIME_UI_HANDOFF.md, UI.md, UNIFIED_POSTFX.md, UNIFORMITY_ROADMAP.md.
- `AgentDocs/archive_old/` root (4): BEHAVIOR_V2.md, chatgpt_mockup_ideas.md, groksflowui_mockups_ideas.md, heightmap_fixing_needs.md.
- `AgentDocs/archive_old/behavioral_audits/` (7): deepseek.md, glm.md, groks.md, muse.md, qwens.md, sonnet.md, step5.md.
- `AgentDocs/archive_old/code_docs/` (19 md + 1 html): caching_optimization_plan.md, consolidated_audit_remediation_plan.md, flow_generation_core.md, fracture_math_validation.md, generator_interaction_ux_design.md, generator_warp_output_alignment_design.md, hidden_features_and_ideas_roadmap.md, mixtormat_ids_masks_peeling_refactor_plan_updated.md, modular_layer_graph_audit_handoff.md, multi_material_substrate_plan.md, newmigrationproposal.md, parameter_system_refactor_plan.md, priorities.md, rock_formation_shape_controls_plan.md, shippingplan.md, strata_structural_warp_design.md, surface_mask_rework_plan.md, wrinkle_curvature_pinch_plan.md, `Mixtormat Infrastructure Analysis — commit 1c91520.html`.
- `AgentDocs/archive_old/flows/` (3): evaluationorder.md, mixtormat_generator_interaction_mermaid.md, useruxflow.md.
- `AgentDocs/archive_old/RELEASE_READINESS/` (5): 01_composition_identity.md, 02_generators_masks_ids.md, 03_ui_ux.md, 04_performance_release.md, README.md.
- `AgentDocs/old_docs/` (27): Audit-2026-10-02.md, auditorkcsgen02.md, auditrocksgen01.md, auditrocksgen03.md, BoundaryFromIds.md, child-context-menu-outputs-audit.md, generator_flow_interaction_audit.md, generator_relationship_ux_plan.md, generator-parameter-necessity-audit.md, grok_audit.md, inspector-popover-handoff.md, latestaudit.md, mask_shaping_fix_plan.md, mermaid-diagrams.md, `Mixtormat — Current Priorities.md`, `Mixtormat — Main Branch Cleanup and Finish Plan.md`, mixtormat_mermaid_concepts.md, mixtormat_modules_refactor.md, Mixtormat_Preview_UX_and_Hotkeys.md, Mixtormat_Surfacing_Generator_Audit.md, MIXTORMAT_UI_STYLE_REWRITE.md, modules_audit.md, noise_gate_flow_handoff.md, overlay-workspace-handoff.md, UI_AUDIT.md, ui-layout-and-panels-audit.md, viewport-quick-controls-plan.md.
- `AgentDocs/old_docs/rampsaudits/` (2): deepseek_audit.md, sonnet_audit.md.
- `AgentDocs/audits/` (4): marketplacefab.md, Mixtormat_Unified_Architecture_Combined_Audit.md, overall_release_audit.md, previewmodes_audit.md.
- `AgentDocs/Prototypes/` docs (24, plus dir-level `*.cl|*.glsl|*.vfl` prototype code): Archived/MixtormatBooleanField.usf.txt, Archived/MixtormatClusterIds.usf.txt, Archived/MixtormatStrataCarver.usf.txt, Behaviorsopencls/README.md, CliffStrata/GENERATOR_AUDIT.md, Geology/01_GeologyCoordinates.txt, Geology/02_StrataVolume.txt, Geology/03_PieceFractureVolume.txt, Geology/README.md, layeredrock3d.md, MixtormatFractureBeforeNoShaderClamps.usf.txt, MixtormatFractureBeforeOwnershipGate.usf.txt, MixtormatFractureBeforeSdfBlend.usf.txt, MixtormatFractureBeforeShoulderFix.usf.txt, MixtormatFractureCracks.usf.txt, MixtormatFractureCracksResolve.usf.txt, MixtormatGpuFractureBeforeSdfBlend.cpp.txt, MixtormatGpuGeneratorPassesBeforeFracture.cpp.txt, MixtormatLayeredRockPrototype.usf.txt, MixtormatRockFormation_Step8.usf.txt, rockformation.md, RockFormation_Current.opencl.txt, RockFormation_EdgeToolkit.opencl.txt, ScatterCarve/README.md.
- `AgentDocs/NEW_TASKS/` (9): 01_behavior_inspector_dropdowns.md, `02_context_menu_consistency(DONE).md`, 03_shortcuts_and_multi_selection.md, 04_mask_source_drag_drop.md, 05_generic_object_naming.md, 06_flatten_behavior_child_menus.md, 07_sources_shelf_menu.md, 08_blur_feather_split_plan.md, 09_recipes_presets_and_group_controls.md.
- `auditdocs/` (5): ui-style-token-audit.md, workspace-layout/decisions-log.md, workspace-layout/global_variables_plan.md, workspace-layout/inspector-placement-model.md, workspace-layout/README.md.
- `Concepts/mixtormat-generator-ux/` (3, plus images/css): AGENT_HANDOFF.md, index.html, mixtormat-generator-ux-standalone.html.
- `Docs/` (10, plus `ui-prototype.zip`, `mixtormat-prototype-tokens.json`, `docs-assets/`): index.html, mixtormat-ui-prototype.html, ThirdPartyNotices.txt, ui-prototype/HANDOFF.md, ui-prototype/README.md, ui-prototype/UNREAL_PORT_PLAN.md, ui-prototype/UNREAL_STYLE_MAPPING.md, ui-prototype/index.html, ui-prototype/fonts/Inter-OFL.txt, ui-prototype/fonts/Roboto-OFL.txt.
- `Tools/` (2): upgrade_surface_optics.md, weathering-preview.html.
- Root (4, this file included — exclude from zip): AGENTS.md, CLAUDE.md, FAB_RELEASE_PLAN.md, FAB_RELEASE_DETAIL.md.
- `.github/copilot-instructions.md`.

## PACK — exact zip layout
- Zip root = plugin folder contents (not nested extra folder).
- Include: `.uplugin`, `Config/`, `Resources/`, `Shaders/`, `Source/`, curated `Content/`.
- Exclude: all STRIP dirs above + `*.sln`, `*.vcxproj*`, `.DS_Store`, `Thumbs.db`.
- Test links: `RunUAT BuildPlugin` output zip → clean UE 5.8 project → open asset → compose → preview → bake.

## FAB LISTING — outside zip
- Title, pitch, 5–8 screenshots, 30-sec video, limits (Win64, UE 5.8, bake workflow).
- Demo project link, docs site `https://hugobeyer.github.io/mixtormat/`, support `mailto:overjoy.hb@gmail.com`, license.
- Changelog + version `1.0.0`.
