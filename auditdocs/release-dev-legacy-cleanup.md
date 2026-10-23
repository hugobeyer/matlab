# Release cleanup: developer tools, legacy names, and temporary leftovers

Source audit: 2026-10-23. **Inventory and proposed actions only; no product code changed.**

## Scope and decisions

- Reviewed Runtime, Shaders, Editor UI/style/services, packaging config, and text/tool resources.
- **Keep JSON parameter binding, shipped ranges/defaults, and normal parameter controls.**
- Clarification: **UI STYLE stays, developer-only. Parameter range/definition editing also stays, developer-only.**
- The binding/data-consumption system remains outside removal scope; its developer editing tools are now included.
- Binary assets and repository-excluded paths were not inspected.
- Findings are source-based; no commands, builds, tests, diagnostics, or Unreal launches ran.
- Locations are audit-time line numbers; later edits can move them.
- Keyword matches alone are not proof of dead code or unnecessary functionality.

Action legend:

| Action | Meaning |
|---|---|
| **DELETE UI TEXT** | Mark the stale label, suffix, paragraph, or redundant readout for deletion. Keep its working control/action unless separately approved. A suggested replacement is copy-only. |
| **DEV-ONLY** | Keep useful engineering tooling, but exclude its entry points and execution from customer builds. |
| **RETIRE CANDIDATE** | Obsolete/dead-looking data or implementation. Needs approval and coordinated reader cleanup, not a hidden compatibility stub. |
| **SOURCE COPY** | Historical implementation narration, not live user-facing UI. Clean comments without removing behavior. |
| **PACKAGE-EXCLUDE** | Keep privately; omit from distributed plugin payload. |
| **KEEP** | Real artist/product functionality, even if its internal name contains Debug, Legacy, or Temporary. |

Path keys below, all relative to the plugin root:

- **EW** = `Source/MixtormatEditor/Private/Widgets/`
- **ES** = `Source/MixtormatEditor/Private/Style/`
- **EV** = `Source/MixtormatEditor/Private/Services/`
- **RP** = `Source/MixtormatRuntime/Public/`
- **RI** = `Source/MixtormatRuntime/Private/`
- **SP** = `Source/MixtormatShaders/Private/`
- **SH** = `Shaders/Private/`

## 1. Priority: real developer tooling currently in the product

| ID | Item and exact location | Current exposure | Listed action |
|---|---|---|---|
| D01 | **UI STYLE** button: `EW/SMixtormat_Shell.cpp:214–215,360–365`. | Visibility depends on finding shipped source directories, not a private developer build. | **DEV-ONLY** button and its handler. |
| D02 | `OpenThemePanel()`, title **Mixtormat — UI Style**: `EW/SMixtormat_Theme.cpp:106–135`. | Handler constructs the window without a developer-build guard. | **DEV-ONLY** window and tool-specific state. Do not gate the entire file: it also owns workspace destruction/cleanup. |
| D03 | Theme-authoring explanation, **Save / Load / Reset All**, validation/path readouts: `EW/SMixtormatThemePanel.cpp:209–211,249–290`. | Available inside UI STYLE; useful internally, not material-authoring controls. | **DEV-ONLY**, not deletion of the theme rendering system. |
| D04 | Locator UI: `EW/SMixtormat.cpp:231–249`; `ES/MixtormatStyleLocator.h:13–30`; `ES/MixtormatLocatorOutline.*`. | Locator wrapper/outline constructed for every workspace. | **DEV-ONLY** locator-only wrapper, outline, traversal/ticking. Keep the ordinary top bar. |
| D05 | Built-in library rebuild gear: `EW/SMixtormat_Library.cpp:767–772`; `EW/SMixtormat_Document.cpp:43–50`. | Source-directory visibility check; handler calls `ReimportShippedLibrary()` directly. | **DEV-ONLY** UI, handler, and reconstruction service. |
| D06 | **Developer: Refresh / Reimport from Source**: `EW/SMixtormat_Library.cpp:830–849,869–918`. | Built-in surface/source-basename/source-directory checks, not a developer build. Includes result dialog and suppression checkbox. | **DEV-ONLY** menu, callback, and source-reimport implementation. |
| D07 | **Developer: Delete Material…**: `EW/SMixtormat_Library.cpp:851–857,1157–1162`. | Built-in deletion eligibility plus `IsDeveloperMetaEnabled()` in the handler. | **DEV-ONLY** through the private tool build gate. Keep its destructive operation separate from parameter-authoring permissions; preserve normal binding/data consumption. |
| D08 | Built-in reconstruction/reimport services: `EV/MixtormatSurfaceImporter.cpp:751–1025`. | Developer PNG-source workflows compiled into the editor module. | **DEV-ONLY** these methods, not the shared/customer importer. |
| D09 | Built-in asset/source deletion: `EV/MixtormatSurfaceImporter.cpp:1028–1250`. | Can delete built-in assets and source PNGs; metadata check is not compile-time isolation. | **DEV-ONLY** declaration, implementation, and callers. Destructive execution requires separate approval. |
| D10 | Plugin-destination import branches: `EV/MixtormatSurfaceImporter.cpp:1363–1374,1466–1477`. | `bUsePluginDestination=true` writes into the plugin library. | **DEV-ONLY** plugin-library mutation route; **KEEP** project-destination imports. |
| D11 | `Mixtormat.MigrateAssets [Apply]`: `Source/MixtormatEditor/Private/MixtormatEditorModule.cpp:43–51,128–138`. | Registered with `ECVF_Default` when source directories exist; `Apply` runs migration. | **DEV-ONLY** registration, callback, includes, and command state. |
| D12 | MaterialLab/ML → Mixtormat/DA migration service: `EV/MixtormatAssetMigration.cpp:47–89,119–136,301–328`; `.h:7–27`. | One-time internal asset renaming, not customer import. | **DEV-ONLY** service; **PACKAGE-EXCLUDE** dedicated implementation/header after guarding dependencies. |
| D13 | `Mixtormat.DisableComposeCache`: `SP/MixtormatGpuCompositor.cpp:59–64,1866–1868`. | Default 0, `ECVF_Default`, no private build guard. | **DEV-ONLY** registration and bypass read. Keep normal caching. |
| D14 | Moss **Preview** solver selector: `EW/Inspector/MixtormatInspectorGenerators.cpp:2953–2955`; `RP/MixtormatGeneratorTypes.h:403–414,437–439`. | Normal inspector and reflected editable field. Options: Moss, Chemical A/B, Activation, Arrival, Boundary, Biomass. | **REMOVE AUTHORING CONTROL / VIEWPORT-ONLY INSPECTION**. User clarified that this dropdown should not exist as an authored Moss parameter. Use the existing viewport-output mechanism for inspection, without replacing composed Height. Keep Moss relief and detail normals. |

