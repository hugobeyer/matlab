# Step 7: shared edge toolkit (jag, push, rim chips) for Cracks and Rock

> **Superseded in part (2026-10-01):**
> - Rock's side is now `08-RockFormation-Upgrade.md`; do not add anything to Rock here.
> - **Edge Push is dropped** everywhere (jag does the job).
> - Rim chips are **angular**: an asymmetric triangle, with a flat floor on half of them. See `rk_rim_chips` in `AgentDocs/Prototypes/RockFormation_EdgeToolkit.opencl.txt`.
>
> What remains for this step: build the shared `.ush` (jag + angular rim chips) from the proto's versions, and move Cracks onto it. Cracks' own look may change only where the chips become angular.

Paste `00-Shared-Rules.md` above this. Prerequisite: Step 2 is merged (Rock and Cracks are Generator layers). Do it before the Phase 4 Crack generator and the Lattice work if possible, so they reuse the toolkit.

## Goal
Lift Cracks' edge treatments into one shared include, and give Rock Formation the same controls. One implementation, one set of control names, used by every generator with piece edges.

## What exists (read these first)
- **`Shaders/Private/MixtormatCracks.usf`:**
  - **Jag:** `FCrackJag` and `CrackJagShift(Q, Sa, Ia, Sb, Ib, J)`. Every crack border is shifted sideways by a zigzag of straight segments, keyed on the *pair* of pieces, so both sides agree and nothing slivers. `CrackEdge` applies it to the exact bisector distance. Constants: `JagDetailPerDetail`. Amp comes from `Rough * RoughScale * RoughRefScale / Scale`.
  - **Push:** the unpushed crack distance is evaluated first. Within a band (`2 * |Push| + Width`) around the cracks, the lookup position is nudged by a periodic 2D noise. Piece tops never move. Constants: `PushPerRough` and `PushScalePerScale`.
  - **Rim chips:** `CrackChips(U, Key, Seed, Density, Size)`, scattered semicircle bites along one side of a crack. U is the position along the crack and Key is the pair key × 2 + side. Each bite widens the bevel (`HalfWidth + ChipBite`).
- **`Shaders/Private/MixtormatRockFormation.usf`:**
  - A tileable rock field: per-cell BSP polygon chunks, each a tilted slab with a top, a planar chamfer and a steep wall. It has edge distance and coverage outputs, plus `RockTopRamp`/`RockChamferRamp`/`RockWallRamp`.
  - **Facet chips:** up to three planar chip cuts per chunk (`U0..U2`, `R0..R2`, `ChipK` = tan 38°, `P.chip`).
  - **Warp:** bends the whole lookup coordinate, tops included.
  - The field is node-cached (`FMixtormatNodeCache`).

## Build: `Shaders/Private/MixtormatEdgeToolkit.ush`
- **Jag:** a generic per-edge zigzag offset. Inputs: the position along the edge, an edge key, amplitude, frequency, detail and seed. It returns a signed shift to add to an edge's signed distance.
  - Cracks keeps its pair-keyed consistency: both sides pass the same key.
  - Rock passes the cut-plane ID as the key. Rocks have gaps, so neighbours need not agree.
- **Push:** a band-limited lookup displacement. Inputs: the unpushed edge distance, the band width, amount and scale, a seed and the period. It returns the displaced lookup position. It must stay tileable (periodic noise on the tile).
- **Rim chips:** generalise `CrackChips`. Inputs: the position along the edge, an edge key + side, density, size and seed. It returns a bite radius that widens the bevel/chamfer locally.
- Move Cracks onto the include. Delete the old local copies; **Cracks' output must be bit-identical** before and after. Verify by reading carefully: same constants, same hashes, same seeds and salts.

## Rock Formation gets
- **Edge Jag** (amount, frequency, detail): a zigzag offset on each BSP cut / edge signed distance, keyed on the cut plane. The chamfer and wall ramps follow it, which is intended: it reads as broken rock.
- **Edge Push** (amount, scale): band-limited around the rock edges, tops never move. Two ways to do it; pick one and report why:
  - **(a) Re-evaluate the rock field** at the pushed position, inside the band only. Exact, but about 2× cost on the heaviest shader. The cache absorbs it when only non-field settings change.
  - **(b) Re-sample the cached field** with the push offset in a cheap second pass. Slightly softer, nearly free.
- **Rim Chips** (density, size): semicircle bites along each rock edge. The position along the edge comes from the cut plane's tangent; the key is the plane ID. Bites widen the chamfer locally, so the chamfer and wall ramps follow automatically.
- **Facet Chips:** today's three planar cuts, kept and renamed in the UI so the two chip kinds are clearly different.
- Everything stays inside the cached field where possible, and all of it must tile.

## Clean-up
- Rock's global **Warp** stays (it bends the whole layout); Push is the edges-only one. Make the UI hints say that clearly.
- **Control names:** the same labels on Cracks and Rock ("Edge Jag", "Edge Push", "Rim Chips"). Put them in an "EDGES" caption group in both generator panels.
- No back-compat: rename or remove Cracks' old property names freely if the shared naming is cleaner (Rough/Jag, Chip, ChipSize, etc.). Update the gather, render data, pass parameters, inspector, parameter authoring and docs.

## Static checks
- dxc for `MixtormatCracks.usf` and `MixtormatRockFormation.usf` (all entry points and stages) at HV 2018 and HV 2021.
- Compare each shader's globals against its `SHADER_PARAMETER` list.
- Grep for no leftover local copies of the jag, push and chip functions.
- Node cache keys include every new field-affecting property.

## Checklist for Hugo
- Cracks look identical before and after the move (same seed, same settings).
- Rock Edge Jag: the rock outlines zigzag, and the chamfer and wall follow.
- Rock Edge Push: only the edges wander, and the tops stay put.
- Rock Rim Chips: small bites nibble the rims, and the ramps follow.
- Facet Chips still work and read differently from Rim Chips.
- Everything is seamless, and Rock editing is still fast when you only change Amount or Height Scale (the cache).
