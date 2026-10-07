# Viewport quick controls — audit and prototype plan

Status: audit/planning only, 2026-10-07. No UI implementation in this pass.
The reference image is a grouping reference, not a visual or feature specification.

## Agreed scope

- Reuse existing Slate controls, icons, plates, typography and theme tokens.
- One-step access: actual controls visible immediately, no category → submenu navigation.
- **Tab opens the marking/direct-controls menu** around the pointer; no wedges, glow,
  connectors or fancy animation. RMB does not invoke this menu.
- Top = render; left = lighting; right = geometry; bottom = Actions placeholder.
- Actions becomes a conventional **RMB-click context-menu row list** later. RMB drag retains
  lighting rotation. For now, one disabled dummy row;
  do not implement the image's Preview/Isolate/IDs/Add/Compare buttons.
- GLOBAL also provides the settings and switches for showing each group on the viewport.
- Always-visible controls anchor to the full viewport, never to Inspector
  position/size. “Always-visible” does not imply disk persistence.
- A floating Layers panel (D22) may cover the left rail. Accepted: controls do not move to
  avoid overlays, and the user can hide or move the panel instead.
- Inspector, gallery, GLOBAL variables, camera framing and docking remain otherwise unchanged.

## Source audit

| Existing owner | What can be reused |
|---|---|
| `SMixtormat_Preview.cpp::BuildPreviewPanel` | FXAA/TSR/Off, render Scale, Default/Lumen, Final settings, displacement toggle/amount, light/skylight sliders, lighting presets/reset, four mesh icons, 90° UV toggle, FOV, preview-mode label, 1K/2K/4K and clear-debug control |
| `SMixtormatPreviewViewport.cpp::FMixtormatPreviewViewportClient` | LMB drag orbits; RMB drag rotates lighting; wheel zooms; F frames; H/Space hides overlay UI; Z toggles displacement; V changes channels |
| `SMixtormat_Shell.cpp::BuildGlobalPage` | Existing third left tab, currently a global-variables placeholder in a scroll/group container |
| `SMixtormat.h` | Canonical preview values and one master `bPreviewOverlayUiVisible` flag; group-specific visibility does not exist yet |
| `SMixtormat_Theme.cpp::TransferLayoutState` | Reconstruction preserves scroll/expansion; runtime UI choices belong on retained `SMixtormat`, not theme data |

### Important mismatches / risks

- RMB drag already rotates lighting. Tab avoids the marking-menu conflict; future RMB-click
  context actions must distinguish click from drag and never open after lighting rotation.
- No explicit Tab binding was found in the audited main/viewport widgets. Slate uses Tab for
  focus navigation: consume bare Tab only in the preview, preserving text entry, Shift+Tab
  and normal navigation elsewhere. Client `InputKey` is the proposed entry point,
  not a proven interception point: this review did not inspect engine Slate/viewport
  Tab routing or test it in-editor. Prove delivery before implementing the popup.
- 90° rotates the complete material UVs, including tangent-space normal direction. Label it
  “UV 90°”; it is not geometry rotation and must retain its existing material-edit semantics.
- 1K/2K/4K changes **composition resolution**, unlike render Scale (viewport screen percentage).
  Keep the meanings and setters separate.
- Final is an existing settings popup, not a third Default/Lumen rendering mode.
- The image's HDRI chooser, exposure slider and custom-mesh button have no equivalent control
  in the audited viewport builder. Do not add them as part of this layout prototype.
- Existing sliders need capture and continued interaction. Releasing Tab must not generically
  activate or close controls: hold/flick/release handling would be a separate input-system pass.
- Never change values merely on hover. Hover highlights; an explicit activation commits.
- The GLOBAL section being extended lives in `SMixtormat_Shell.cpp`; its empty-state
  text still uses a local `0.5f` opacity literal (L586–587). Tokenize it while adding
  the Preview/Viewport section (token audit checklist).
- GLOBAL/popup/viewport may use separate **control views**, all bound to the same
  existing state/setters. This permission does not apply to duplicating Inspector
  or Layers panels: each panel remains one live instance, reparented between hosts.
- `BuildFinalSettingsControls` is owned by `Widgets/Inspector/MixtormatInspectorGenerators.cpp`
  (L495+), not the preview file. Reuse its callback in each view; do not duplicate
  document-setting logic while extracting preview controls.