### Moss clarification: keep relief/normals; move solver inspection to viewport outputs

**Moss and Skin changing Height is intentional, not a release defect.** Moss bulges/thickness belong
in Height. The final-normal path derives slope from finished Height and adds authored detail normals
(`SP/MixtormatGpuComposePipeline.cpp:1567–1571`). Preserve the little-leaf detail normals too.

The separate issue is the authored **Preview** dropdown, not normal Height-to-normal behavior.
Its current diagnostic selection changes composed/published Height rather than only viewport display:

1. `SP/Compositing/MixtormatSurfacingGather.cpp:35–40` clamps the selection, but does not dev-gate it.
2. `SP/MixtormatGpuGeneratorPasses.cpp:4075` binds it to the shader.
3. The Moss height/preview shader stage can substitute a diagnostic field for Height. Shader line numbers have moved since the initial audit.
4. `SP/MixtormatGpuGeneratorPasses.cpp:5211–5223` skips normal height scaling/normalization for that mode and applies the result to composition.
5. The published **Height** also receives `Raw.Height` at `:5231`.

**User-confirmed direction:** remove the Moss Preview dropdown as an authoring parameter.
Use the existing viewport-output mechanism for solver inspection, not a second preview system.
Check which diagnostic fields are already published before deciding what wiring is needed.
Inspection should not replace composed or published material Height, or affect baking.

Retire the Preview field/enum and its gather, cache, CPU/shader binding, and Height-substitution readers
together; merely hiding the row would leave authored values affecting output. The composition path
also reads `Child.Surfacing.Moss`, so this is not a gather-only change. Remove the obsolete tooltip
with the dropdown. Preserve normal Moss generation, Height Scale/normalization, Skin relief, and all
detail-normal behavior. No product changes have been implemented by this document.

### Confirmed policy: keep UI STYLE and parameter-range authoring for developers only

**UI STYLE is not marked for removal.** Keep its panel, tools, and private access.
Customer builds retain the authored appearance/theme loading, but no UI STYLE editing entry point.
Its current source-directory check is not sufficient developer isolation (D01–D04).

The parameter metadata/range tooling is also **not correctly private-dev-only today**:

| ID | Evidence | Current behavior / listed action |
|---|---|---|
| D15 | `Source/MixtormatEditor/Private/UI/Parameters/MixtormatParameterUiMeta.cpp:18–23,150–153`. | `Mixtormat.Developer.ParameterMeta` defaults to **1**. `IsDeveloperMetaEnabled()` checks only that public console variable. **DEV-ONLY** build-gate its availability; defaulting a customer-accessible CVar to 0 alone is insufficient. |
| D16 | `EW/SMixtormat_Parameters.cpp:702–799`. | The flag exposes **Parameter Info**, **Edit Authoring Setup…**, **Save to Plugin Defaults**, **Revert Unsaved Changes**, **Restore Compiled Defaults**, **Override UI Range…**, **Remove UI Range Override**, and **Copy Parameter Address**. **DEV-ONLY** this developer menu and its callbacks, not the normal binding/reference/driver menu. |
| D17 | `EW/SMixtormat_Parameters.cpp:1172–1233`; `Source/MixtormatEditor/Private/UI/Parameters/MixtormatParameterUiMeta.cpp:186–216`. | Session-only Min/Max override panel checks the target, not developer identity; setters and range-override resolution are not independently gated. **DEV-ONLY** panel, mutation APIs, override storage, and override participation in resolution. Preserve normal reflected/shipped range resolution. |
| D18 | `EW/SMixtormat_Parameters.cpp:1259–1323`; `Source/MixtormatEditor/Private/UI/Parameters/MixtormatParameterAuthoring.h:71–84`; `.cpp:299–320,362–384,742`. | Persistent authoring panel writes pending definition changes; menu actions can save/restore plugin defaults. No private developer-build guard found in the mutation service. **DEV-ONLY** pending-edit/mutation/write tooling and its UI; **KEEP** reading the shipped database, applying shipped defaults, and normal binding evaluation. |
| D19 | `Source/MixtormatEditor/Private/UI/Parameters/MixtormatParameterUiMeta.cpp:231–255`. | Literal/UPROPERTY mismatch reporting emits **Mixtormat dev** warnings with per-key tracking. **DEV-ONLY** engineering report/state; preserve metadata-based slider resolution. |

**Range distinction:** developers edit parameter-definition UI Min/Max, snap, defaults, and clamps.
Customers still use the resulting ranges, resets, defaults, ordinary sliders, bindings, and drivers.
Do not gate artist-owned material ranges/remaps or driver settings merely because they also have Min/Max.

