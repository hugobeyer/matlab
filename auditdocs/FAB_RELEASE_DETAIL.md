# Mixtormat FAB Release — Evidence and Exact Paths

Checked 2026-10-23 by targeted source/config reads. No packaging or runtime validation.
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
  search/category filters and shipped read-only versus user labels.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Presets.cpp`: insertion,
  Refresh Recipes, Save User Copy and referenced-asset details. Insertion appends an
  independent editable group; document/global settings remain unchanged.
- `Source/MixtormatEditor/Private/Services/MixtormatRecipeService.*`: group/layer
  capture, strict dependency-closure validation and insertion preparation.
- `Source/MixtormatEditor/Private/Services/MixtormatPresetStoreDocuments.cpp`:
  shipped recipe lookup is `Resources/Presets/Recipes`; recipe files are JSON.
- Retain intended shipped recipe JSON and every referenced asset during curation.
  Assets are not embedded in recipes. This check does not establish which recipes ship today.
- Owner smoke checks: save a group recipe, search/filter it, insert twice, verify independent
  groups/internal links and unchanged globals; check user copies and missing-asset errors.
- Artist menus omit JSON import/export. Card/empty-state help still mentioning those actions
  is stale copy to fix separately, not a missing feature to reintroduce.

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

## Content — owner decision and dependency check pending
`AGENTS.md` excludes Content and binary assets from agent inspection. Previous counts
(871 Content files, six HDRs) and candidate existence were not revalidated.
Historical candidates, **not an approved manifest**:
- `Content/Examples/MxT_Peeled_Worned_Wall_Sample.uasset`.
- `Content/Effects/Peeling/DA_Peeling_Standard_01.uasset`.
- `Content/Effects/Stain/DA_Stain.uasset`.

Select a small demo set, then check dependencies in Unreal before excluding anything.
Keep required plugin preview/runtime materials, meshes, textures, masks and referenced assets,
not just the visible demo files. Regenerate sample thumbnails after curation if needed.
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

## Owner-run release gates
1. BuildPlugin for UE 5.8 / Win64 and inspect the resulting file manifest.
2. Confirm both JSONs, SVGs, applicable notices and required content dependencies are included.
3. Confirm developer files/raw import sources are absent from the submission artifact.
4. Install in a clean project: open → compose → preview → bake.
5. Check baked-material behavior in a packaged game and review relevant log warnings.
6. Finish changelog, screenshots/video, demo/docs/support links and submission metadata.

No commands, git operations, tests, network checks or asset deletions were performed.
The obsolete Lucide/Feather notice was subsequently deleted with explicit owner approval.
