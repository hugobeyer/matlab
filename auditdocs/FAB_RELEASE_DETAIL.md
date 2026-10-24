# Mixtormat FAB Release — Evidence and Exact Paths

Targeted source/config reads; recipe storage/workflow updated 2026-10-24.
No packaging or runtime validation.
Git stays private; exclusion from a release is not a request to delete repository files.
See [release checklist](FAB_RELEASE_PLAN.md) and [parameter audit](GHOST_PARAMS_AUDIT.md).

## Confirmed current state

| Path | Evidence / status |
|---|---|
| `Mixtormat.uplugin` | Version 1 / `1.0.0`, Engine `5.8.0`, Win64, Rendering, non-beta. Buyer description includes baking and baked-material runtime control. |
| `Mixtormat.uplugin` modules | MixtormatRuntime = Runtime; MixtormatShaders and MixtormatEditor = Editor with Editor target allowlists. |
| `Source/MixtormatRuntime/MixtormatRuntime.Build.cs` | Only Core, CoreUObject, Engine declared. This is not an exhaustive include audit. |
| `Source/MixtormatShaders/MixtormatShaders.Build.cs` | Runtime, RenderCore, RHI and Projects dependencies; no Editor module dependency declared. |
| `Config/` | DefaultMixtormat.ini, FilterPlugin.ini, MixtormatParameterAuthoring.json and UIStyleTheme.json present. |
| `Config/FilterPlugin.ini` | Explicitly includes both Config JSON files; excludes Docs/Old, Docs Markdown, Tools, .gitignore, Content PNG/PNG~/HDR/FBX and nested Source Tests. |
| `.gitignore` | Ignores Content, generated outputs and asset/image formats; exceptions for Icon128 and icon PNGs. SVGs are not covered by the PNG exclusion. Not a packaging policy. |
| `Docs/ThirdPartyNotices.txt` | Removed with its filter entry at owner request: Lucide/Feather no longer used; SVGs are owned. |
| `AgentDocs/` | Nine canonical guides plus ignored temp.md in visible root listing. Archived directories were not inspected. |

## Package inputs — retain and verify
- `Mixtormat.uplugin`.
- `Source/`: required module source; current filter excludes nested `Tests/`.
- `Shaders/`: required `.usf` and `.ush` files; registration coverage not audited here.
- `Config/DefaultMixtormat.ini` and `Config/FilterPlugin.ini`.
- `Config/MixtormatParameterAuthoring.json`: plugin-relative lookup in
  `Source/MixtormatEditor/Private/UI/Parameters/MixtormatParameterAuthoring.cpp`,
  `GetDatabaseFilePath()`.
- `Config/UIStyleTheme.json`: plugin-relative path in
  `Source/MixtormatEditor/Private/Style/MixtormatThemeSchema.cpp`, `SavePath()`.
  Generated defaults also exist in `MixtormatTheme.cpp`; their presence does not prove JSON packaging.
- `Resources/Icon128.png`: intended plugin icon; binary existence/appearance not checked.
- Registered `Resources/Icons/*.svg`, brand artwork and any still-used PNGs.
  `AgentDocs/ICONS.md` identifies current SVG registrations in
  `Source/MixtormatEditor/Private/Style/MixtormatStyle.cpp` as authoritative.
  The previous PNG-only recommendation was incorrect.

- Curated `Content/` plus all required dependencies; see below.

## Layer/group recipes — implementation and package scope
- Gallery has a third Recipes column, mounted by
  `Source/MixtormatEditor/Private/Widgets/SMixtormat_Library.cpp::BuildBottomLibrary`.
  Materials/Masks/Recipes use persisted `GalleryColumnWeights.X/Y/Z`.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Recipes.cpp`: recipe cards,
  search/category filters and plugin versus user labels.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Presets.cpp`: insertion,
  Refresh Recipes, Save User Copy, Update from Selected Group and referenced-asset details.
  Create Recipe also offers Publish Plugin Recipe. Insertion appends an independent
  editable group; document/global settings remain unchanged.
- `Source/MixtormatEditor/Private/Services/MixtormatRecipeService.*`: group/layer
  capture, strict dependency-closure validation and insertion preparation.
- `Source/MixtormatEditor/Private/Services/MixtormatPresetStoreDocuments.cpp`:
  plugin recipes: `<Plugin>/Resources/Presets/Recipes/*.json`;
  user recipes: `<Project>/Content/Mixtormat/Presets/Recipes/*.json`.
  These are JSON files, not Content Browser assets. Ramp/module paths are unchanged.
- Plugin publish/update requires `Mixtormat.Developer.ParameterMeta`; project save is the default.
  Updates require confirmation and matching destination/name/ID/filename; metadata is retained.
- Old `Saved/Mixtormat/Presets/Recipes` JSON remains untouched but is no longer listed.
  Explicitly relocate wanted recipes to the project folder; no migration or fallback was added.
- Owner decision: ship **all developer-authored recipes**, not a curated subset.
  Retain their complete asset dependency closure. Assets are referenced, not embedded.
- Publishing rejects direct/resolved paths outside `/Mixtormat/`, `/Engine/`, `/Script/`.
  Transitive dependencies and actual package inclusion still require owner checks.
  This source review does not establish which recipe files currently exist.
- Owner smoke checks: save a group recipe, search/filter it, insert twice, verify independent
  groups/internal links and unchanged globals; check project copies and missing-asset errors.
  Check confirmed updates, retained metadata/identity and developer-gated plugin writes.
- Artist menus omit JSON import/export. Card/empty-state help now describes Save User Copy,
  filtering and Create Recipe instead. Gallery restore help includes Recipes (2026-10-24).

