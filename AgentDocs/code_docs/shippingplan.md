Verdict on your current feature set: ship it, don’t grow it

Retirement update: Combine IDs is not a supported functional feature. Its original numeric child slot is reserved as ReservedCombineId; no legacy evaluation guarantee or reactivation work is implied. Other inventory counts below are original planning estimates, not a fresh audit.
The node inventory is complete as a product. You have 17 child types, and every link in the workflow chain exists and is evaluated: 2 generators (Strata Carver, Fracture), ID producers (Pattern and ID Group under its supported contract; Cluster authoring retired), 8 ID consumers (Random, Ramp, HSV, ColorId-Exact, UV-from-IDs, Relief-from-IDs, RampRelief, drivers), 3 mask sources (texture, published child output, Layer-Values) + scoped Blur/Curvature, 9 effect families (Peeling, Stain, Erosion, Grade, Breakup, Worn Edges, Flow Warp, Layer Blur, Runoff). Nothing half-built is user-visible except one thing: the disabled Bake AA control. Cut that from the UI, and the feature surface is coherent.

Content: 65 surfaces / 18 categories, matching instances, 47 masks (14 patterns, 9 grunge, 5 scratches…), 2 effect presets, 1 example map. That’s a legitimate library. Thin spots (Rock, Moss, Misc, Dust = 1–2 each) are fine for 1.0. One real cleanup: there’s a typo’d Fabrc folder alongside Fabric in Instances/Thumbnails/Surfaces — move its asset, delete the folder.

The former Combine/Breakup ordering priority is obsolete: Combine IDs is retired, not a legacy functional producer. Keep ID Group planning tied to its supported contract rather than reactivating the retired path.

Ship plan — three working days
Day 1 — code deltas (only these, ~3h total):

Remove Bake AA control from the bake dialog (implement never; post-1.0 maybe).

Symbol-sweep + delete ghost methods from priorities.md (BuildCompositionResolutionMenu, MoveSelectedLayer, erosion slider leftovers, etc.).
Fix FilterPlugin.ini: add Docs/docs-assets/* — right now your packaged DOCS button ships a manual with broken images. This is a Fab blocker.
Delete *.png~ files, the 32 MB docsimgs…zip from the package path, empty Textures\Test\Type dirs, redirector sweep.
Day 2 — docs + listing (the biggest real gap): 6. Add Requirements/Installation section (UE 5.8, Win64, enable plugin). 7. Write the missing sections: Strata Carver, Layer Groups, ID Group (document its supported input/grouping contract; retired nodes have no Combine evaluation guarantee). 8. Rewrite the Instances section — it still documents the old Copy-as-Instance names. 9. Fab listing assets: thumbnail + gallery from your docs-assets beauty shots (≥1920×1080, <3 MB each, <25 MB total — you have 26 candidates, pick 6–8).

Day 3 — live gate + package: 10. Run your own preview checklist in the editor (the C++ build can’t validate the blit shader): Generated Mask, Pattern IDs + gaps, Breakup IDs/Gap/Edge/Pieces, Worn Wear, group preview, ID Group with two producers + Boundary, one real bake, Exact ID picker. 11. Run the ~18 automation test files. 12. Package from your local complete plugin, not git (git ignores Content/): ZIP one plugin folder, import into a clean 5.8 project, compile, open the sample, bake once, then submit. New package per engine version, Win64 only for now — matches your .uplugin, which is otherwise release-ready.

Explicitly cut from 1.0 (post-1.0 backlog): clipboard refactor, SMixtormatInternal pruning, the 250 KB file splits, duplicate menu consolidation, more surfaces/effects presets.

The mechanical Day-1 items (FilterPlugin.ini, ghost methods, folder cleanup, Bake AA removal) I can do right now — say go.