`MixtormatParameterAuthoring.h:17–27,67–97` explicitly separates the shipped database from pending
developer edits. Preserve that read path and `Config/MixtormatParameterAuthoring.json` in customer builds.
The existing comments equating editor-only code with developer-only code are misleading: customers
use the editor module too. This clarification is a documentation change, not an implemented gate.

## 2. UI-only text and space to mark for deletion

These are deletion marks in this document, **not implemented deletions**.
Keep working controls, semantic titles, and useful output rows.

| ID | Text / location | Listed action |
|---|---|---|
| U01 | **Debug:** prefix on Feature, Height Blend, Contact AO, Border Normal, Layer Mask, Stain, Runoff, Child Output, Layer UV; generic **Debug** fallback. `EW/SMixtormatPreviewViewport.cpp:1839–1861`. Displayed through `EW/SMixtormat_PreviewControls.cpp:790–801`. | **DELETE UI TEXT** prefix/fallback engineering wording. Keep artist preview modes. Use the actual output name for Child Output when available. |
| U02 | **Show diagnostic / Hide diagnostic / Diagnostic active**: `EW/SMixtormat_PreviewHints.cpp:200–211,228–239`. | **DELETE UI TEXT** generic diagnostic wording; describe mask/scalar/flow/UV preview instead. Keep W/E behavior. |
| U03 | **LEGACY TREATMENT** wrapper heading: `EW/Inspector/MixtormatInspectorIds.cpp:1250`. | **DELETE UI TEXT** historical heading. Keep the existing UV Variation, Relief, and Edges cards while their behavior remains supported. Do not silently remove parameters. |
| U04 | **Legacy Pattern UV/relief settings are preserved for compatibility. New setups should use…**: same file `:1171–1175,1262`. | **DELETE UI TEXT** inline compatibility paragraph and duplicated tooltip. The controls are live; see R01. |
| U05 | **Automatic (Legacy)**: `RP/MixtormatLayerTypes.h:60–67`; editable HeightSource at `:547–548`. | **DELETE UI TEXT** `(Legacy)` only. Keep automatic packed Height → Mask → Constant selection (`SP/Compositing/MixtormatLayerGather.cpp:254–269`). |
| U06 | **SDF Inflation (Legacy)**: `RP/MixtormatGeneratorTypes.h:416–422`; corresponding Cushion Solver help: `EW/Inspector/MixtormatInspectorGenerators.cpp:3088–3093`. | **DELETE UI TEXT** `(Legacy)` and historical “legacy Jump-Flood” narration. Keep the active solver and describe its result. |
| U07 | **the old OVER / the old BLEND** in operation help: `EW/SMixtormat_Inspector.cpp:366`. | **DELETE UI TEXT** historical aliases; retain current operation explanations. |
| U08 | **which is what every layer did before this control existed**: `EW/SMixtormat_Inspector.cpp:802`. | **DELETE UI TEXT** history sentence; keep Blend. |
| U09 | **Editor preferences for Mixtormat debug visualization.**: `Source/MixtormatEditor/Private/MixtormatEditorModule.cpp:87`. | **DELETE UI TEXT** debug-only description. Suggested copy: “Mixtormat preview and bake preferences.” |
| U10 | **Real-time Preview · {0} · SM6**: `EW/SMixtormat_Shell.cpp:959–968`. | **DELETE UI TEXT** “Real-time Preview” and “SM6” boilerplate. This is not a measured performance indicator. Quality can remain in its actual control. |
| U11 | **Default · Studio AO**: same file `:962`; quality help at `EW/SMixtormat_PreviewControls.cpp:478` says viewport AO is off. | **DELETE UI TEXT** misleading AO wording; suggested quality label “Studio.” Do not remove final-material AO. |
| U12 | **Layers {0}** status readout: `EW/SMixtormat_Shell.cpp:980–985`. | **DELETE UI TEXT** redundant count if reclaiming status-bar space. Keep layer stack and actions. |
| U13 | **OUTPUTS · {child name}** header: `EW/SMixtormat_PreviewHints.cpp:347–355`. | **DELETE UI TEXT** candidate for reclaiming overlay space. Keep preview/copy output rows at `:359–406`; this is presentation preference, not proven dead UI. |
| U14 | **isolated channel debug preview**: `EW/SMixtormatWelcomeCard.cpp:150`. | **DELETE UI TEXT** “debug”; use “isolated material-channel preview.” |
| U15 | **press Tab to open quick controls** and channels via Output strip: same file `:359`. | **DELETE UI TEXT** stale guidance. Q opens quick controls; V cycles material channels; Shift+V resets diagnostics. The strip is selected-module outputs. Handlers: `EW/SMixtormatPreviewViewport.cpp:443–450,507–519`. |
| U16 | **Baking generates 7 coordinated assets**: `EW/SMixtormatWelcomeCard.cpp:366`. | **DELETE UI TEXT** stale fixed-count claim. The bake dialog handles ten named outputs (`EW/Dialogs/SMixtormatBakeSettingsDialog.cpp:308–334`); describe selected outputs instead. |
| U17 | **avoiding … texture resolution limits during authoring**: `EW/SMixtormatWelcomeCard.cpp:336`. | **DELETE UI TEXT** overstatement. Composition still offers 1K/2K/4K (`EW/SMixtormat_PreviewControls.cpp:826–844`). |
| U18 | **legacy fuzz influence** in FuzzColor tooltip: `RP/MixtormatLayerTypes.h:759–760`. | **DELETE UI TEXT** historical qualifier; retain shared fuzz color and supported material behavior. |
| U19 | Bake warning about **legacy channels/global fuzz** and upgrading a reviewed duplicate: `Source/MixtormatEditor/Private/Compositing/MixtormatBakeService.cpp:856–859`. | **DELETE UI TEXT** internal migration narration only. Preserve the useful warning that the selected parent cannot render exported per-pixel optical maps. Not a dev-only failure. |
| U20 | **Return … (V)**: `EW/SMixtormat_PreviewControls.cpp:848–926`, in `BuildPreviewOutputControls()`. | **DELETE UI TEXT** stale reset shortcut if this helper remains. Reset is Shift+V. No caller found in audited scope; not confirmed currently visible clutter. |
| U21 | **Obsolete Flow**: `EW/Layers/MixtormatLayerChildren.cpp:1561`. | **DELETE UI TEXT** unhelpful historical wording, not its warning function. If the unsupported subtype remains, show unsupported-state/replacement guidance; coordinated retirement is R02. |