## Filter gaps / limits
- Explicit includes for both Config JSON files were added on 2026-10-24.
  Actual inclusion still needs a BuildPlugin artifact/manifest check.
- Content exclusions remove raw import sources only. They do not curate `.uasset` files,
  HDR-derived assets, masks, sample materials or thumbnails.
- There are no explicit exclusions for every developer directory listed below.
  That does not prove leakage: BuildPlugin defaults are not equivalent to zipping the tree.
- The filter comments describe Epic FileFilter behavior; this pass did not inspect engine tooling.
- No built artifact was inspected. Neither a clean package nor a leak is established.

## Development-only paths — exclude from release, keep in repository
- `AgentDocs/`, `auditdocs/`, `Concepts/`, `Tools/`.
- Developer HTML/prototypes/assets under `Docs/`, except required notices.
- `.agents/`, `.claude/`, `.gemini/`, `.zed/`, `.stakpak/`, `.github/`, `.vs/`, `.git/`.
- `AGENTS.md`, `CLAUDE.md`, `mixtormat-icon.ico`, local audit/plugin archives.
- Local `Intermediate/`, `Saved/`, `DerivedDataCache/`, IDE files and development binaries.

Do not assume BuildPlugin output must contain no `Binaries/` at all: generated package
artifacts differ from local development outputs. Check current FAB code-plugin submission
requirements before final staging. Do not manually zip the entire working tree.

## Content — all developer recipes ship; dependency check pending
`AGENTS.md` excludes Content and binary assets from agent inspection. Previous counts
(871 Content files, six HDRs) and candidate existence were not revalidated.
Historical extra-demo candidates, **not a limit on shipped recipes or an approved manifest**:
- `Content/Examples/MxT_Peeled_Worned_Wall_Sample.uasset`.
- `Content/Effects/Peeling/DA_Peeling_Standard_01.uasset`.
- `Content/Effects/Stain/DA_Stain.uasset`.

Ship every developer-authored recipe and check its full dependency closure in Unreal
before excluding anything. A small extra demo set is optional, not a recipe limit.
Keep required plugin preview/runtime materials, meshes, textures, masks and referenced assets,
not just visible demo files. Regenerate sample thumbnails after curation if needed.
A separate demo-project download is an option, not a completed task.

## Unverified historical claims
- Old Source/Shaders/Resources/Content counts and byte sizes are not current evidence.
- The former long archive inventory mixed canonical guides with archived files.
  It has been replaced here by release policy; archive contents were not inspected.
- Stale-folder deletion, resource pruning, content curation and archive moves are not confirmed.
- No changelog found in the visible workspace; listing media may exist outside it.
- Docs/support fields are populated; URL availability and mail delivery were not checked.
- No definitive archive-root layout or FAB validator outcome is claimed without current guidance.

## Stable-source follow-up — 2026-10-24
- `Source/MixtormatEditor/Private/Compositing/MixtormatBakeService.cpp::GetOutputAssetNames`
  declares 10 outputs: BC, Normal, RAM, Height, RAMH, Optics, FuzzColor, Subsurface,
  Material Instance and Surface. Smoke checks must cover optical outputs as well.
- A complete optical-master contract uses `DA_Optics`, `DA_FuzzColorMap`,
  `DA_Subsurface` and `DA_UseSurfaceOptics`. Partial support errors; no support warns
  while still exporting maps. Shipping a working optical preview/master remains unverified.
- Config JSON filter rules are now explicit; packaged-file inclusion remains unverified.
- Moss and GPU-pass internals are intentionally excluded during active rewrites.
  Recheck their documentation and release behavior after those changes land.

## UI consistency follow-up — 2026-10-24
Status carried forward from [ui-style-token-audit.md](ui-style-token-audit.md) and the
implementation handoff, not an independent re-review of the latest source.
- Implemented: ramp/tab tokens, shared bake typography, styled mask-thumbnail hover,
  native-tooltip bypass fixes, bounded popover scrolling and text wrapping.
- Deferred to Q/context owner: `Widgets/SMixtormat_Preview.cpp` has two icon-button
  `.ToolTipText` calls in `MakeFeaturePreviewButton` and `MakeChildOutputPreviewButton`;
  use the existing `.ToolTip(FText)` wrapper.
- `Widgets/Layers/MixtormatLayerMenus.cpp::BuildGroupAccentMenu` has one native swatch
  `SBox.ToolTipText`; use the shared styled adapter. Paths are relative to
  `Source/MixtormatEditor/Private/`. These are tooltip fixes, not changes to Q input routing.
- Owner checks remain: clipping/scroll reachability, DPI, focus, capture, nested-menu and
  click-away dismissal, thumbnail lifetime, hover timing and theme reconstruction.
- Remaining bake chrome and Unreal-owned picker/tab help are tracked exceptions in the audit.
  Do not mark all UI compliant or runtime-validated. Recipe dependency checks remain separate.

## Owner-run release gates
1. BuildPlugin for UE 5.8 / Win64 and inspect the resulting file manifest.
2. Confirm both JSONs, SVGs, applicable notices and required content dependencies are included.
3. Confirm developer files/raw import sources are absent from the submission artifact.
4. Install in a clean project: open → compose → preview → bake.
5. Check baked-material behavior in a packaged game and review relevant log warnings.
6. Finish changelog, screenshots/video, demo/docs/support links and submission metadata.

No commands, git operations, tests, network checks or asset deletions were performed.
The obsolete Lucide/Feather notice was subsequently deleted with explicit owner approval.
