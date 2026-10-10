# Mixtormat — Agent Routing

Read this file before any repository-wide search. It routes you to the right
subsystem doc and the right files; it is not the full manual.

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
- Do not remove parameters, behavior, compatibility paths, or public APIs that
  merely look unused — confirm readers first (reflection, shader tags, gather).
- Preserve the Runtime → Shaders → Editor direction. Never include Editor from
  Runtime or Shaders.
- Shader parameter changes must be traced through: CPU declaration → gather →
  dispatch/binding → defaults → inspector metadata → `.usf`/`.ush`.
- UI changes use `Style/MixtormatDesignTokens.h` + `MixtormatThemeStore`, not
  local styling.
- Layers, Library and Global share the left-column Panel background and reusable
  foldout/card/well/row recipes. Never add independent local palette/opacity
  numbers or borrow marking-menu geometry for left-column page layout. Page-only
  spacing and Library label opacity belong in Shell UI STYLE metrics; retain the
  viewport builders' behavior while adding explicit compact variants as needed.
  See `AgentDocs/UI.md` > Left-column visual contract.
- Never introduce native Unreal/Slate default tooltips (`.ToolTipText`, `SetToolTipText`,
  or unstyled `SToolTip`). All help popovers must use `SMixtormatHelp` or its
  `MakeStyledToolTip` adapter for interactive widgets. Text, appearance and timing
  must be centralized; no local tooltip styling. See `AgentDocs/HELPERS.md`.
- Enum values are serialized by value: append, never reorder. Renames go in
  `Config/DefaultMixtormat.ini` `[CoreRedirects]`.

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