### Inactive UI STYLE parameters are worse than just bad names

| ID | Evidence | Listed action |
|---|---|---|
| U22 | **Legacy Shadow Offset (Inactive)** and **Legacy Shadow Radius (Inactive)**: `ES/MixtormatThemeSchema.cpp:696–697`; fields `ES/MixtormatTheme.h:821–823`. | **RETIRE CANDIDATE** inactive slider registrations and underlying unused fields. **DELETE UI TEXT/control rows** from the authoring panel once approved; they tune no current shadow. Hiding all UI STYLE already keeps them away from customers, but leaves internal clutter. |
| U23 | `LeftOverlayWidth`, `LeftOverlaySurfaceOpacity`: `ES/MixtormatTheme.h:826–827`; defaults in `ES/MixtormatTheme.cpp:400–401`. | **RETIRE CANDIDATE** retired Layers pop-out fields. Source search found declarations/defaults, not active widget consumers or schema sliders. Do not remove Inspector overlay behavior. |

`AgentDocs/UI.md:470–473` describes rail shadow fields as retained serialized state.
The current schema nevertheless exposes them as explicitly inactive sliders. Source wins over that description.
These are theme settings, **not the excluded JSON parameter-binding system**.

## 3. Obsolete parameters and compatibility payloads

These are **not approved removals**. No dev gate should be used to disguise a broken or retired feature.
When approved, remove obsolete fields and their direct/reflected readers together; do not add migration
stubs or redirects solely for this unshipped plugin's development history.

| ID | Candidate / exact evidence | Dependency and required decision |
|---|---|---|
| R01 | Embedded Pattern UV/Relief/Edges: `RP/MixtormatIdTypes.h:392–493`. | **Live**, not dead: gather `SP/Compositing/MixtormatIdGather.cpp:327–378`; UV path `SP/MixtormatGpuCompositor.cpp:604–611,631–707`; relief work `SP/MixtormatGpuReliefIdPasses.cpp:649–704,881–885`; Height shader `SH/MixtormatRampIdRelief.usf:263–265,294`. Delete legacy copy first. Retire functionality only with an approved UV/Relief From IDs replacement plan. Preserve IDs, Gap masks, and intrinsic pattern orientation. Defaults include nonneutral Height/Gap/roughness settings. |
| R02 | Obsolete Behavior `FlowField`: `RP/MixtormatBehaviorTypes.h:63–70,255–260,288–362`. | Creation blocked at `EW/Layers/MixtormatLayerActions.cpp:1965–1969`, but runtime validation, source eligibility, and gather still accept it. GPU branch falls through: `SP/MixtormatGpuGeneratorPasses.cpp:2751–2758`. Retire subtype, old settings, and dependent readers together. Preserve current Noise/native Flow. Details below. |
| R03 | Six deprecated Stain defaults: `RP/MixtormatEffect.h:114–138`. | `DefaultStainColor`, `DefaultStainRoughness`, `DefaultStainHeightInfluence`, `DefaultStainHeightWarp`, `DefaultStainHeightBias`, `DefaultStainHeightContrast`. Deprecated, not editable; source says nothing reads them. No direct render reader found. Reflection still matters. |
| R04 | Strata `RampShape`: `RP/MixtormatGeneratorTypes.h:120–122`. | Serialized deprecated property; replacement is `LedgeWidth`. No direct shader-module reader found. |
| R05 | `RockHeightMode` and enum Raw / Analytic / Measured: `RP/MixtormatGeneratorTypes.h:178–184,308–311`. | Field retained for loading, not a normal inspector control; enum still Blueprint-visible. Name remains in cache exclusion list at `SP/Compositing/MixtormatGeneratorGather.cpp:168–170`. |
| R06 | Generator-local `HeightBlend`: `RP/MixtormatGeneratorTypes.h:1824–1828`. | Bare serialized property explicitly marked obsolete. **Not** the live layer/child Height Blend modules. |
| R07 | Authored Color Ramp `OutputName`: `RP/MixtormatGeneratorTypes.h:1228–1232`. | Gather substitutes canonical `Color` at `SP/Compositing/MixtormatGeneratorGather.cpp:1056`. Retire authored slot only; keep live render/publication output names. |
| R08 | Behavior `CarveMode`, enum Groove / Deposit: `RP/MixtormatBehaviorTypes.h:274–280,383–387`. | Editable field, gather at `SP/Compositing/MixtormatGeneratorGather.cpp:618`; render slot `SP/MixtormatGpuCompositorInternal.h:1172`. Actual Sculpt uses signed Depth at `SH/MixtormatBehaviorSculpt.usf:98–100,119`. No live GPU reader of the old selector found. |
| R09 | Standalone Behavior `CarveWidth`: `RP/MixtormatBehaviorTypes.h:433–437`. | Gather `SP/Compositing/MixtormatGeneratorGather.cpp:688–689`, render slot `SP/MixtormatGpuCompositorInternal.h:1196`; no GPU consumer found. Actual Sculpt uses separate live `Flow.Width` at `SP/MixtormatGpuGeneratorPasses.cpp:2138–2142`. Preserve that width. |
| R10 | Generator `HeightSource`, `WarpSource`: `RP/MixtormatGeneratorTypes.h:1805–1821`. | Editable input slots and group remapping at `RI/MixtormatLayerGroups.cpp:355–357`; no shader-module consumer found. Confirm intended feature ownership before retirement. This is not a recommendation to remove Behavior sockets or parameter binding. |
| R11 | `ErosionGravityForce`, `ErosionSmoothing`: `RP/MixtormatEffect.h:587–607`. | **KEEP live behavior / semantic naming review**, not dead data. Gather `SP/Compositing/MixtormatEffectGather.cpp:41,45`; bind `SP/Effects/MixtormatErosionPasses.cpp:370–374`; shader `SH/MixtormatErosion.usf:162–165,260`. “Smoothing” now affects horizon feathering. Removing it changes output. |
| R12 | Erosion direct placement-mask compatibility route: `SP/Effects/MixtormatErosionPasses.cpp:181–189,385–388`. | Still active when no scoped mask exists. Retire only with an explicit decision on supported authored setups; not engineering diagnostics. |
| R13 | Document-wide `FuzzInfluence` / old-parent optical branch: `RP/MixtormatLayerTypes.h:752–760`; `RI/MixtormatMaterial.cpp:135–175`; `SP/MixtormatGpuCompositor.cpp:2379–2401`. | **KEEP pending supported-parent decision**. Real recursive/runtime readers and bake behavior remain. Not safe to delete because comments call it legacy. Shared FuzzColor also feeds optical composition (`SP/Compositing/MixtormatLayerGather.cpp:285–288`). |
| R14 | Theme single-backend `FontFamily` token and family slots: `ES/MixtormatDesignTokens.h:399–401`; `ES/MixtormatTheme.h:1006–1008,1082`; `ES/MixtormatResolvedStyle.h:252`; copy at `.cpp:421`. | **RETIRE CANDIDATE** inert selection plumbing. One native font backend, no current FontFamily picker found. Do not remove actual typography roles, weights, or native font resolution. |
| R15 | Theme loader old Layers → Sources section lookup: `ES/MixtormatThemeSchema.cpp:1045–1055`; old string-valued choices at `:1090–1103`. | **RETIRE CANDIDATE** pre-release theme compatibility routes, not binding JSON. Confirm the current authored theme format before approving removal. Do not dev-gate ordinary theme loading. |
| R16 | Enum redirects Chipping → Breakup and Fracture → Cracks: `Config/DefaultMixtormat.ini:1–3`. | **RETIRE CANDIDATE** unshipped-history redirects. Asset dependencies were not inspected; approve coordinated content/schema cleanup before deleting. Do not introduce new compatibility scaffolding. |

