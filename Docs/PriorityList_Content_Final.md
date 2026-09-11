I’d turn this into the final pre-release backlog, in roughly this order:

1. **Craquelure variation**

   * Groove depth variation per crack/region.
   * Profile/roundness variation.
   * Possibly width variation independent from groove depth.
   * Keep crack network stable; vary the resulting relief, not the topology unnecessarily.

2. **Grade overhaul**

   * Make Grade closer to a proper remap.
   * Input min/max → output min/max.
   * Separate R/G/B channel bias/offset.
   * Keep brightness/contrast/gamma if still useful.
   * Consider per-channel contrast only if it stays understandable.

3. **Fuzz Influence**

   * Add a layer/material `Fuzz Influence` parameter.
   * Let substrate/surface assets such as `DA_Fuzz...` provide the actual fuzz material behavior.
   * Mixtormat controls how much that channel/property participates.

4. **Fix the unusable mask block above effect inspectors**

   * Find why the generic mask UI is still appearing above effects/features where it does not belong.
   * Do not just hide it blindly; determine whether it represents the layer mask, scoped mask, or obsolete UI.
   * Inspector should make ownership obvious.

5. **Universal mask I/O architecture**

   * Every effect/feature should be able to receive a mask.
   * Effects/features that produce a meaningful scalar result should optionally publish that as a mask.
   * Support local sub-mask children under effects/features.
   * Preserve local shaping/blend/weight/UV.
   * IDs can remain special producers rather than forcing them into exactly the same model.
   * This should become one reusable system, not custom code per feature.

6. **Pattern ID → selectable region mask**

   * Allow Color ID / region selection directly from Pattern IDs.
   * Example: select the invalid/black region as `Grout`.
   * Ideally expose Pattern outputs such as `Cells`, `Grout`, `Edges`, etc. as named published masks.
   * This is cleaner than forcing the artist to reconstruct grout afterward.

7. **Layer-stack buttons**

   * Move `+ Layer` and `+ Fill Layer` to the bottom of the layer stack.
   * Icon + label.
   * They should remain visible after scrolling if practical.

8. **Modular Debug Visualization**

   * Every generator/effect/filter/mask feature should be able to publish debug channels.
   * One common debug registration mechanism rather than adding enum/switch logic every time.
   * Missing ones should include things like adhesion, peel SDF, stain water/saturation, erosion amount, wear, crack distance, ramp, edge field, IDs, etc.

9. **Debug visualization settings**

   * Settings panel controls for debug colors:

     * low
     * mid
     * high
     * invalid/ID background
     * potentially positive/negative SDF
   * Debug shaders read the shared configured palette.

10. **Persistent plugin settings**

    * Bake/export folder.
    * Default preview/bake resolution.
    * Quality.
    * processing scale.
    * AA/supersampling.
    * preview mesh.
    * possibly preview material/environment settings.
    * Restore these when Mixtormat reopens.
    * These belong in editor/plugin user settings, not in individual Mixtormat documents.

11. **Procedural Peeling cleanup**

    * Finish inspector terminology for the new adhesion/eikonal model.
    * Add a real `PeelCurlLength`; stop abusing `MicroMorph`.
    * Remove old `Seed`, `Flake Cells`, etc. wording where semantics changed.
    * Smooth curl profile; noise should affect the tear/adhesion, not make the paper itself noisy.

12. **Wet Stain accumulation**

    * Add the `Accumulation` control we just designed.
    * Additive/conservative liquid convergence.
    * Wetness based on actual accumulated liquid rather than renormalizing against total injection.
    * Debug views for Water / Saturation / Deposit would fit item 8.

13. **Height-effect consistency**

    * Audit every height-changing feature:
      `height delta → normal → AO`.
    * Make sure no feature derives normals from absolute height when it should use Δheight.
    * Make AO amount controllable where appropriate.
    * There is still the Ramp/Pattern AO separation issue worth cleaning up.

14. **Parameter system completeness**

    * New controls should support References/Drivers wherever existing scalar parameters do.
    * Avoid ending up with a second class of controls that cannot participate in your parameter system.

15. **Effect duplication/reordering integrity**

    * Scoped/sub-masks must move with their owner.
    * Duplicating an effect should duplicate its children correctly.
    * Published mask references should remain valid or fail cleanly after deletion/reorder.

16. **Bake/export finishing**

    * Predictable output naming.
    * overwrite behavior.
    * packed channel presets.
    * selected resolution/quality actually reflected in bake.
    * remember output folder.
    * obvious completion/error feedback.

17. **Inspector cleanup pass**

    * Consistent section names and row ordering across all effects.
    * Remove controls that no longer do anything.
    * Remove legacy terminology.
    * Ensure zero/default values really mean neutral/no-op where expected.

18. **Before Fab/package**

    * Fix Ramp double-AO risk.
    * Packaging/build from clean install.
    * Plugin icon/metadata/version.
    * Human-facing docs/screenshots.
    * A small set of example materials showing Pattern → Grout mask, Worn Edges, Peeling, Wet Stain, Craquelure, Grade, etc.

The biggest architectural item is **#5**. If you solve mask input/output/submask ownership properly, #4, #6, a large part of #8, and several future features become much easier instead of accumulating one-off paths.
