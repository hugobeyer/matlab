# Mixtormat — FAB Release Plan

Status checked 2026-10-23 against current source/config. Documentation update only.
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
- [x] All four planned Config files exist.
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

## Next actions — in order
1. **Confirm package inputs.** Ensure both Config JSON files reach the built package;
   they are accessed by plugin-relative paths. Add explicit filter includes if needed.
2. **Choose sample content.** Pick 2–3 materials and a sample mesh, then retain their
   full dependency closure, including required preview/runtime assets. Do not prune blindly.
3. **Review resources/notices.** Retain registered SVGs, brand art, fonts and required licenses.
   Do not delete apparently unused resources without approval and a reference check.
4. **Resolve parameter audit decisions.** See [GHOST_PARAMS_AUDIT.md](GHOST_PARAMS_AUDIT.md).
   ChildMask drivers are deferred beyond v1 by owner decision; retain disabled UI and notes.
   IdMapping remains open.
5. **Owner runs package/smoke gates.** BuildPlugin → inspect artifact → clean UE 5.8 project
   → open asset → compose → preview → bake → packaged-game baked-material runtime.
6. **Finish listing.** Screenshots, video, feature/limit list, demo link, license/EULA review,
   changelog, live docs/support checks; add MarketplaceURL when assigned.

## Acceptance gates — still pending
- [ ] Artifact contains needed Config JSON, SVG/fonts, shader/source files and notices.
- [ ] No developer docs, prototypes, metadata, caches or raw import sources leak into it.
- [ ] Curated Content includes all dependencies; no stale sample thumbnails.
- [ ] Clean-project and packaged-game checks pass; relevant log warnings are reviewed.
- [ ] FAB submission requirements and final archive layout are checked against current guidance.
- [ ] Listing assets and links are ready.

Do not delete source-controlled files or blanket-remove BuildPlugin-generated binaries.
The submitted archive must follow the applicable FAB code-plugin requirements; output
binaries and staging files are distinct from local development artifacts.