### R02: obsolete Flow payload and readers

Old editable Behavior payload:
`Mode`, `FlowSource`, `FlowAmount`, `FlowTangent`, `FlowAngle`, `GravitySurfaceFollow`,
`GravityDeflection`, `FlowBend`, `FlowSeed`, `FlowRadius`, `FlowSmooth`, `Reach`, `Feather`,
`FlowOffsetAlong`, `FlowOffsetAcross`, `ShapeOffset`, `Bulge`.

- Declarations: `RP/MixtormatBehaviorTypes.h:288–362`.
- Runtime validation: `RI/MixtormatChildScope.cpp:286–291`.
- Flow-source eligibility: `RI/MixtormatOutputReference.cpp:673–687`.
- Admission/gather: `SP/Compositing/MixtormatGeneratorGather.cpp:551–610`.
- Retained render fields: `SP/MixtormatGpuCompositorInternal.h:1146–1166`.
- Dependent Feather parent lookup/publication: `SP/MixtormatGpuGeneratorPasses.cpp:2683–2713`.
- Stale editor creation enum: `EW/SMixtormat.h:95`.

No GPU consumers of those old Behavior solver values were found. Some fields still affect
validation/references, so “no rendering consumer” does not mean globally unread.
Preserve working automatic Flow, Noise Flow, Warp, trace controls, Sculpt, and live Feather.

**Reflection caveat for all parameter retirements:**
`SP/Compositing/MixtormatComposeHash.cpp:35–53,123–125` hashes reflected properties without
filtering deprecated properties. Removing apparently inert fields also changes composition hashes.

## 4. Temporary helpers and dormant shader work

