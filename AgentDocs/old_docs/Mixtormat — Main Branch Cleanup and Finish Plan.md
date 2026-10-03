Mixtormat — Main Branch Cleanup and Finish Plan

Audit basis: current hugobeyer/matlab main snapshot only

Purpose: establish the current architectural truth, remove stale/conflicting planning material, reduce the main collision hotspots, and finish the remaining generator/ID work without reopening systems that are already correctly implemented.

1. Executive Summary

The current main branch is not a failed refactor. It contains a mostly coherent new foundation: Generator Layers use ordered generator children/modules, height operations are shared through FMixtormatHeightBlend and MixtormatHeightOps.ush, ID Group preview plumbing exists, and Rock/Strata/Cracks/Pebbles are integrated into the generator path. The important correction is that generator support/shape masks must not implicitly become final layer coverage.

The mess comes from two places. First, several core files are still monolithic and carry too many responsibilities, especially the compositor gather and the two large editor widgets. Second, the AgentDocs/Prompts folder contains old plans that describe mutually exclusive architectures. If those prompts continue to be used, an agent can legitimately delete or rebuild systems that main already uses.

2. Current Architectural Truth — Keep This

2.1 Generator Layer model

Generator Layers own ordered EMixtormatLayerChildType::Generator children.

Each generator child wraps one FMixtormatGenerator with a Type and its payload.

Current generator payloads are StrataCarver, Cracks, RockFormation and Pebbles.

Material layers should not be treated as generator containers.

The module chain is already the correct base architecture; do not return to a single FMixtormatLayer::Generator payload.

FMixtormatGenerator

    bEnabled

    Type

    FMixtormatHeightBlend HeightBlend

    StrataCarver

    Cracks

    RockFormation

    Pebbles

2.2 Shared height operation model

Layers and generator modules use FMixtormatHeightBlend.

Shared shader math lives in Shaders/Private/MixtormatHeightOps.ush.

Generator module combination is centralized in MixtormatGeneratorBundle.usf.

Height Blend is an EMixtormatHeightOp value, not a separate enable/disable architecture.

bHeightBlendEnabled and the old per-module BlendOp/BlendSoftness/BlendAmount model should remain retired.

2.3 Generator field / support model

Generator modules may produce internal support/domain masks for their own solve and for downstream module inputs, but these are not final layer visibility.

Retire FMixtormatLayer::bGeneratorDrivesCoverage as a product concept. Generator layers should behave like Fill-style layers whose final visibility is controlled by normal layer opacity and masks.

Do not pass generator support automatically into MixtormatComposite.usf as layer coverage. A generator may publish an explicit mask that the artist can choose to use, but no generator output silently punches holes in the layer.

Keep module-local support/domain data only where it is needed for blending, clipping, named masks or downstream dependencies. It must remain separate from final layer coverage.

2.4 Rock and Cracks: one scalar height field

Rock Formation and Cracks should each resolve to one scalar height field before their FMixtormatHeightBlend operation. Edge/seam/crack character is produced by shaping and remapping that field, not by a parallel geometry/coverage system.

Rock Gap is valid only as a size dilation/erosion of each rock region before height shaping: positive Gap shrinks rock regions and widens the separation; zero keeps the original boundary; negative Gap expands regions and closes/overlaps the separation.

Rock Gap changes the domain used to build the Rock height field. It never changes layer coverage.

Rock chamfer/bevel character should be derived from the shifted boundary and remapped into the same scalar height field. Do not maintain a separate chamfer-SDF geometry output that competes with the height field.

Cracks use crack width/dilation plus profile/remap to create their scalar height field. Do not add a separate Gap concept for Cracks and do not treat crack width as transparency.

After field shaping, the module result enters the shared Height Blend operation against the running generator height.

2.5 Preview / ID Group plumbing

ChildOutput preview state exists.

ID Group is represented in child capabilities and preview selection.

Instance/reference preview resolution already exists through current preview-target resolution.

Layer-row/child-row preview/source-highlight state is already partially integrated and should be fixed locally if a visual bug remains, not rebuilt wholesale.

3. Main Problems in Current Main

3.1 Compositor gather is still a collision hotspot

FMixtormatGpuCompositor::RequestComposeInternal remains responsible for too many unrelated domains. It directly gathers and transforms layer state, IDs, masks, generators, effects, references, cache keys and preview/publication state. This makes every feature touch the same function and is the primary source of merge/agent collisions.

Generator gathering is still embedded directly in MixtormatGpuCompositor.cpp.

ID gathering and same-layer RegionIdMaps handling remain embedded there.

