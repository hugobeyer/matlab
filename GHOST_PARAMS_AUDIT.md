# Ghost Params / Dupes Audit (glance, symbol-traced)

- Method: enum entries vs `Enum::` references across struct → gather → passes → inspector → JSON. Name search only, no full semantic review.
- Scope: effects (8), generators (11), child types (27), masks (3), behaviors (6), bindings (owners, drivers, refs), authoring JSON (spot-checked).

## GHOSTS — authored, never read

- `EMixtormatDriverSourceKind::ChildMask` — defined (`Runtime/Public/MixtormatParameterTypes.h:94`), offered in UI (`Widgets/SMixtormat_Parameters.cpp:1820`), zero resolve/gather readers. `Runtime/Private/MixtormatParameterDrivers.cpp:55-69` handles None/CombinedMask/RegionIds only. Authoring one silently no-ops. Fix: wire it or remove from UI.
- `FMixtormatParameterDriver::IdMapping` — default RandomPerId (`MixtormatParameterTypes.h:240`), set in UI (`SMixtormat_Parameters.cpp:1883`), never read anywhere in Source. SpecificId/IdRange are dead. Fix: wire it or delete field + UI.

## RETIRED — present, documented, remove when approved

- `HeightPush` / `StructuralWarp` — REMOVED 2026-10-23 (enum entries + recipe check deleted; `Behavior=24, Boolean=25, Surfacing=26` pinned so saved assets keep values).
- Stain asset defaults x6 (`Runtime/Public/MixtormatEffect.h:114-138`) — self-documented unread, kept for asset load.
- `RampShape` (`Runtime/Public/MixtormatGeneratorTypes.h:122`) — deprecated serialized-only, replaced by LedgeWidth.
- Deprecation comments at `MixtormatGeneratorTypes.h:309,1197,1793` — signed-normalization, serialized-only field, per-generator blend removal. Comment-sourced, not fully read.

## VERIFIED CLEAN — suspected, disproven

- NoiseV2 is a live helper lib (`Shaders/Private/MixtormatNoiseV2.ush`), consumed by `MixtormatNoise.usf`. Not a dupe.
- `CombineId`: readers in `Compositing/MixtormatGeneratorGather.cpp:1242`, `Runtime/Private/MixtormatOutputReference.cpp:317,507`, `Runtime/Private/MixtormatParameterBinding.cpp:85,224,669`.
- `ProceduralType` (authoring) → `Type` (render) is one pipeline (`Shaders/Private/MixtormatGpuCompositor.cpp:2134`).
- `EdgeWear`: live (`Shaders/Private/Effects/MixtormatWornEdgesPasses.cpp`, `Shaders/Private/MixtormatEdgeWear.usf`).
- SSS/Fuzz, ViewScale/CameraPitch/Quarter, TilingX/Y, TipRoundness/CrackSlip/CrackTilt: struct + gather + inspector all present.
- Coverage: generators 11/11, effects 8/8, MaskSource 3/3, ValueType 7/7, CombineMode applied GPU-side numerically (`Shaders/Private/MixtormatGpuGeneratorPasses.cpp:1478,1946,2024,2155`), RefMode Link/Follow consumed (`Runtime/Private/MixtormatGroupControls.cpp:133`, `MixtormatParameterBinding.cpp:1549`), `MixtormatShaderParamScanner::Scan` wired (`Widgets/SMixtormat_Parameters.cpp:987`).
- Stain/erosion/relief roughness+AO all live end-to-end (Stain.usf:112/248, CarveShade.usf:34, ReliefIdPasses, SurfaceIds.usf); the 6 stain asset fields are already out of the inspector (no EditAnywhere).
- No header dupes: `MixtormatGroupControls.h`, `MixtormatGroupParameterTypes.h`, `MixtormatMask.h`, `MixtormatSurface.h`, `MixtormatScalarRemap.h` all referenced.
- `EMixtormatNoiseType` fwd-decl in `MixtormatOutputReference.h:11` is legal, not a dupe.
- Zero `DEPRECATED` markers outside the known headers.

## OPEN QUESTIONS — not ghosts, need owner call

- `OutputReference` child has no owner mapping (`MixtormatParameterBinding.cpp:63-107` default false). By design (references carry no payload) or gap?
- `EMixtormatSurfacingType` has one entry (`Skin`); Skin-only owner gate is vacuous today. Fine unless more types planned.
- `Mask.TilingY` has no authoring JSON key (only `Mask.TilingX:Int`). Falls back to defaults — asymmetry or intent?
- Flow naming overlap (`Behavior FlowField` vs `FlowWarp` effect vs `NativeFlow` passes vs `Flow.usf`) not verified as dupe or distinct — needs a read, not a grep.

## FOLLOW-UPS

- Exhaustive authoring-JSON ↔ struct diff (scriptable, spot-checked only here).
- Decide ChildMask + IdMapping: wire or delete (unshipped, no compat cost).
- Delete retired fields once approved (no migration stubs per project rule).