| ID | Item / exact location | Listed action and constraints |
|---|---|---|
| H01 | `MixtormatFont` bridge: `ES/MixtormatFont.h:8–29`, `.cpp:8–41`. | **RETIRE CANDIDATE** forwarding shim. Comment says “TEMPORARY BRIDGE — delete at Stage 10” and claims ~200 callers; current targeted source search found only its own include/definitions, no external `MixtormatFont::` consumers. This is stale scaffolding, not something to dev-gate. Keep `MixtormatTypography`. |
| H02 | Height Feather old Gaussian/arrival permutations: `SP/MixtormatGpuGeneratorPasses.cpp:1059,2334–2335,2377–2383,2391–2410`; `SH/MixtormatBehaviorFeather.usf:151–238`. | **RETIRE CANDIDATE** unreachable Height stages 0–3 and 8 and exclusive resources. Height is hardcoded Eikonal; current dispatch uses 4–7. Keep Flow Feather Blur (`:2256,2297–2307`) and the shared enum. |
| H03 | Generator Bundle old Structural Warp stage 8: `SH/MixtormatGeneratorBundle.usf:174–185`; permutations `SP/MixtormatGpuGeneratorPasses.cpp:989`; live stage selection `:1846–1873`. | **RETIRE CANDIDATE** inactive stage/permutation; no stage-8 dispatch found. Preserve live remapping algorithms. |
| H04 | Old coherent Flow shader pair: `SH/MixtormatFlow.usf:5–24`, `SH/MixtormatFlow.ush:3–23`. | **RETIRE CANDIDATE** unregistered/unreferenced pair in audited source. Do not confuse with NativeFlow, FlowTrace, FlowPreview, FlowWarp, or Flow Feather. File deletion requires approval. |
| H05 | `BuildPreviewOutputControls()`: `EW/SMixtormat_PreviewControls.cpp:848–926`. | **RETIRE CANDIDATE** helper if a complete caller check confirms it is unused. No caller found in audited scope. Do not remove current output-strip actions or normal channel controls. |
| H06 | `RequestedTypeface()` / `ResolvedTypeface()`: `ES/MixtormatFont.h:17–23`, `.cpp:10–21`. | Included in H01 retirement. Both return the same display-only “Unreal Default”; no requested/resolved backend distinction remains. No UI readout consumer found. |

### Historical source naming: clean narration, not functionality

| ID | Location | Listed action |
|---|---|---|
| C01 | `ES/MixtormatThemeStore.h:22–23,61–62`: claims persistence/UI STYLE and recipe adoption are future stages. | **SOURCE COPY** delete stale Stage 9 / Stage 5 narration; the panel and persistence already exist. |
| C02 | `EW/SMixtormatThemePanel.h:12–13`: Stage 9 / Stage 10 staging description. | **SOURCE COPY** describe current ownership. |
| C03 | `EW/SMixtormatPreviewViewport.h:44–46`: “Temporary V-key diagnostic cycle.” | **SOURCE COPY** current V-cycle is an artist channel feature; keep it. |
| C04 | `SP/MixtormatGpuNoisePasses.cpp:174–175,204–205,662–666`: P2 slope/composition not connected. | **SOURCE COPY** obsolete claim. Height binding exists at `:368–370`; Flow composition executes in `SP/MixtormatGpuGeneratorPasses.cpp:2607–2618`. |
| C05 | `SH/MixtormatCliffStrata.usf:246,253,260,267,277,287`: “Prototype Shape iteration” headings. | **SOURCE COPY** keep active stages; replace historical headings with semantic stage names. |
| C06 | `EV/MixtormatPaths.h:9–10`: claims paths still use MaterialLab. | **SOURCE COPY** implementation already uses Mixtormat (`.cpp:10–11,57–59`). |
| C07 | `SP/MixtormatGpuCompositorInternal.h:1658–1668`; `SP/MixtormatGpuReliefIdPasses.cpp:451–452,539–540`; `SH/MixtormatRegionFields.usf:78`: `LegacyFour`. | **SOURCE COPY / semantic rename candidate** four-neighbor boundary policy, not an obsolete algorithm. Preserve invalid-neighbor handling. |
| C08 | `SP/MixtormatGpuCompositorInternal.h:1352–1353,1379–1384`: `LegacyLocalCentre`, `LegacyLocalOrientation`. | **SOURCE COPY / semantic rename candidate** affine-local estimates still consumed by remap stages 5/6 (`SP/MixtormatGpuGeneratorPasses.cpp:1847–1849`). No behavior change. |
| C09 | Prototype/CSS-origin commentary in `ES/MixtormatCompositing.h:9,26`, `MixtormatDesignTokens.h:193–215,272–334,383,547`, `MixtormatGroupButtonTokens.h:6`, and recipe comments. | **KEEP rationale where useful**; optional source-copy polish only. Prototype provenance alone does not make shared styling code temporary or justify deleting it. |

## 5. Engineering logs, instrumentation, and operational controls

| ID | Item / exact location | Listed action |
|---|---|---|
| L01 | Overlay path/status logs and persistent log latches: `SP/MixtormatGpuComposePipeline.cpp:32–37,1613–1627,1642–1654,1707–1713`. | **DEV-ONLY** routine resampling/path chatter. Preserve actionable missing-output warnings for support. Gate log-only state too, not rendering. |
| L02 | Cache detail logs: `SP/MixtormatGpuComposePipeline.cpp:252,280`; `SP/MixtormatGpuPatternPasses.cpp:1058–1059`. | Already Verbose/VeryVerbose. **DEV-ONLY candidate** if stricter release isolation is wanted; not an on-screen clutter issue. Keep cache logic. |
| L03 | CPU scopes `SP/MixtormatGpuCompositor.cpp:1700,1889,1919`; GPU stat families `SP/MixtormatGpuComposePipeline.cpp:15–28`. | **KEEP engine-controlled profiling**. These are not user UI. No blanket rendering gate or unconditional deletion. |
| L04 | Assertions, e.g. clamp helper `SP/MixtormatGpuGeneratorPasses.cpp:55–60`, semantic checks `:1840–1842`. | **KEEP engine-controlled checks** and independent clamps/failure behavior. Assertions are not accidental release labels. |
| L05 | `Mixtormat.ComposeCacheBudgetMB`: `SP/MixtormatGpuCompositor.cpp:66–71,2338–2339`. | **KEEP** operational memory policy, not solver diagnostics. If console exposure changes, preserve an owned budget. |
| L06 | Runtime automation file: `RI/MixtormatGeneratorInputTests.cpp:9–14,119`. | **DEV-ONLY already guarded** by `WITH_DEV_AUTOMATION_TESTS`. It sits outside a `Tests/` directory, so existing folder filter does not specifically exclude its source file. Add a targeted packaging exclusion if customer source should omit it; preserve private tests. |

