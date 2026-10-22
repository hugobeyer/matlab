# Mixtormat — Agent Routing

Read this file before any repository-wide search. It routes you to the right
subsystem doc and the right files; it is not the full manual.

# IMPORTANT

- **Do not spend tokens on code reviews or review-only subagents unless explicitly requested.**
- **The user handles compilation and testing. Do not duplicate that work with unsolicited validation.**
- Read only the context needed to implement the requested change, then report it concisely.

Minimize token consumption. Read only files necessary for the requested change if it was specified. Avoid repeated repository-wide searches, unnecessary explanations, and redundant tool calls. Preserve existing features or suggest necessities. When the request was literal, implement the task completely and report only modified files and essential findings.

## Code Reuse — Recommended Practices

- Prefer existing implementations before creating new functions, widgets, helpers or systems.
- Consider **Reuse → Extend → Refactor → Create**, but use judgment.
- Favor canonical ownership and a single source of truth.
- Avoid duplicating logic unless separation provides a clear benefit.
- Reuse shared components when appropriate; don't force incompatible abstractions.
- Prefer modular, maintainable implementations over unnecessary generalization.
- Preserve existing features and APIs unless removal is approved.
- When a new implementation is preferable, proceed without unnecessary restrictions.
- Keep changes focused and avoid unrelated refactoring.

## 0. Agent working style

- The user has ADHD: keep responses short, direct, and scoped to the request.
- Do not wander into unrelated analysis, changes, or recommendations.
- Do not introduce hardcoded values; use canonical settings, constants, tokens, or metadata.
- If you have a question or a material uncertainty, ask the user before proceeding.
- **Unshipped development:** no released authored artist content needs backward
  compatibility. Do not create migration stubs, deprecated fields, dormant code
  paths, redirects or guards for hypothetical old user assets.
- Preserve working features unless removal is approved. Once approved, delete
  obsolete parameters, runtime fields, gather/bindings, shaders, inspector
  controls, JSON metadata and associated docs together. No compatibility
  scaffolding solely for pre-release history.
- Maintain consistency across current code and internal data; ask if a genuine
  active dependency or unresolved design issue would be broken.
- Do not add or run tests unless the user explicitly asks.
- Update only documentation directly related to a completed change; keep it current.

## 1. What this is

Mixtormat is an Unreal Engine 5.8 procedural material authoring / mixing plugin.
Three modules, one-way dependency: **Runtime ← Shaders ← Editor**.

- **MixtormatRuntime** — data model, UObjects, parameter contract. No GPU, no Slate.
- **MixtormatShaders** — Editor-only. Gather + RDG GPU compositor + compute passes.
- **MixtormatEditor** — Slate UI, layer hierarchy, inspector, preview, bake, style.

## 2. First-read rules

1. Read this file first. Then open **one** `AgentDocs/` file for the subsystem.
2. Search by exact symbol (`rg`, LSP references) before opening files.
3. Open line ranges, not whole files. The runtime data model is split by domain:
   `MixtormatLayerTypes.h` (~680), `MixtormatGeneratorTypes.h` (~890),
   `MixtormatIdTypes.h` (~845), `MixtormatMaskTypes.h` (~600), `MixtormatEffect.h` (~1,180).
4. Follow the routing links below; do not re-derive architecture each session.

## 3. Hard exclusions — never read, search, or edit

