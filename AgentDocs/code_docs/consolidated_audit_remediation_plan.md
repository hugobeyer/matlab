# Mixtormat — Consolidated Audit Remediation Plan

Date: 2026-10-08
Status: dated implementation snapshot; source edits are not runtime-validated.

## Local implementation status — 2026-10-08

Source edits present: unique structural localization keys, compact chip diagnostics,
icon-role/HitSize schema exposure, 33% default Masks width, and visible mask-selection feedback.
Tile borders paint above thumbnail content. Marking-menu 1K/2K/4K controls and Ctrl+wheel FOV
routing are present. The NavigationRail role is separate and larger than its prior role.
The gallery header supports click-to-collapse with drag-threshold handling.

Further source edits: Inspector structural labels are cached with refresh/rebuild invalidation;
incoming counts map effective targets back to authored rows; MMB pan uses camera-plane movement
with reduced sensitivity. A layered translucent gallery shadow approximation was added; it is
not verified visually and does not establish that the requested soft shadow is achieved.

Not implemented/confirmed: 1.5x icon-default scaling (requested, not applied), group containment,
marking-menu camera drift fix, turn damping, gallery pin/auto-collapse, helper action state machine,
noise wiring, and group-shared Push compatibility decision.
No build, tests, profiling or Unreal session was run. Runtime behavior and visual quality remain
unverified. Basis: supplied remote audit (`dbc71fe`) and targeted local source reads.
Local commit attribution and working-tree differences remain unverified.

## Scope and rules

- Implement P1–P4 as small, separately reviewable changes.
- Keep P5 and execution-changing P2 items behind explicit approval.
- Preserve flat execution order, explicit links, saved invalid links and deferred paste validation.
- Preserve effect-only Warp sources and all current generator target support.
- Keep `None → None` selectors and existing highlighting/instance-source behavior.
- Do not remove historic documents, assets, parameters, APIs or compatibility behavior.
- Respect Runtime ← Shaders ← Editor and the existing theme/token system.
- Use targeted source review only by default.
- Builds, tests, profiling, commands and Unreal sessions require separate consent.
- These acceptance checklists are prospective; they do not authorize execution.

## Sequence

| Step | Deliverable | Gate |
| --- | --- | --- |
| 1 / P1 | Cached Inspector connection presentation | Source review |
| 2 / P2a | Effective-to-authored incoming-count mapping | Preserve current count semantics |
| 3 / P2b | Count and invalid-GUID policy decisions | Approval before behavior changes |
| 4 / P3 | Missing icon roles and HitSize controls | Preserve defaults |
| 5 / P4a | Unique localization keys and compact chip text | Preserve full diagnostics |
| 6 / P4b | Current-state documentation | Distinguish source from runtime evidence |
| 6a / UI | Larger left-rail icons and clearer group containment | Presentation only |
| 6b / Viewport | Resolution actions and camera input corrections | Root-cause input review |
| 6c / Gallery | Soft shadow and consistent collapse/restore bar | Preserve drawer behavior |
| 6d / Helpers | Viewport helper labels | Reuse canonical help state |
| 6e / Noise | Investigate and connect missing noise controls | Confirm intended noise feature |
| 7 / P5 | Group-shared Push compatibility decision | Explicit approval and asset evidence |
| 8 | Optional runtime acceptance pass | Separate permission |

Start with Step 1. Steps 4–6e can proceed independently of unresolved legacy decisions.
Within Step 6b, investigate marking-menu camera drift before tuning camera motion. Include
input-conflict checks for Ctrl+wheel, menu gestures and existing camera controls.

## Step 1 — P1: Cache Inspector connection presentation

Primary files under `Source/MixtormatEditor/Private/`:
- `Widgets/Inspector/MixtormatInspectorGenerators.cpp`
- `Widgets/Layers/MixtormatStructuralConnections.cpp`
- `Widgets/SMixtormat.h`
- `Widgets/SMixtormat.cpp`
- `Widgets/SMixtormat_Layers.cpp`

Observed: Inspector Slate attributes call `GetStructuralConnectionLabel`; each populated
label constructs `FConnectionProjection`. Row chips already resolve once at construction.