## Recommended control map

| Group | Popup / GLOBAL content | Default viewport visibility |
|---|---|---|
| Render | AA, render Scale, Default/Lumen, existing Final popup, displacement + amount | On, top-centre strip; wrap existing rows if narrow |
| Lighting | Current preset icons, Light and Skylight sliders, existing camera/lighting reset | On during prototype; left rail/card |
| Geometry | Sphere, cylinder, cube, plane, UV 90° | On during prototype; right rail |
| Camera | Existing FOV and preview-mode label | On, bottom-centre; GLOBAL also exposes FOV |
| Output | Composition 1K/2K/4K; preserve clear-debug affordance | On, bottom-right |
| Actions | Disabled “Context actions — later” row list placeholder | Popup only |

Keeping existing groups on by default avoids silently removing controls. GLOBAL lets the user
hide lighting/geometry rails to try the uncluttered workflow. Popup access remains available
when a viewport group is hidden. Render, Camera and Output can also be shown/hidden independently.
The existing H/Space master visibility flag combines with group flags; it does not overwrite them.
GLOBAL settings stay accessible with no selected layer. Preserve the existing variable placeholder;
add a separate “Preview / Viewport” section, not document variables masquerading as preview state.
No new preference persistence in this prototype: flags are session state on the
retained workspace, preserved by theme reconstruction and reset on workspace close.
Group visibility changes UI only; keep existing viewport hotkeys regardless of flags.

## Input plan

- **Bare Tab:** open the direct-controls marking menu; ignore key repeats and modifiers.
  Decided (D26): press once to open, then normal LMB interaction; release does not close.
- **RMB click:** reserved for the later conventional context menu, not implemented now.
- **RMB drag:** retain lighting rotation. Later distinguish click/drag using Slate's drag threshold.
- Escape/outside click dismisses. Sliders and existing Final popup retain normal capture/focus.
- One-step direct access first; optional Tab-hold/flick/release marking remains deferred.
- Clamp the whole popup to the viewport; near edges shift the group together, keeping directions.
  On small viewports use the same groups in a compact stacked layout, not offscreen controls.
- Invocation requires viewport keyboard focus **and** the pointer over exposed
  viewport content, not over Inspector, Layers or other controls. Focus alone does
  not identify the hovered widget. Use Slate-local pointer coordinates so DPI
  scaling and viewport movement do not misplace the popup.
- Sliders/combos may legitimately capture mouse or focus, and numeric text entry
  must retain keys. Do not force focus back to the viewport while typing. Define
  focus-loss dismissal to distinguish nested Final/combo menus from leaving the
  popup; restore viewport focus only when appropriate on dismissal.
- An outside click needs a defined consumed-versus-forwarded policy; dismissing the
  popup must not accidentally start orbit or lighting rotation through its content.

## Implementation sequence (when approved)

Step 0: settle the builder API/private boundary (B8) **before step 1**.
D26–D28 settle the gesture, Render merge and in-viewport overlay approach. During
step 4, prove viewport-scoped Tab delivery and implement/test focus and callback
lifetimes (B1/B2/B10). Steps 1–2 do not require D22, variables, gallery changes or
persistence. The user will test each implemented stage (D29).

1. Extract focused control builders from `BuildPreviewPanel`; preserve callbacks, reset behavior,
   disabled states, tooltips, preview-mode feedback and debug clearing. Reuse builders in all views.
   Tokenize any style literal the touched code carries (audit checklist).
2. Add group-visibility state on `SMixtormat` and a GLOBAL Preview/Viewport section with toggles
   and the actual settings. Keep master visibility and theme reconstruction behavior intact;
   the state lives on the retained `SMixtormat` and is session-only.
3. Merge AA/Scale/Displacement and Default/Lumen/Final into the Render strip (D27)
   using the shared builders; maintain full-viewport alignment.
   No Inspector-dependent offsets, camera changes or compulsory rail removal.
4. Add the press-to-open Tab direct-controls popup as an in-viewport overlay
   (D26/D28). Bottom stays a disabled dummy context row
   list; RMB-click context actions are a later pass. No new lighting/geometry capabilities.