- `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `.vs/`
- `Content/` (gitignored; `.uasset`/`.umap`/textures are binary)
- `.claude/worktrees/` (stale, empty)
- `*.uasset`, `*.umap`, `*.png`, `*.exr`, `*.hdr`, `*.zip`
- `Docs/docs-assets/` (design assets, not source)
- `AgentDocs/old_docs/` (superseded; see §6)

## 4. Architecture map

| Area | Primary location | Entry points |
|---|---|---|
| Runtime data/model | `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h` (aggregation; plus `MixtormatMaterial.h`, `MixtormatMaskTypes.h`, `MixtormatIdTypes.h`, `MixtormatGeneratorTypes.h`, `MixtormatParameterTypes.h`, `MixtormatHeightTypes.h`, `MixtormatEffect.h`) | `FMixtormatLayer`, `FMixtormatLayerChild`, `UMixtormatMaterial`, all enums |
| Layer composition | `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp` | `EnqueueCompose`, `AddLayerCompositePass` |
| GPU passes | `Source/MixtormatShaders/Private/MixtormatGpu*Passes.cpp` | `Add*Passes` functions |
| Shaders | `Shaders/Private/*.usf`, `*.ush` | `IMPLEMENT_GLOBAL_SHADER` sites in `MixtormatShaders` |
| Generator system | `MixtormatGpuGeneratorPasses.cpp`, `Compositing/MixtormatGeneratorGather.cpp` | `AddGeneratorLayerPasses`, `GatherGeneratorChild` |
| Masks | `MixtormatGpuMaskPasses.cpp`, `Compositing/MixtormatMaskGather.cpp` | `AddMaskPasses`, `GatherMaskChild` |
| IDs | `MixtormatGpuPatternPasses.cpp`, `Compositing/MixtormatIdGather.cpp` | `AddRegionProducerPasses`, `GatherIdChild` |
| Inspector | `Source/MixtormatEditor/Private/Widgets/Inspector/*.cpp` | `SMixtormat::Build*Panel` |
| Layer hierarchy | `UI/Layers/*`, `Widgets/Layers/*` | `SMixtormatLayerHierarchy`, `MixtormatLayerChildren` |
| Preview viewport | `Widgets/SMixtormatPreviewViewport.*`, `Preview/*` | `SMixtormatPreviewViewport`, `MixtormatPreviewSceneSettings` |
| Ramp controls | `UI/Controls/SMixtormat{Color,Scalar}Ramp.*`, `SMixtormatRampEditor.*` | shared ramp widgets |
| Parameter metadata | `UI/Parameters/MixtormatParameterUiMeta.*`, `MixtormatParameterAuthoring.*` | `MixtormatParameterUi`, `MixtormatParameterAuthoring` |
| Clipboard/reference | `Widgets/Layers/MixtormatLayerClipboard.cpp`, `Runtime/Public/MixtormatOutputReference.h`, `MixtormatParameterBinding.h` | `MixtormatOutputReferences`, `MixtormatParameterBinding` |

## 5. Modification rules

- Do not add or run tests, test harnesses, diagnostics, or automated validation.
  Use targeted source reads and static review only. Builds, commands and Unreal
  launches require explicit user consent; testing requires explicit consent too.
- Identify canonical ownership before editing; do not duplicate definitions.
- Before removing parameters, behavior or APIs, confirm their active readers
  (reflection, shader tags, gather) and update those readers. Unshipped backward
  compatibility is not a reason to retain explicitly retired features.
- Preserve the Runtime → Shaders → Editor direction. Never include Editor from
  Runtime or Shaders.
- Shader parameter changes must be traced through: CPU declaration → gather →
  dispatch/binding → defaults → inspector metadata → `.usf`/`.ush`.
- UI changes use `Style/MixtormatDesignTokens.h` + `MixtormatThemeStore`, not
  local styling.
- Layers, Library and Global share the left-column Shell background and reusable
  foldout/card/well/row recipes. Never add independent local palette/opacity
  numbers or borrow marking-menu geometry for left-column page layout. Page-only
  spacing and Library label opacity belong in Shell UI STYLE metrics; retain the
  viewport builders' behavior while adding explicit compact variants as needed.
  See `AgentDocs/UI.md` > Left-column visual contract.
- The Layers stack is dock-only: never recreate the grab header, floating Layers
  overlay, snap-back gesture or its old UI STYLE controls. The Inspector remains
  eligible for overlay mode. Preserve Library/Global navigation.
- Never introduce native Unreal/Slate default tooltips (`.ToolTipText`, `SetToolTipText`,
  or unstyled `SToolTip`). All help popovers must use `SMixtormatHelp` or its
  `MakeStyledToolTip` adapter for interactive widgets. Text, appearance and timing
  must be centralized; no local tooltip styling. See `AgentDocs/HELPERS.md`.
- Enums serialize numeric values. Preserve values consumed by current data and
  code; deliberate pre-release schema changes may remove obsolete entries once
  their readers are updated. Do not add CoreRedirects just for unshipped assets.

## 6. Navigation recipes

- **Generator parameter?** `MixtormatGeneratorTypes.h` (struct) → `MixtormatGeneratorGather.cpp`
  → `MixtormatGpuGeneratorPasses.cpp` → `Mixtormat<Name>.usf` → `MixtormatInspectorGenerators.cpp`.
- **Preview feature?** `SMixtormatPreviewViewport.*` → `SMixtormat_Preview.cpp` →
  `MixtormatGpuDebugPreviewPasses.cpp` → `MixtormatDebugPreviewBlit.usf`.
- **Layer child type?** `EMixtormatLayerChildType` → `MixtormatChildCapabilities.cpp`
  → `MixtormatLayerChildren.cpp` → inspector → gather/compose.
- **Ramp change?** `SMixtormatScalarRamp.*` / `SMixtormatColorRamp.*` first, then consumers.
- **Parameter default/range?** `MixtormatParameterUiMeta.*` + `MixtormatParameterAuthoring.*`
  + `Config/MixtormatParameterAuthoring.json`.

## 7. Subsystem docs

- `AgentDocs/ARCHITECTURE.md` — modules, data flow, boundaries, canonical types
- `AgentDocs/FILE_MAP.md` — task → files, symbols → owners, large-file sections
- `AgentDocs/GENERATORS.md` — generator registration → gather → GPU → shader → inspector
- `AgentDocs/UI.md` — Slate regions, hierarchy, inspector, controls, theme
- `AgentDocs/ICONS.md` — icon set: names, keys, files, roles, how to add one, gaps
- `AgentDocs/HELPERS.md` — tooltips/hints/overlay help text: mechanisms, hotkey catalog, mapping + architecture TODOs
- `AgentDocs/COMPOSITION.md` — gather, masks, IDs, references, passes, caching
- `AgentDocs/SHADERS.md` — shader dirs, class↔file map, bindings, conventions
- `AgentDocs/CONVENTIONS.md` — naming, ownership, serialization, checklists

## 8. Stale docs

`AgentDocs/old_docs/` holds archived audits, superseded proposals and implemented
handoffs. It is excluded from agent reads/searches; use current subsystem docs and
source instead. `AgentDocs/code_docs/` still holds active or mixed plans — verify
against source before acting. Archiving does not imply runtime validation.