Layer composition properties and parameter sanitation are also embedded there.

MixtormatEffectGather.cpp proves the extraction pattern already works.

3.2 Editor files are too large

File

Approx. lines

Problem

SMixtormat_Layers.cpp

~8.3k

Hierarchy, drag/drop, menus, clipboard, child creation, preview state, flow placement.

SMixtormat_Inspector.cpp

~6.1k

Layer, ID, generator, effect, height, mask and legacy inspector UI in one unit.

MixtormatMaterial.h

~3.9k

Large runtime schema surface; manageable, but should not absorb editor orchestration.

MixtormatGpuCompositor.cpp

~3.4k

Gather, orchestration, preview/publication and multiple feature domains collide.

3.3 Height Blend naming is ambiguous

The height architecture is broadly correct, but naming can mislead future changes. FMixtormatHeightBlend contains Amount and Strength, while a layer also has HeightBlendAmount. That means two different concepts contain the word 'Amount' and one of them is effectively the layer-driven Strength.

Layer.HeightBlend.Amount        -> how much of the selected height op reaches the stack

Layer.HeightBlendAmount         -> driven Height Blend contest strength

Module.HeightBlend.Strength     -> module-local Height Blend contest strength

Do not redesign the math. The cleanup should document this contract and preferably rename the flat layer member to HeightBlendStrength when it can be done safely across parameter binding/UI. Until then, comments and UI labels must make the distinction explicit.

3.4 Cross-module dependencies are unfinished

Generator modules can be ordered and blended, but do not yet have a general Region input from modules above.

There is no general Stay Inside Region gate on a downstream module.

There is no generic upstream module Mask input.

There is no generic upstream module Flow input.

Cache keys therefore do not yet encode a complete module dependency graph.

3.5 Cross-layer published IDs are unfinished

The compositor still uses per-layer RegionIdMaps and nearest-above lookup semantics.

There is no compose-wide published-ID registry keyed by layer + producer + output.

Explicit cross-layer ID Source picking is therefore not a finished general system.

Lattice should not be implemented before this publication contract is stable.

3.6 Lattice Generator is not implemented

References to 'Lattice' exist in unrelated systems such as Craquelure and Pattern IDs, but the generator type set remains Strata, Cracks, Rock Formation and Pebbles. A true Lattice generator with generic outputs is still future work.

4. Cleanup Work — Exact Order

Phase 1 — Split compositor gather without behavior change

This is the highest-priority cleanup. It reduces collision risk before any new dependency system is added.

Source/MixtormatShaders/Private/Compositing/

    MixtormatLayerGather.cpp

    MixtormatMaskGather.cpp

    MixtormatIdGather.cpp

    MixtormatGeneratorGather.cpp

    MixtormatEffectGather.cpp        // existing

    MixtormatGatherCommon.h

Move existing gather statements; do not redesign algorithms while extracting.

Preserve cache-key input order exactly.

Preserve sanitation/default behavior exactly.

Keep RequestComposeInternal as orchestration: request setup, layer loop, group scopes, cache coordination, publication, final pass scheduling.

Do not modify shaders in this phase unless a compile dependency forces it.

Do not combine this with the Generator Region/Flow/Mask work.

While extracting generator gather, remove the automatic generator-support-to-layer-coverage plumbing as an intentional behavior change only if kept isolated and obvious; otherwise make it the first small follow-up immediately after the pure gather split.

Target outcome:

RequestComposeInternal becomes short enough to read as a pipeline rather than a database importer.

Generator changes stop colliding with ID/mask/effect gather changes.

Future agents can work in a feature-specific gather file.

Phase 2 — Finish generator module dependency inputs

Once gathering is separated, finish the module-chain model rather than adding one-off generator properties.

Add Region input: only modules above the current module in the same Generator layer that publish the required ID/centre/orientation outputs.

Add Stay Inside Region: clip the downstream module's support/domain or blend influence to the selected upstream region. This does not alter final layer coverage.

Add Mask input: named upstream mask outputs, optional invert, used as a module gate.

Add Flow input: selected upstream flow field/warped UV output.

Reject cycles by construction: pickers only expose earlier modules.

At None, output must remain identical to the current module behavior.

When an upstream dependency is selected, include the upstream node/output identity in the downstream cache key.

Implement first for Strata and Cracks; then Rock and Pebbles.

Preferred data ownership:

FMixtormatGeneratorInputRef

    SourceChildId / index

    OutputName

    Enabled / None semantics


FMixtormatGenerator

    RegionInput

    bStayInsideRegion

    MaskInput

    bInvertMaskInput

    FlowInput

