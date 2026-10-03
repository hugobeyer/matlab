# Mixtormat — Modularization Possibilities

> **Brainstorm only; not source-verified.** These are candidates to inspect and prioritize, not confirmed duplication or approved refactors. Keep this list separate from the mask-shaping implementation and the planned visual ramp prototype.

## GPU and render pipeline

- **Mask shaping** — shared normalization, input levels, balance, contrast, offset, and invert. Separate generators from the shaping stage.
- **Blend modes** — shared blend-operation evaluation and consistent parameter binding. Keep blend operations independent of mask shaping.
- **Mask placement** — shared tiling, UV offset, rotation, and flip behavior for texture-backed masks.
- **Scalar-field normalization** — a reusable GPU min/max reduction and remap service for masks and other scalar fields.
- **Blur and curvature filters** — reusable scalar-field filters while keeping producer-specific generation separate.
- **Preview publishing** — shared debug snapshots, preview selection, and final-output routing.
- **Typed output publication** — common output contracts for masks, region IDs, flow, UVs, and height.
- **ID sampling** — reusable region/ID lookup, validity checks, and sampling policy.
- **Effect scheduling** — common lifecycle for gather, queue, compose, and post-compose stages where effects share that workflow.
- **Shader parameter packing** — typed per-effect parameter definitions instead of repeated anonymous packing, where layouts can remain version-safe.

## Runtime data and authoring

- **Parameter binding** — shared address resolution, reference traversal, validation, and value conversion.
- **Document editing operations** — undo, clipboard, duplication, remapping, and layer/child identity operations outside Slate.
- **Effect metadata** — a single source for effect identity, controls, capabilities, and input/output declarations.
- **Mask shaping payload** — shared authored and render-side data rather than repeated per-mask declarations.
- **Curve remapping** — future reusable evaluator for mask bias and other scalar controls. The UI interaction, curve format, interpolation, and migration strategy still need visual/design decisions.

## Editor and services

- **Asset registry access** — an injectable/cached query service with explicit invalidation when assets change.
- **Inspector controls** — reusable parameter-row construction driven by metadata, preserving current UI ranges and interaction behavior.
- **Preview refresh orchestration** — shared invalidation and scheduling rules across editor actions.
- **Layer-stack commands** — reusable editor commands independent of widget construction.

## Cross-cutting infrastructure

- **Cache policy** — common lifecycle/invalidation patterns, with cache budgets tailored to each data type.
- **Diagnostics and validation** — reusable validation/reporting for references, published outputs, and authored values.
- **Test seams** — focused interfaces for GPU dispatch, asset queries, and document operations so each layer can be tested at its boundary.

## Prioritization guidance

1. Confirm repeated responsibility and call paths in source before refactoring.
2. Prefer a clear shared boundary over merging different producers or changing public data formats.
3. Preserve saved values, UI ranges, UI hard clamps, and output behavior unless a deliberate change is approved.
4. Add regression coverage before moving behavior across module boundaries.
5. Profile performance claims; modularity alone does not prove a speed improvement.