Implementation:
1. Trace canonical refresh paths for link edits, names, enabled states, bindings and group edits.
2. Include selection, undo/redo, document changes and instance/source refresh in that review.
3. Add a widget-owned derived cache keyed by complete authored child address and connection role.
4. Resolve source and target presentation together on a miss, sharing one projection/status query.
5. Cache derived text/status only; never retain pointers into rebuilt layer arrays.
6. Invalidate at the existing preview-refresh and layer-list-rebuild boundaries first.
7. Cover any mutation path that bypasses those boundaries before switching Inspector attributes.
8. Keep menus live: build their current options on opening, not from cached menu contents.
9. Keep invalidation conservative initially; avoid a new global revision framework.

Acceptance by static review:
- Unchanged Slate attribute reads return cached data without constructing a full projection.
- Source and target reads share resolution for the same address/state.
- Selection changes cannot display another module's labels.
- Renaming, disabling, group edits and undo/redo have explicit invalidation coverage.
- No source asset loading occurs repeatedly on unchanged attribute evaluation.

Performance improvement is expected from the code path; actual timing remains unmeasured.

## Step 2 — P2a: Map incoming counts through resolved target identity

Primary file: `Widgets/Layers/MixtormatStructuralConnections.cpp`.
Reference owners: `Source/MixtormatRuntime/Private/MixtormatLayerGroups.cpp`
and `Source/MixtormatRuntime/Private/MixtormatOutputReference.cpp`.

Observed: evaluation uses effective/resolved children, but count arrays index authored children.
Shared children are appended, so ordinary local indices are not shifted. Do not rewrite the
count cache simply because it uses authored indices.

Implementation:
1. Continue evaluating modules with `EvaluateStructuralLinkForGather` on resolved effective data.
2. Validate the returned effective target index before accessing its resolved child.
3. Map that resolved target's identity back to the appropriate authored generator row.
4. Compare resolved target identity, not only the module's authored target field.
5. Leave effective-only targets without an authored row uncounted; do not fabricate rows.
6. Preserve legacy Push's first-eligible-target resolution and Warp's unique-identity contract.
7. Define ambiguous/invalid authored identity handling explicitly; do not silently rebind links.
8. Retain authored-row-sized labels and the existing count-cache invalidation boundaries.

Acceptance cases for source tracing:
- Ordinary local Push/Warp links retain existing counts.
- Appended shared children cannot index unrelated authored rows.
- An executable legacy shared module targeting a real authored generator remains countable.
- Projected targets without authored rows do not create misleading authored badges.
- Duplicate-identity handling does not tighten Runtime Push execution rules.

## Step 3 — P2b: Resolve two small policy questions

### Incoming-count meaning

Current behavior counts structurally executable links; Gather separately skips zero-strength
Flow Warp (`FlowAmount == 0` or clamped `FlowTraceLength == 0`).

Recommended default: retain valid enabled connection counts, including neutral Flow Warp.
Clarify the wording in documentation/tooltips rather than altering rendering or badge values.

If actual gathered-operation counts are preferred, approve that UI behavior change first.
Then mirror Gather eligibility, including finite-value normalization and trace-length clamping,
through an appropriate shared owner rather than introducing an Editor dependency in Runtime.
Do not generalize zero-strength suppression to Push or other Warp kinds without evidence.

### Empty Push target GUID

Current `EvaluateStructuralTarget` guards empty Warp IDs but deliberately preserves older Push
matching behavior. An empty Push ID may match a malformed child's empty ID.

Recommended change, pending approval: reject an invalid target GUID before matching for both
module kinds. Preserve first-eligible matching for valid Push IDs. Trace the legacy resolver and
all callers first, and record the impact on malformed saved assets. No GUID repair or migration.

Keep this Runtime guard separate from the safe Editor-only count-mapping patch.

## Step 4 — P3: Expose missing UI Style icon controls

Primary owner: `Style/MixtormatThemeSchema.cpp::AddIconRole`.
Reference defaults: `Style/MixtormatTheme.cpp`; consumers: existing icon widgets and theme store.

Implementation:
1. Register existing `CardLeading` and `GalleryToolbar` roles through `AddIconRole`.
2. Add `Icons.<Role>.HitSize` through the same helper for every icon role.
3. Preserve all current role defaults; do not alter enum ordering or authored theme values.
4. Inspect HitSize consumers/normalization before choosing bounds; reuse existing conventions.
5. Use reconstruction refresh for geometry changes and existing persistence/reset behavior.
6. Assign a locate target only where a truthful semantic locator exists; otherwise use None.

