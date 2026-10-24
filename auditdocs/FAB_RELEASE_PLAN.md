# Mixtormat — FAB Release Plan

Status checked against source/config; packaging follow-up 2026-10-24.
Static inspection; no git, commands, builds, Unreal launches, network checks or binary inspection.

## Goal
Ship Mixtormat 1.0 for UE 5.8 / Win64 with a curated, tested BuildPlugin package.
See [exact paths and packaging caveats](FAB_RELEASE_DETAIL.md).

## Confirmed done
- [x] `Mixtormat.uplugin`: `VersionName` is `1.0.0`; `Version` is `1`.
- [x] Buyer description mentions authoring, baking and runtime control of baked materials.
- [x] Engine `5.8.0`, Win64-only, Rendering category, non-beta are declared.
- [x] Runtime + two Editor modules are declared; Editor modules have Editor target allowlists.
- [x] Docs/support URLs are populated. Reachability/delivery is not verified.
- [x] Runtime Build.cs declares only Core, CoreUObject and Engine dependencies.
- [x] All four planned Config files exist; both JSON files now have explicit filter includes.

- [x] Filter excludes Tools and nested source Tests.
- [x] Removed obsolete Lucide/Feather notice and its filter entry; owner confirms no longer used.
- [x] Filter excludes Content PNG/HDR/FBX import sources (not their `.uasset` counterparts).
- [x] Nine canonical AgentDocs guides remain at root, plus ignored `temp.md`.

## Layer/group recipes — implemented, release checks pending
- [x] Gallery has Materials, Masks and Recipes columns with persisted resize weights.
- [x] Recipes column supports search/categories and shipped/user recipe entries.
- [x] Group/layer capture, closure validation and editable-group insertion exist in source.
- [x] Menus provide refresh, insertion, user copies and referenced-asset details.
- [ ] Verify intended shipped recipes in `Resources/Presets/Recipes` reach the package.
- [ ] Include all referenced assets; recipes reference assets rather than embedding them.
- [ ] Owner smoke check: create/save group recipe → find in gallery → insert twice;
  confirm independent editable groups, preserved internal links and unchanged global settings.
- [ ] Check shipped read-only entries, user copies and missing-dependency errors.
- [ ] Correct stale recipe help mentioning JSON import/export; current menus omit these actions.

Implementation map and behavior: `AgentDocs/UI.md` → Layer/group recipes — Gallery column.
These are static source findings, not runtime test results.

## Partial / not yet established
- Packaging filter is not a complete curated-content policy or verified output manifest.
- `.gitignore` excludes local content/output; it does not control BuildPlugin packaging.
- SVG icon registrations supersede the old PNG-only shipping recommendation.
- Owned SVGs do not require the removed Lucide/Feather notice.
- No changelog was found in the visible workspace; scope freeze is an owner decision.
- Historical asset/file counts and archive inventories were not revalidated.
- Full Runtime include audit, shader registration coverage and UI compliance remain unverified.

## Stable-doc follow-up — 2026-10-24
- Corrected architecture's obsolete Generator-layer description and stale file-map offsets.
- Bake service declares 10 outputs, including three optical textures; include these in smoke checks.
- Missing authoring JSON defaults resolve through reflected defaults; Mask.TilingY is not a proven bug.
- Recipe help still mentions unavailable import/export; gallery restore help omits Recipes.
  These are copy issues, not reasons to reintroduce menu actions.
- Moss and GPU-pass internals are being rewritten. Reconcile their docs after the work lands;
  this review does not certify those implementations or their existing documentation.
- File splits remain optional maintenance, not FAB blockers.

## Next actions — in order
0. **UI consistency first.** Audit/fix hover help, interactive popovers and dialogs against
   shared help/menu/card surfaces, typography and theme metrics. Confirmed native-tooltip,
   ramp/tab literal and bake-dialog typography gaps are in [ui-style-token-audit.md](ui-style-token-audit.md).
   Trace wrappers before replacing tooltip calls; preserve focus, drag and dismissal behavior.
1. **Confirm package inputs.** Ensure both Config JSON files reach the built package;
   explicit filter includes are added; verify the built artifact when packaging.
2. **Choose sample content.** Pick 2–3 materials and a sample mesh, then retain their
   full dependency closure, including required preview/runtime assets. Do not prune blindly.
3. **Review resources/notices.** Retain registered SVGs, brand art and any required notices.

   Do not delete apparently unused resources without approval and a reference check.
4. **Resolve parameter audit decisions.** See [GHOST_PARAMS_AUDIT.md](GHOST_PARAMS_AUDIT.md).
   ChildMask drivers are deferred beyond v1 by owner decision; retain disabled UI and notes.
   IdMapping remains open.
5. **Owner runs package/smoke gates.** BuildPlugin → inspect artifact → clean UE 5.8 project
   → open asset → compose → preview → bake → packaged-game baked-material runtime.
6. **Finish listing.** Screenshots, video, feature/limit list, demo link, license/EULA review,
   changelog, live docs/support checks; add MarketplaceURL when assigned.

## Acceptance gates — still pending
- [ ] Hover help/popovers/dialogs follow shared styling; owner checks clipping, focus and dismissal.
- [ ] Artifact contains needed Config JSON, SVGs, shader/source files and applicable notices.
- [ ] No developer docs, prototypes, metadata, caches or raw import sources leak into it.
- [ ] Curated Content includes all dependencies; no stale sample thumbnails.
- [ ] Clean-project and packaged-game checks pass; relevant log warnings are reviewed.
- [ ] Bake produces all 10 expected outputs; optical-master support/warnings are checked.
- [ ] FAB submission requirements and final archive layout are checked against current guidance.
- [ ] Listing assets and links are ready.

Do not delete source-controlled files or blanket-remove BuildPlugin-generated binaries.
The submitted archive must follow the applicable FAB code-plugin requirements; output
binaries and staging files are distinct from local development artifacts.