The exact struct names can differ, but the important rule is one reusable input-reference mechanism, not separate IdSource/MaskSource/FlowSource inventions in every generator payload.

Phase 3 — Generalize published IDs across layers

After generator-local dependencies are stable, make IDs a compose-wide published resource.

FPublishedIdKey

    LayerId

    ProducerChildId (or layer-level producer)

    OutputName

Publish Pattern/ID Group/other ID-producer results through one registry.

Publish Generator-module ID outputs through the same mechanism.

Keep same-layer nearest-above as a convenience/default, not as the storage architecture.

Add explicit source selection for consumers that need cross-layer IDs.

A source that has not executed yet resolves to none; never silently fall back to another producer.

Any centre/orientation cache must key by the selected source identity, not only by numeric Region ID.

Phase 4 — Implement Lattice as a Generator

Only after generic publication exists should Pattern-style structural generation become a Lattice module.

Add EMixtormatGeneratorType::Lattice.

Publish Height, Region IDs, CentreUV, Orientation and useful named analytical masks. Do not publish an implicit final Coverage channel. A Lattice tile/gap mask may exist as an explicit named mask for downstream use.

Move only the geometry/pattern-generation responsibilities needed by the generator.

Do not delete old Pattern IDs until all consumers have generic replacements and Hugo explicitly approves removal.

Avoid reintroducing Pattern-specific exceptions in UV From IDs; consume generic published orientation/centre data.

Phase 5 — Freeze Up To Here

Freeze should be implemented only after dependency tracking is stable, because the correctness of a frozen prefix depends on knowing every upstream dependency and invalidation key.

Freeze a prefix, not a single isolated layer.

Pinned frozen entries must survive normal cache-budget eviction.

Resolution changes invalidate/rebuild the frozen prefix.

UI should clearly identify the frozen boundary and prevent editing inside the frozen prefix until unfrozen.

Do not use Freeze as a workaround for a slow or incorrect dependency graph.

Phase 6 — Small Strata completion

Expose the remaining lamination constants as authored parameters if still hard-coded: LaminaCount, LaminaWidth and BreakupScale.

Keep current shader behavior; this is parameter exposure, not another Strata rewrite.

Do this as a small isolated patch, independent of the architecture phases.

5. Editor Cleanup After Core Architecture Stabilizes

Do not start by physically splitting every editor file. First finish Phase 1 and the dependency architecture. Then split editor files by responsibility with no behavior changes.

5.1 Inspector split

Widgets/Inspector/

    MixtormatInspectorLayer.cpp

    MixtormatInspectorHeight.cpp

    MixtormatInspectorGenerators.cpp

    MixtormatInspectorIds.cpp

    MixtormatInspectorMasks.cpp

    MixtormatInspectorEffects.cpp

Keep common row builders/helpers centralized.

Do not duplicate selection-resolution logic.

Remove historical 'legacy treatment' UI only if the feature itself is explicitly approved for removal.

5.2 Layer-list split

Widgets/Layers/

    MixtormatLayerHierarchy.cpp

    MixtormatLayerActions.cpp

    MixtormatLayerMenus.cpp

    MixtormatLayerClipboard.cpp

    MixtormatLayerDragDrop.cpp

    MixtormatLayerChildren.cpp

Hierarchy/depth calculations should have one source of truth.

Instance/source highlighting should consume existing selection state, not maintain duplicate bookkeeping.

Generator-flow scoping should use shared ownership predicates such as MixtormatCanOwnGeneratorFlow.

6. Prompt Folder Cleanup

This cleanup is important. The current prompt directory contains historical instructions that conflict with main. Active prompts should describe only work that is still valid.

Current prompt

Action

Reason

00-Shared-Rules.md

KEEP

Still useful as the shared operating contract.

01-Height-Op-And-Coverage.md

ARCHIVE / RETIRE

Describes an older independent Height Blending model.

02-Generator-Layer.md

ARCHIVE / RETIRE

Generator Layer foundation already exists.

03-Cleanup-Cross-Layer-IDs-Lattice.md

REPLACE

3a conflicts with current generator children; split valid 3b/3c ideas into new prompts.

04-Freeze-Up-To-Here.md

KEEP FOR LATER

Still valid after dependency/publication architecture is finished.

05-Strata-Lamination-And-Breakup.md

REPLACE WITH SMALL TASK

Most shader work is done; keep only parameter exposure if still missing.

06-Generator-Offset-By-IDs.md

RETIRE

Its intent should be absorbed into generic Generator Module Inputs.

07-Edge-Jag-Push-Chips-Shared.md