Acceptance by source tracing:
- All nine current roles expose HitSize through one schema helper.
- Newly exposed roles use canonical defaults and stable property IDs.
- Theme load/save/reset paths cover new properties without resetting existing customization.
- No new tokens, styling system or unnecessary config rewrite is introduced.

## Step 5 — P4a: Compact structural diagnostics

Primary file: `Widgets/Layers/MixtormatStructuralConnections.cpp`.

Implementation:
1. Give the two `StructuralConnectionReason` formats distinct localization keys.
2. Separate cached compact text from full diagnostic tooltip text.
3. Show a short issue code plus recognizable name in bounded row chips.
4. Keep full source/target identity and issue explanation in the tooltip.
5. Preserve Inspector information density, unconnected selectors and menu accessibility.
6. Use existing badge width tokens; do not increase hardcoded widths to fit long diagnostics.

Optional, separate follow-up: classify a missing `OutputName` as an incomplete reference.
Trace consumers and enum serialization first; append any needed serialized enum value.
Do not replace deliberate default `InvalidSourceScope` results or change source eligibility.

Acceptance: each localization key has one format; compact chips retain full tooltip reasons;
valid, unset, unavailable, disabled and invalid links remain editable where currently allowed.

## Step 6 — P4b: Update current-state documentation

- `AgentDocs/mixtormat_generator_interaction_v1_implementation_plan.md`:
  add a dated implementation-status overlay; preserve the original proposal and unimplemented
  hierarchy-indentation work. Mark phases individually from current source, not assumptions.
- `AgentDocs/ICONS.md`: reconcile registrations and missing-icon inventory from source;
  document SVG registrations and role/HitSize exposure only after those changes exist.
- `AgentDocs/UI.md`: distinguish drawer header `GALLERY` from MATERIALS/MASKS pane labels;
  document connection cache, count semantics and compact/full diagnostic presentation.
- `AgentDocs/code_docs/strata_structural_warp_design.md` and
  `AgentDocs/code_docs/generator_warp_output_alignment_design.md`:
  preserve intended rendering contracts and add source-only validation status where relevant.

Use additive status notes and localized corrections, not historical-document rewrites.
Do not imply builds, GPU execution, visual quality or performance have been verified.

## Step 6a — UI: Larger left-rail icons and clearer group containment

Requested from the supplied layer-stack screenshot: left navigation icons are too small,
and group headers do not visually encompass their member layers clearly enough.
This is presentation work, not a change to structural-module parenting or execution order.

### Enlarge left navigation icons

Starting owners under `Source/MixtormatEditor/Private/`:
- `UI/Controls/SMixtormatIconRail.*`
- `Widgets/SMixtormat_Shell.cpp`
- Existing theme/schema and design-token owners.

Implementation:
1. Trace the rail's actual glyph, button, hit-area and width settings before changing them.
1. Increase the left-rail glyph size; the user separately requested 1.5x icon defaults overall.
   That scaling is not applied yet; confirm its effect on button/hit sizes before changing defaults.
3. Keep icons centered, with consistent spacing and distinct active/hover/disabled states.
4. Reuse canonical theme controls; expose rail sizing there if it is not already editable.
5. Do not enlarge unrelated toolbar icons or reduce existing hit areas.

### Make group membership visually cohesive

Starting owners:
- `UI/Layers/SMixtormatLayerHierarchy.*`
- `UI/Layers/SMixtormatLayerGroupRow.*` and `SMixtormatLayerGroupContainer.*`
- `UI/Layers/SMixtormatLayerRow.*` and existing hierarchy/connector painting.
- `Widgets/Layers/MixtormatLayerHierarchy.cpp` and theme/token owners.

Preferred direction: indent member layers beneath the group header, and use the existing
container surface or hierarchy guide to visually connect the header to its members.
Make the group's beginning and end readable without adding a heavy border to every row.

