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
- Persistent controls anchor to the full viewport, never to Inspector position/size.
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
  and normal navigation elsewhere.
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
- GLOBAL/popup/viewport may use separate widget views, but all bind the same existing state
  and setters. Do not share one Slate widget between parents or duplicate settings state.

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
No new preference persistence in this prototype.

## Input plan

- **Bare Tab:** open the direct-controls marking menu; ignore key repeats and modifiers.
  Recommended first prototype: press-to-open, then normal LMB interaction; release does not close.
- **RMB click:** reserved for the later conventional context menu, not implemented now.
- **RMB drag:** retain lighting rotation. Later distinguish click/drag using Slate's drag threshold.
- Escape/outside click dismisses. Sliders and existing Final popup retain normal capture/focus.
- One-step direct access first; optional Tab-hold/flick/release marking remains deferred.
- Clamp the whole popup to the viewport; near edges shift the group together, keeping directions.
  On small viewports use the same groups in a compact stacked layout, not offscreen controls.
- Handle invocation only in the preview viewport, not over Inspector or other Slate controls.

## Implementation sequence (when approved)

1. Extract focused control builders from `BuildPreviewPanel`; preserve callbacks, reset behavior,
   disabled states, tooltips, preview-mode feedback and debug clearing. Reuse builders in all views.
2. Add group-visibility state on `SMixtormat` and a GLOBAL Preview/Viewport section with toggles
   and the actual settings. Keep master visibility and theme reconstruction behavior intact.
3. Regroup the viewport controls using those builders; maintain full-viewport alignment.
   No Inspector-dependent offsets, camera changes or compulsory rail removal.
4. Add the simple Tab-invoked direct-controls popup. Bottom stays a disabled dummy context row
   list; RMB-click context actions are a later pass. No new lighting/geometry capabilities.
5. Test Tab invocation versus normal focus navigation, repeats/modifiers, RMB lighting rotation,
   capture, popup sliders/Final menu, viewport edges, narrow layouts, group visibility, H/Space,
   shared setting values and LiveTheme reconstruction.

## Blockers

| # | Blocker | Why | Resolution |
|---|---|---|---|
| B1 | Tab is Slate's focus-navigation key | Consuming it workspace-wide breaks focus traversal and text entry | Handle bare Tab in `FMixtormatPreviewViewportClient::InputKey` only, so it fires when the viewport has focus. Do not add it to the `FMixtormatWorkspaceHotkeys` preprocessor (`SMixtormat.cpp` L69–88), which is workspace-wide |
| B2 | Viewport keyboard focus is a precondition | `InputKey` only runs when the viewport widget is focused; the popup must not steal focus | Open the popup without moving keyboard focus; verify F/H/Z/V still work while it is open |
| B3 | RMB is lighting rotation | `InputAxis` rotates lighting while RMB is held (L452–465) | Tab owns the marking menu. RMB-click context actions are a later pass and must use a drag threshold, never firing after a rotation |
| B4 | Slate widgets cannot have two parents | GLOBAL and the popup cannot share one instance | Build separate instances from shared builder functions; keep one state owner (`SMixtormat`) |
| B5 | Existing builders are local lambdas | `AddMeshButton`, `AddPresetButton`, `MakeSlider`, `MakePreviewCluster` live inside `BuildPreviewPanel` and cannot be reused | Extract them first (step 1); this is a prerequisite, not a parallel task |
| B6 | Popup cannot be a plain menu | Menus close on click; the popup contains sliders and a combo | Reuse the `SMenuAnchor` + `UseCurrentWindow` pattern the Final settings popup already uses, or an overlay layer inside `BuildPreviewPanel` positioned from the stored pointer position |
| B7 | Theme reconstruction destroys the tree | A popup left open would reference dead widgets | Close the popup on reconstruct; keep group-visibility flags on retained `SMixtormat` state |

## Necessities not yet in the plan

- Group-visibility state on `SMixtormat`, composed with `bPreviewOverlayUiVisible` (H/Space), never replacing it.
- Popup dismissal rules: Escape, outside click, viewport focus loss, reconstruction, and entering Docked/Overlay inspector modes.
- A pointer-position store for popup placement, clamped to the viewport like the inspector overlay.
- A decision on Tab press-to-open versus hold-to-show (open question below).
- A GLOBAL “Preview / Viewport” section that hosts the real settings, not only toggles.

## Modularity and file plan

| File | Change | Size guard |
|---|---|---|
| `Widgets/SMixtormat_PreviewControls.cpp` **(new)** | Extracted builders + the quick-menu builder | Keep under ~400 lines; this is where the new code goes |
| `Widgets/SMixtormat_Preview.cpp` (1669) | Loses the extracted builders; keeps `BuildPreviewPanel` composition | Must **shrink**, not grow |
| `Widgets/SMixtormat_Shell.cpp` (880) | GLOBAL Preview/Viewport section only | Small, bounded addition |
| `Widgets/SMixtormat.h` (1663) | New state + a few declarations | Target under ~20 added lines; no new inline logic |
| `Widgets/SMixtormatPreviewViewport.cpp` (1506) | Bare-Tab branch in `InputKey` only | A few lines; no new camera or lighting behavior |
| `Style/MixtormatDesignTokens.h` (841) | Popup layout tokens if needed | Tokens only, no local literals in widgets |

`SMixtormat.h` is already the largest file in the module and is included widely; treat every addition to it as a cost. If the quick menu outgrows a builder function, promote it to a dedicated widget under `UI/Controls/` (the `SMixtormatSegmentedControl` / `SMixtormatTabStrip` pattern) rather than growing the header.

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
Tab invocation and future RMB-click context actions are decided. Before coding, confirm whether
Tab is press-to-open or hold-to-show, and whether the Render strip combines AA/Scale/Displacement
with the existing top-centre Default/Lumen/Final group.

## Open questions

| # | Question | Notes |
|---|---|---|
| Q1 | Tab press-to-open or hold-to-show? | Press-to-open is the smaller prototype; hold-to-show needs gesture handling |
| Q2 | Does the Render strip merge AA/Scale/Displacement with Default/Lumen/Final? | Merging shortens the top strip but mixes two existing groups |
| Q3 | Popup as `SMenuAnchor` or as an overlay layer? | Both reuse existing patterns; the overlay layer avoids window focus entirely |
| Q4 | Do hidden viewport groups keep their hotkeys? | H/Space, Z and V are viewport-level today |