RETIRE

Rock and Cracks no longer justify forcing one shared abstraction.

08-RockFormation-Upgrade.md

ARCHIVE

Current Rock already contains the upgraded jag/chamfer/rim-chip path.

09-UI-Backlog-Layer-List.md

ARCHIVE / VERIFY VISUALLY

Most requested state/preview/source logic is present in main.

10-Split-Compositor-Gather.md

KEEP / DO NEXT

Best immediate cleanup and prerequisite for safer feature work.

11-Generator-Module-Chain.md

REWRITE

Part 1 exists. Keep only missing Region/Mask/Flow dependency work.

11b-Unified-Height-Blend.md

MARK IMPLEMENTED + NOTE CLEANUP

Core shared struct/op/shader architecture exists.

HANDOFF-HeightBlend-Restore.md

RETIRE

Historical handoff; should not drive current work.

NEXT-Opus-Generator-Layer.md

RETIRE

Routing/state assumptions are stale.

REPORT-Step1-Sanity.md

ARCHIVE AS REPORT

Useful history, not an execution prompt.

7. New Active Prompt Set

Replace the current execution maze with a small linear set:

00-Shared-Rules.md

01-Split-Compositor-Gather.md

02-Generator-Module-Inputs.md

03-Cross-Layer-Published-IDs.md

04-Lattice-Generator.md

05-Freeze-Up-To-Here.md

06-Strata-Expose-Lamination-Controls.md

Reports/handoffs may remain in an Archive folder, but they should not sit beside active numbered prompts in a way that implies they are still executable.

8. Things Not To Reopen

Do not return to a single generator payload stored directly on FMixtormatLayer.

Do not remove EMixtormatLayerChildType::Generator; it is now the module-chain container.

Do not recreate bHeightBlendEnabled.

Do not split layer height operation and Height Blend into independent competing systems again.

Do not use generator support/domain as implicit layer coverage. Final layer visibility stays on the ordinary layer opacity/mask path.

Do not reinterpret Rock Gap as transparency. Rock Gap is boundary dilation/erosion used only to reshape Rock height.

Do not create a separate Rock chamfer-SDF geometry path or Crack gap/coverage path. Both generators resolve through scalar height shaping/remapping before Height Blend.

Do not add one-off IdSource fields separately to every generator when a reusable module input reference can solve the whole class.

Do not force Rock and Cracks into a shared edge helper unless the math is actually identical.

Do not implement Lattice before generic publication/input contracts are stable.

Do not delete Pattern IDs or compatibility behavior merely because Lattice is planned; remove only after replacements are complete and approved.

Do not mix architecture cleanup and visible behavior changes in the same large patch.

9. Completion Criteria

Mixtormat is structurally clean enough to continue product work when all of the following are true:

RequestComposeInternal is orchestration-focused and feature gathering lives in dedicated files.

Generator modules have one reusable dependency model for Region, Mask and Flow inputs.

Generator support/domain never implicitly changes final layer coverage.

Rock Gap behaves only as inward/outward boundary dilation before Rock height remapping.

Rock and Cracks each resolve through one shaped scalar height field before Height Blend.

Dependency pickers cannot create cycles.

Downstream module cache keys include selected upstream dependencies.

IDs can be published and addressed across layers using one generic registry.

Same-layer 'nearest above' remains available as a default convenience.

Lattice is a normal generator module using generic published outputs rather than Pattern-specific exceptions.

Height Blend terminology clearly distinguishes op Amount from contest Strength.

Active prompt files describe only current unfinished work.

Historical handoffs/reports are archived away from the active execution sequence.

Editor file splits can proceed as pure organization work without changing feature ownership.

10. Recommended Immediate Work Session

The next work session should do only the compositor gather split. It should not add Lattice, cross-layer IDs, new generator inputs or new shader behavior.

Extract layer-level gather.

Extract mask gather.

Extract ID gather.

Extract generator gather.

Move shared sanitation/hash/register helpers into a small gather-common header.

Reduce RequestComposeInternal to orchestration.

Update the active prompt folder so stale prompts cannot redirect the next session.

11. Final Direction

The correct way to finish Mixtormat from current main is not another broad refactor. Preserve the existing Generator Layer/module-chain and shared-height foundations, reduce the monolithic gather/UI collision points, then finish dependency publication in a strict order: local module inputs -> cross-layer IDs -> Lattice -> Freeze. Most of the current confusion can be removed simply by retiring obsolete prompts and refusing to let historical handoffs redefine the architecture.

Mixtormat — Main Cleanup and Finish Plan