Implementation:
1. Trace current group-container ownership, indentation and connector painting.
2. Add a consistent member-layer inset; retain child indentation relative to each layer.
3. Use a subtle continuous group surface or existing guide to encompass member rows.
4. Keep adjacent groups visually separate; preserve readable names and right-side controls.
5. Preserve selection, collapse, drag/drop, reorder, context menus and structural highlights.
6. Use existing palette/layout tokens; avoid a new styling system or duplicate connectors.
7. Update the original plan's hierarchy-indentation status only after implementation exists.

Acceptance by static review:
- Only the left navigation rail receives the requested icon-size change.
- Group membership is communicated by containment/indentation, not by altered data ownership.
- Ungrouped layers retain their existing alignment and behavior.
- Layer children remain distinguishable from member layers at each hierarchy depth.
- Expanded, collapsed and empty groups retain valid layout and interaction paths.

Visual acceptance remains pending a separately authorized Unreal session:
- Icons read clearly at normal UI scale without clipping or crowding.
- Each group visibly encompasses its own layers, including the last member row.
- Narrow stack widths and supported UI scaling preserve labels and controls.

## Step 6b — Viewport: Marking-menu resolution and camera input

User-reported behavior, not yet independently reproduced: opening the marking menu sometimes
moves the camera downward; MMB panning is too fast; camera turning needs slight damping.

Starting owners under `Source/MixtormatEditor/Private/`:
- `Widgets/SMixtormatPreviewViewport.*` for camera/input routing.
- `Widgets/SMixtormat_Preview.cpp` for preview actions and resolution state.
- Discover the marking-menu owner by exact symbols before implementation.

Implementation, in this order:
1. Trace menu activation, mouse capture, accumulated deltas and camera input dispatch.
2. Fix the confirmed failure path causing downward motion on menu opening; do not mask it
   with damping. Opening, selecting, dismissing or cancelling the menu must not move the camera.
3. Add `1K`, `2K`, `4K` resolution actions to the marking menu using existing resolution setters
   and preset mappings. Do not introduce independent resolution state or a new bake setting.
4. Reflect the active preset; disable genuinely unavailable actions instead of hiding them.
5. Reduce MMB pan sensitivity through the canonical camera settings, preserving direction
   and existing camera-mode behavior.
6. Add subtle, frame-rate-independent turning damping without making navigation sluggish.
   Reset pending motion on menu activation, capture loss and camera reset to prevent drift.
7. Route Ctrl+mousewheel to the existing camera FOV owner, using its bounds and update path.
   Plain wheel retains current behavior; Ctrl+wheel must not also trigger dolly/zoom.
8. Keep unrelated shortcuts, camera modes and input gestures intact.

Acceptance, with runtime checks requiring separate consent:
- Menu opening/cancellation introduces no translation, rotation or residual camera drift.
- All three presets update the same preview resolution used by existing controls.
- MMB pan is controllable at different camera distances and supported UI scales.
- Turning has light damping with prompt stop behavior and no delayed menu-induced motion.
- Ctrl+wheel changes FOV within canonical limits; plain wheel behaves as before.

## Step 6c — Gallery: Soft drop shadow and consistent collapse bar

Starting owners: `Widgets/SMixtormat_Shell.cpp`, `Widgets/SMixtormat_Library.cpp`,
existing gallery widgets, surface painters and `GalleryLayout` theme settings.

Implementation:
1. Trace current drawer surface, shadow painting, header and collapsed restore strip.
2. Add or tune one soft outer drop shadow using the existing theme/painter system.
   Avoid stacked child shadows, hard outlines and darkening the translucent gallery content.
3. Make the same gallery header/bar collapse the expanded drawer and restore it when collapsed.
   Reuse canonical collapse state and handlers; do not add a second competing control/state.
4. Preserve header dragging/resizing. Distinguish click from drag so resizing cannot collapse it.
5. Keep the existing G shortcut, restore affordance, drawer-height persistence and pane labels.
6. Preserve accessibility and a usable hit area in both expanded and collapsed states.

### Unpinned auto-collapse

Requested behavior: when the pointer leaves the gallery and its pin is not active, return the
drawer to its collapsed restore-bar state. An active pin keeps the drawer open.