5. Validate each stage before proceeding. The baseline build passes according to
   the user; no baseline behaviour has been tested in-editor. With authorization,
   test viewport/text/popup focus, repeats/modifiers, all preserved keys, RMB lighting,
   slider capture/reset/type-in, nested Final menu, dismissal/click-through, DPI/edges,
   tiny viewports, group flags plus H/Space, no-material disabled states, shared values
   and theme reconstruction with panels docked/hidden/floating. Build success alone
   is not a UI acceptance result.

## Blockers

| # | Blocker | Why | Resolution |
|---|---|---|---|
| B1 | Tab is Slate's focus-navigation key | A client binding may not receive it before Slate navigation; engine routing is unverified | Prove delivery to `FMixtormatPreviewViewportClient::InputKey` first. If intercepted earlier, design a viewport-widget-scoped handler. Never add Tab to the application-registered L/P preprocessor (`SMixtormat.cpp` L69–88) |
| B2 | Viewport/popup focus policy is incomplete | Viewport keys and slider text entry need different focus owners; viewport focus alone does not exclude pointer-over-panel invocation | Open without stealing focus; yield while controls type/capture, exempt nested menus from dismissal, define focus restoration and require an exposed-viewport hit. Test rather than assuming an overlay solves focus |
| B3 | RMB is lighting rotation | `InputAxis` rotates lighting while RMB is held (L452–465) | Tab owns the marking menu. RMB-click context actions are a later pass and must use a drag threshold, never firing after a rotation |
| B4 | Slate widgets cannot have two parents | GLOBAL and the popup cannot share one instance | Build separate instances from shared builder functions; keep one state owner (`SMixtormat`) |
| B5 | Builders are split three ways | `AddMeshButton` / `AddPresetButton` are local lambdas inside `BuildPreviewPanel` (`SMixtormat_Preview.cpp` L1027, L1135); `MakeSlider` is an `SMixtormat` member (`SMixtormat.h` L733); `MakePreviewCluster` is a file-local free function (L128) | Extract the lambdas to a shared builder form (step 1); decide what the member and free functions need in their new home |
| B6 | Popup contains interactive controls | Choosing an overlay does not automatically guarantee clamping, capture or focus | Implement the decided in-viewport overlay (D28), retaining the existing nested Final popup pattern (Preview L1552–1557). Test control capture/text entry and nested-menu dismissal |
| B7 | Theme reconstruction destroys the tree | A popup left open would reference dead widgets | Close the popup on reconstruct; keep group-visibility flags on retained `SMixtormat` state |
| B8 | Extraction boundary is not yet specified | Local lambdas and file-local plate/style helpers need an API; new `SMixtormat` declarations consume header budget | Choose minimal member declarations or a private context/header before step 1; keep logic out of the main header. Preserve `MakeSlider` reset registration (SMixtormat.cpp L366–414) and callback enablement |
| B9 | Tab gesture — resolved design | D26 selects press-to-open; release does nothing | Implement bare non-repeating press only, with Escape/outside-click dismissal; hold/flick/release is out of scope |
| B10 | Popup and viewport callbacks need safe lifetimes | Reconstruction replaces layout but reuses the viewport; delegates are originally installed only on viewport creation (Preview L1495–1519) | Close popup/nested menus and release capture before rebuild. Bind the new viewport request to retained workspace state, never a destroyed popup/anchor. Define a current-owner handle or rebind path; test reopening after rebuild |

## Implementation requirements and lifecycle checks

- Group-visibility state on `SMixtormat`, composed with `bPreviewOverlayUiVisible` (H/Space), never replacing it.
- Popup dismissal rules: Escape, outside click, viewport focus loss, reconstruction, and entering Docked/Overlay inspector modes.
- A pointer-position store for popup placement, clamped to the viewport like the inspector overlay.
- Press-to-open Tab semantics (D26), implemented without release-triggered activation.
- A GLOBAL “Preview / Viewport” section that hosts the real settings, not only toggles.
- Popup lifetime must be explicit: the retained workspace should reach the current
  popup owner to close it deterministically before reconstruction. A weak handle
  is a proposed mechanism, not an already implemented API (B10).
- `TransferLayoutState` stores groups/scroll offsets in traversal order (Theme
  L22–76). Ensure additional GLOBAL/control views produce identical capture/restore
  topology; close popup content before snapshot or explicitly exclude it. Keep one
  attached hidden instance per reparented panel so state remains reachable.

## Modularity and file plan