## 6. Files/helpers that belong only in development payloads

`Config/FilterPlugin.ini:16–18` already excludes **all `Tools/`**.
Do not delete these utilities merely to clean the customer package.

| ID | File / exact location | Listed action |
|---|---|---|
| P01 | `Tools/CreateBuildEditorShortcut.vbs:3–8`. | **DEV-ONLY / PACKAGE-EXCLUDE** machine-specific build shortcut; legacy host-project wording is private. |
| P02 | `Tools/CreateMatLabShortcut.vbs:3–8`. | **DEV-ONLY / PACKAGE-EXCLUDE** local host-project launcher, not customer product branding. |
| P03 | `Tools/bal.py:3–21`. | **PACKAGE-EXCLUDE already**; optional private rename to `check_bracket_balance.py`. No rename implemented. |
| P04 | `Tools/test_generator_bundle_remap.py:1–6`. | **DEV-ONLY / PACKAGE-EXCLUDE already** offline tests. |
| P05 | `Tools/test_structural_composition.py:1–13`. | **DEV-ONLY / PACKAGE-EXCLUDE already** static/source-contract tests. |
| P06 | `Tools/weathering-preview.html:6–15`. | **DEV-ONLY / PACKAGE-EXCLUDE already** browser experiment with external CDN dependency. |
| P07 | `Tools/upgrade_surface_optics.py:1–6,18–29`; `.md:233–256`. | **DEV-ONLY / PACKAGE-EXCLUDE already** master-preparation tool and internal upgrade instructions. Preserve resulting product/material functionality. |
| P08 | `Tools/strip_import_metadata.py:14–28,60–67,79`. | **DEV-ONLY / PACKAGE-EXCLUDE already** staging sanitation; executes on load. Do not run against the working asset library without approval. |
| P09 | `Tools/__pycache__/` directory. | **PACKAGE-EXCLUDE already** under Tools; generated private cache, not release content. Not inspected or deleted. |
| P10 | `Resources/Icons/_source_sheet.webp` (file inventory only). | **PACKAGE-EXCLUDE candidate** authoring sheet; no source-code reference found. No explicit filter exclusion. Confirm no external/resource consumer before excluding. Image contents were not read. |
| P11 | `Resources/Icons/beh-deform.svg:7–9`: copied document identity/export name. | **SOURCE COPY** clean embedded source metadata, preserve icon path/artwork. |
| P12 | `Resources/Icons/beh-flow.svg:8–9`, `beh-gravity.svg:8`, `beh-noise.svg:8–9`, `beh-push.svg:8–9`, `beh-warp.svg:8–9`. | **SOURCE COPY** stale SVG document/export names; not visible material labels. Preserve resource filenames. |
| P13 | `Resources/Icons/filter.svg:8–9`, `frame.svg:8–9`, `mixtormat-logo.svg:8`. | **SOURCE COPY** stale export targets/inconsistent embedded branding. Preserve product resources. |

### Packaging boundaries already present

- `Config/FilterPlugin.ini:9` explicitly includes `Docs/ThirdPartyNotices.txt`; keep notices.
- `:12–14` excludes old/working Markdown docs under Docs.
- `:20–26` excludes redundant raw import art; this audit did not inspect binary content.
- `:28–30` excludes source `Tests/` directories, not every test-named source file.
- UAT defaults listed at `:2–3` include Source, Shaders, Resources, Content, and third-party binaries.
- Root agent/audit/prototype docs are not explicit extra includes; a manual whole-folder ZIP is different.
- This new `auditdocs/` inventory belongs in the repository, not the customer plugin payload.
- Package filters do not disable code already compiled into shipped editor binaries.
- Package-excluding developer headers/files is safe only after customer source can rebuild without them.

## 7. What must remain user-facing

| Feature / evidence | Why it is not development clutter |
|---|---|
| Material channels, mask/scalar/Flow/UV views, Region IDs; `EW/SMixtormat.cpp:155–174`; `Source/MixtormatShaders/Public/MixtormatGpuCompositor.h:35–82`. | Core artist inspection, including W/E/R/I/V. Internal Debug names are not a reason to remove it. |
| Height Blend, Contact AO, Border Normal eye controls; `EW/Inspector/MixtormatInspectorHeight.cpp:65–128`. | Useful scoped coverage visualization. |
| Output-strip preview and copy rows; `EW/SMixtormat_PreviewHints.cpp:359–406`. | Real published outputs; preserve disabled states for unavailable preview actions. |
| Skin/Moss material and growth fields; `RI/MixtormatSurfacingTypes.cpp:218–237`. | Published physical/material outputs, not automatically developer-only because inspection is possible. |
| Behavior gate inspection; `SP/MixtormatGpuDebugPreviewPasses.cpp:256–283`. | Artist visualization of scoped mask × Influence. |
| Accumulated Flow snapshots; public compositor header `:35–40`; `SP/MixtormatGpuGeneratorPasses.cpp:5818–5819`. | Demand-driven, display-only authoring aid. |
| Feather Blur / Eikonal choices under scoped Noise Flow; `EW/Inspector/MixtormatInspectorGenerators.cpp:2481–2501`. | Working context-specific behavior. Do not retire Flow Blur with dormant Height permutations. |
| Moss Cushion Solver choices; `RP/MixtormatGeneratorTypes.h:511–518`; `SP/MixtormatGpuGeneratorPasses.cpp:4262–4289`. | Both SDF and deposition paths execute. Only the legacy suffix is stale. |
| Preview master, bake resolution, output path; `Source/MixtormatEditor/Public/MixtormatEditorSettings.h:73–88`. | Real artist settings; `DeveloperSettings` is an engine settings facility, not proof of engineering-only content. |
| `DefaultBakeAASamples`; same header `:81–85`. | Already Config-only, not EditAnywhere; no live settings row to delete. Keep hidden pending feature completion. |
| Safe Refresh Library; `EW/SMixtormat_Library.cpp:75–82,758–763`. | Refreshes lists/galleries, not destructive source reconstruction. Keep distinct from the dev gear. Current source-folder visibility restriction is not a reason to remove it. |
| Q → ACTIONS; `EW/SMixtormat_PreviewControls.cpp:1004–1026`; `EW/Layers/MixtormatLayerMenus.cpp:108–239`. | Implemented creation menu, not a placeholder. |
| User imports; `EV/MixtormatSurfaceImporter.cpp:1253–1367`; preset APIs `EV/MixtormatPresetStore.h:51–69`. | Customer workflows; do not gate the whole importer or preset system. |
| Temporary preset files; `EV/MixtormatPresetStore.cpp:247–254`; `MixtormatPresetStoreDocuments.cpp:157–163`. | Real safe-write/publish machinery, not development leftovers. |
| Normal-to-Height readback; `SP/MixtormatNormalHeightGenerator.cpp:270–278,457–461`. | Artist texture-conversion operation, not a debug dump. |
| Adaptive composition timing; `EW/SMixtormatPreviewViewport.cpp:795–806,873–879`. | Functional throttling, not a telemetry overlay. No FPS/GPU-time/memory readout found in the audited UI. |
| Unreal `LegacyScreenPercentageDriver` in thumbnail rendering. | Engine API name, not Mixtormat release branding or obsolete helper evidence. |