Implementation:
1. Trace existing pin, hover and drawer state; reuse canonical state and collapse handlers.
2. Provide a pin toggle using the existing icon/theme conventions if none currently exists.
3. Detect leaving the whole drawer, not transitions between its child tiles or columns.
4. Defer auto-collapse during dragging/resizing, mouse capture or gallery-owned popup interaction.
   Re-evaluate once the interaction ends so an unpinned drawer does not remain open indefinitely.
5. Keep pinned drawers open on pointer exit; retain manual bar collapse and the G shortcut.
6. Preserve remembered expanded height when auto-collapsing; do not reset splitter sizes.
7. Avoid flicker when crossing the restore bar or opening a gallery-owned menu.

### Default Masks column width

Implementation:
1. Trace the MATERIALS/MASKS splitter's canonical defaults and persistence path.
2. Set the default Masks share to 33% of the available gallery content width; Materials takes 67%.
3. Apply this to fresh/default layout state, not over an existing user-saved splitter ratio.
4. Preserve resizing and existing minimum-width constraints at narrow window sizes.
5. Expose the default through the existing layout/theme owner if that is its current convention.

### Visible mask-selection feedback

User reports that selecting masks produces no visible feedback; the failure path is not yet
confirmed. Distinguish gallery selection from applying a mask to an authored target.

Implementation:
1. Trace mask-tile click → selected-item state → tile presentation and any existing apply action.
2. Repair the confirmed selection/notification or paint-state gap at its canonical owner.
3. Show a persistent selected state using existing gallery tile selection styling, distinct from hover.
4. Keep selected feedback visible after the pointer moves away; update it on another selection.
5. Preserve existing single/double-click and apply behavior; do not auto-apply masks as a feedback fix.
6. If applying a mask is unavailable, retain the item and disable the relevant action with its reason.
7. Preserve keyboard focus feedback separately from selected-item feedback.

Acceptance: a single soft shadow separates the drawer from the viewport; the bar toggles
collapse/restore consistently; drag and resize preserve height and never toggle accidentally.
Unpinned pointer exit collapses the drawer, while pinned and actively interacting drawers remain
open. Fresh layouts give Masks 33% without overwriting saved sizes. Selecting a mask produces
clear persistent feedback without changing application semantics.

Additional edge-case acceptance:
- Specify the initial pin state; pin and unpin are operable by mouse and keyboard.
- Pointer transitions across gallery children, popups and restore bar do not cause flicker.
- Auto-collapse is deferred during capture/drag and re-evaluated after interaction ends.
- Narrow windows respect minimum column widths without losing access to either gallery pane.
- Saved splitter sizes and drawer height survive collapse/restore and relaunch.
- Selection, hover and keyboard focus remain visually distinct; selection does not imply apply.

Final softness, clipping, hover/popup handling and selection contrast require visual review,
not just source inspection.

## Step 6d — Helpers: Contextual action state machine and viewport tips

First read `AgentDocs/HELPERS.md`, then trace canonical helper text, visibility and input state.
Starting owners include `Widgets/SMixtormat_Preview.cpp` and existing overlay/help widgets.

Goal: guide the user through the action currently being performed, with brief contextual tips
rendered as small viewport labels. Labels should have a soft shadow for contrast, but occupy
minimal space and never behave like a modal overlay.

Implementation:
1. Inventory intended helper actions, triggers, completion/cancellation events and existing text.
2. Model helper progression as explicit states: idle, action available, action in progress,
   next-step tip, completed and cancelled. Keep this state machine focused on helper presentation;
   do not duplicate or own the underlying tool/action state.
3. Define deterministic transitions from existing tool and input events; handle interruption,
   mode changes, selection changes and reopening the same action without stale tips.
4. Select one concise label for the active state; avoid showing a stack of competing messages.
5. Render labels through the existing viewport overlay/help path, not a blocking dialog or panel.
6. Use existing typography/palette and a subtle theme-consistent soft shadow; keep labels compact,
   placed away from focal content and essential controls where practical.
7. Reuse canonical helper text and activation state rather than copying descriptions.
8. Preserve helper cancellation on mouse clicks and chip dropdowns; dismiss or transition labels
   consistently on Escape, completion and tool cancellation.
9. Prevent overlap with marking/context menus and keyboard help; labels must not capture input.
10. Keep tooltips available as a separate fallback for the relevant controls, not as duplicate
    persistent viewport text.