| File | Change | Size guard |
|---|---|---|
| `Widgets/SMixtormat_PreviewControls.cpp` **(new)** | Extracted builders + the quick-menu builder | Keep under ~400 lines; this is where the new code goes |
| `Widgets/SMixtormat_PreviewControls.h` **(new, if needed)** | Shared context/API for the extracted builders | Declarations only; no inline logic |
| `Widgets/SMixtormat_Preview.cpp` (1669) | Loses the extracted builders; keeps `BuildPreviewPanel` composition | Must **shrink**, not grow |
| `Widgets/SMixtormat_Shell.cpp` (880) | GLOBAL Preview/Viewport section only | Small, bounded addition; tokenize the empty-state opacity literal (L586–587) while here |
| `Widgets/SMixtormat.h` (1663) | New state + a few declarations | Target under ~20 added lines; no new inline logic |
| `Widgets/SMixtormatPreviewViewport.h/.cpp` | Viewport-scoped Tab request and delegate bridge to workspace | Minimal routing/API only; prove delivery first, no popup layout or new camera/lighting behaviour |
| `Style/MixtormatDesignTokens.h` | Structural popup layout tokens if needed | No widget-local styling |
| `Style/MixtormatTheme.h/.cpp`, `MixtormatThemeSchema.cpp` | Live-retunable styling only, when needed | Preserve current defaults and choose correct Paint/Layout/Reconstruct reader behaviour |

`SMixtormat.h` is already ~1663 lines and included widely; every addition is a cost.
The ~400-line extraction budget is a guard, not permission to omit callbacks or
behaviour: existing controls plus plate/style helpers may exceed it. If needed,
place a focused quick-menu widget under `UI/Controls/`, with private preview builders
remaining under `Widgets/`; do not grow the main header or duplicate implementations.
No new dependencies or alternate compatibility/fallback systems.

## Must not break

- **Viewport input:** LMB orbit, RMB lighting rotation, wheel zoom, F, H/Space, Z, U/M, V, Shift+V, and the light gizmo's `IsRotatingLighting()` visibility.
- **Preview state path:** `SetPreviewAntiAliasing`, `SetPreviewScreenPercentage`, `SetPreviewFov`, `SetPreviewQuality`, `SetPreviewDisplacementEnabled/Amount`, `SetPreviewLightIntensity`, `SetPreviewSkylightIntensity`, `SetStudioLighting`, `SetPreviewMesh`, `SetGlobalUVRotation90`, `SetCompositionResolution` stay the only route to the viewport.
- **Shell:** splitter fractions and their write-back guards, `bSuppressSplitWriteBack`, the derived `bInspectorCollapsed`, the L/P preprocessor, top bar and status bar.
- **Inspector overlay:** the single `InspectorPanel` instance, dock/overlay host reparenting, drag/resize, and the placement cycle.
- **Theme:** `TransferLayoutState` scroll/expansion transfer, `FMixtormatStyle::Refresh()` brush keys, and the `Mixtormat.Panel` / overlay brush split.
- **Existing popups:** `BuildFinalSettingsControls` and `SMixtormatPopupAnchor` clamping.

## Effort / decisions

Medium: control reuse and GLOBAL switches are straightforward; Tab/focus routing, capture and
popup edge fitting are the main work. True flick/release marking adds another input-system pass.
D26–D28 decide press-to-open Tab, the merged Render strip and an in-viewport
control overlay. Technical focus/capture/dismissal details must be implemented and
verified; engine Tab delivery remains unproven until the routing stage is tested.

## Resolved design questions / implementation checks

| # | Question | Notes |
|---|---|---|
| Q1 | Tab press-to-open or hold-to-show? | Resolved — press-to-open, release does nothing (D26) |
| Q2 | Does the Render strip merge AA/Scale/Displacement with Default/Lumen/Final? | Resolved — merge (D27), preserving individual setters and the Final popup |
| Q3 | Popup as `SMenuAnchor` or as an overlay layer? | Resolved — in-viewport overlay (D28). Controls keep normal focus/capture, nested Final menus remain usable, and popup/nested menus close before theme reconstruction. Exact event routing remains implementation/test work |
| Q4 | Do hidden viewport groups keep their hotkeys? | Preserve them: existing behaviour must not be silently removed. Any change needs explicit approval |
