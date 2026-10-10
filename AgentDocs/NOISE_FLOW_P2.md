# Noise/Flow P2 — implementation status

Branch: `feature/behavior-system-v2`. This is an **incomplete P2 implementation**, not a sign-off.

## Implemented

- The native Noise generator pass now accepts the already accumulated `RunningHeight` snapshot from its actual evaluation position.
- The Noise MODE `Slope` basis uses this preceding height via `ModeSlopeHeight`, with `ModeUseSlopeHeight=1` when the snapshot exists.
- No preceding height means the Slope basis is disabled; it does not use its own Noise height as a surrogate.
- The `Height` MODE basis continues using the node's own native Noise height. The existing downstream height normalization and output gate are unchanged.
- No extra flow solver or source/target picker was added.

## Not yet implemented

- Scoped Flow generator evaluation beneath another generator.
- A persistent per-generator canonical working Flow field and per-child ordered accumulation.
- Enqueuing `AddNoiseFlowComposePass` with real Add/Mix/mask inputs.
- Capturing height changes from preceding scoped Push when evaluating scoped Flow.
- Automatic accumulated-Flow consumption by Distort and Deform.
- Flow transport after Distort and valid canonical intrinsic-Flow initialization.
- Functional Inspector visibility for Slope and Add/Mix.
- End-to-end correctness for mask nesting, save/load, undo, shader/runtime/performance tests.

## Validation

Code was inspected for the modified parameter/call signature and fetched through GitHub. **Unreal Engine 5.8 compilation, PCD3D_SM6 shader compilation, GPU runtime testing and UI verification were not run.** No completed-P2 claim is justified.

## Required next integration

Introduce a canonical working Flow texture and validity in the generator-local evaluation loop; run scoped child Flow generators and Behaviors in authored order, snapshot their current height for Slope, compose generated vectors with masks, and route the resulting Flow to the existing coordinate-tracing implementations. Do not add an unrelated solver or treat normalized legacy `FlowDirection` as canonical.