Acceptance by source review:
- Each supported action has documented transitions and no stale state after cancellation.
- At most one compact, relevant tip is presented at a time.
- Labels do not block viewport interaction, camera controls or essential preview controls.
- Completion, interruption and cancellation clear or advance the tip deterministically.
- Existing helper cancellation semantics remain intact.

Visual/runtime acceptance, requiring separate consent: confirm legibility, soft shadow, minimal
viewport obstruction and correct transitions during real action workflows.

## Step 6e — Noise: Investigate missing wiring before implementation

User reports that "noises are not hooked at all anywhere." The intended noise feature is not
specified here; do not assume this means audio, procedural generators or parameter drivers.
Treat this as an unresolved wiring report, not a confirmed all-subsystems defect.

Investigation:
1. Use `AgentDocs/GENERATORS.md`, parameter routing and targeted noise-symbol searches to map
   existing noise definitions, registrations, controls, consumers and any documented intentions.
2. Identify the exact missing connection and distinguish authored controls from inactive UI.
3. If source does not identify the intended feature, ask which noise controls/system is meant.
4. For procedural noise parameters, trace model/defaults → authoring metadata → Inspector →
   gather → dispatch/binding → shader use before proposing the smallest wiring patch.
5. Preserve existing defaults, serialized fields and output semantics; no new noise types,
   fallback systems, dependency additions or architecture redesign without approval.

Deliverable: a concrete list of disconnected controls and their canonical consumers, followed
by localized wiring steps. Runtime noise output remains unverified until separately authorized.

## Step 7 — P5: Decide legacy group-shared Height Push compatibility

Primary owner: `Source/MixtormatRuntime/Private/MixtormatLayerGroups.cpp`.
Observed: shared Warp clones are disabled; shared Push clones are not. Push source references
are absent from shared reference remapping. This is not permission to make them symmetric.

Prepare a decision note before implementation:
1. Trace saved-asset/group authoring paths and current executable shared Push cases.
2. Describe current source/target identity behavior without automatically remapping anything.
3. Obtain permission to inspect an actual legacy asset in Unreal if asset evidence is needed.
4. Choose one explicit policy:
   - Preserve legacy execution; document its limitations.
   - Disable shared Push clones; approve the possible visual change to existing assets.
   - Formally support shared Push; approve source/target remapping and the broader contract.
5. If disabling/supporting, define saved-asset compatibility and diagnostic behavior first.

Do not call preservation "inert": current source does not guarantee shared Push is inert.
Do not auto-repair GUIDs, migrate assets or change Gather until the policy is approved.
This decision does not block Inspector caching, theme controls or localization cleanup.

## Step 8 — Optional acceptance pass after permission

These items cannot be closed by static review:
- Large-stack Inspector responsiveness and cached-label live updates.
- Multiple Push/Warp operation ordering and displacement/bedding accumulation.
- No double warping and aligned companion outputs.
- Legacy group-shared Push on an actual saved asset.
- Incoming badges across reorder, paste, disable, group edits and undo/redo.
- UI Style live sizing, HitSize behavior and save/reset persistence.
- 1.5x icon-default scaling and left-rail/group-containment appearance at normal and narrow widths.
- Gallery opacity, shadows, splitters and drawer-height persistence across sessions.
- Keyboard/context-menu helper overlap and SVG appearance at final sizes.
- Marking-menu 1K/2K/4K resolution actions and no camera movement on open/cancel.
- Slight turn damping, reduced MMB pan speed and Ctrl+wheel FOV without double handling.
- Gallery soft shadow and consistent click-to-collapse/restore bar without drag conflicts.
- Viewport helper state-machine transitions, concise labels, soft shadow and non-blocking behavior.
- Gallery pin defaults, mouse/keyboard pinning, pointer transitions, narrow widths and persistence.
- Mask selection versus application feedback and distinct keyboard-focus styling.
- Confirmed noise-control wiring and output response, once the intended feature is identified.

Request permission separately for builds, tests and Unreal/manual profiling work.
Report each result as source-reviewed, runtime-verified, blocked or deferred.

## Handoff for every implementation step

- List only localized changes and affected files.
- Record static acceptance coverage and any remaining uncertainty.
- Record approvals for behavior changes separately from implementation completion.
- Never mark runtime acceptance complete based solely on source inspection.