## 8. How to make actual developer tools private

### Current state

No dedicated private developer-tools definition was found in:

- `Source/MixtormatEditor/MixtormatEditor.Build.cs:7–48`.
- `Source/MixtormatShaders/MixtormatShaders.Build.cs:7–22`.
- `Source/MixtormatRuntime/MixtormatRuntime.Build.cs:7–17`.

`Mixtormat.uplugin:18–39` correctly makes Editor and Shaders editor-only.
That separates them from game runtime, **not from customers using Unreal Editor**.

These are not valid private-developer identities:

- `WITH_EDITOR` or an Editor module: customers need them.
- Development / `!UE_BUILD_SHIPPING`: customer editor builds also use these configurations.
- Having plugin Source or raw source-art directories: customer source installs are legitimate.
- A normal console variable or hidden button: discoverable and does not remove executable tooling.
- `Mixtormat.Developer.ParameterMeta`: a normal console switch, currently default 1; not a private build gate.

### Proposed minimal gate — not implemented

1. Introduce one explicit build policy, e.g. `MIXTORMAT_PRIVATE_DEV_TOOLS`, default **0**.
2. Enable **1** only through an intentional private build-rule opt-in, not auto-detection.
3. Apply the same policy to each module that owns engineering tooling, using module-private definitions.
4. Gate the tool's includes, declarations, implementation, state, UI entry points, and callbacks together.
5. Keep normal theme loading, design tokens, typography, styles, artist settings, and preview rendering compiled.
6. Gate UI STYLE authoring/locator machinery, not the theme model consumed by ordinary widgets.
7. Gate plugin-library reconstruction/deletion and migration services, not shared/customer importing.
8. Gate parameter-definition/range authoring menus, panels, pending edits, override storage, and write APIs. Keep shipped JSON loading/resolution and normal binding/reference/driver behavior.
9. Gate cache-disable registration and its read; leave release caching and its memory budget functional.
10. Gate log-only latches with their logs; retain warnings that explain actual product failures.
11. Compile customer binaries with the policy off, and exclude dedicated private tool sources where practical.

**Reflected fields are a separate problem.** Do not put arbitrary private preprocessor guards around
`UPROPERTY`/`UENUM` declarations and assume UHT/serialization will remain valid. First approve either
coordinated parameter retirement or a safe release evaluation policy. For Moss, retire the authored
Preview override while keeping solver inspection in the existing viewport-output path; do not gate
or remove normal Height generation. Matching shader defines/permutations are necessary only
if shader engineering branches are actually compiled out; CPU bindings/dispatch must agree.

This is distribution hygiene, not an anti-tamper/security boundary: customers receiving source can
modify and rebuild it. The deliverable should simply have no private tools enabled by default.

### Do not gate the entire DebugPreview system

Normal rendering currently requires valid Debug targets (`SP/MixtormatGpuCompositor.cpp:1855–1862`).
Preview requests also participate in cache restoration (`:2323–2335`) and selected-output handling.
Overlays run after material composition (`SP/MixtormatGpuComposePipeline.cpp:1593–1658,1696–1700`).
Removing that pipeline would break artist inspection and require coordinated target/binding/cache work.
Clean the labels and isolate genuine engineering tools instead.

## 9. Suggested implementation order, after approval

1. Delete stale UI text and redundant readouts from section 2; preserve working features.
2. Add the default-off private gate for UI STYLE, locator, parameter-definition/range editing, source-library mutation, and migration. Keep these tools available in private developer builds.
3. Remove the Moss Preview authoring parameter and route solver inspection through existing viewport outputs. Preserve material Height-to-normal behavior and leaf detail normals.
4. Retire confirmed unused shim/inactive style controls, with approval for actual deletions.
5. Decide obsolete Flow and serialized parameter retirements as separate coordinated changes.
6. Trim dormant shader permutations only after checking every dispatch/resource dependency.
7. Preserve existing package exclusions; add targeted source/resource exclusions where approved.
8. User handles compilation, package inspection, and testing; none was run during this audit.
