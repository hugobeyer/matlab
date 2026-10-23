# Behavior Inspector — Remove Dropdowns

> Repository: `hugobeyer/matlab`  
> Branch: `feature/behavior-system-v2`  
> Read root `AGENTS.md` and `AgentDocs/UI.md` before editing. Follow canonical ownership, Slate theme tokens, and existing parameter bindings. No tests, builds, diagnostics, automated validation, or extra documentation unless explicitly requested.

## Requested changes

Replace all Behavior Inspector dropdown selectors with explicit segmented controls, toggles, or numeric weights according to their semantics. Remove obsolete source/target routing in UI **and runtime**, consistent with `AgentDocs/FINAL_BEHAVIOR_PLAN.md`; do not merely hide it.

Affected controls in `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` (line references from `c57c418`):

| Area | Existing control | Change |
|---|---|---|
| Push | Signed Height Field (1039) | Inherit working height automatically |
| Carve | Carve Mode (1201) | Segmented Standard / Traced |
| Carve | Signed Boundary / SDF (1236) | Use owner's compatible boundary |
| Carve | Trace Operation (1293) | Segmented Groove / Deposit |
| Carve | Steering Source (1296) | Consume working field |
| Warp | Direction Field (1427) | Consume accumulated Flow |
| Warp | Sampling Stage (1477) | Segmented Before / After; retain existing eligibility |
| Warp | Flow Mode (1573) | Segmented Flow / Gravity where still supported |
| Warp | Steering Source (1576) | Consume working field |
| Deform | Deform Mode (1818) | Segmented Standard / Traced |
| Deform | Direction Field (1855) | Consume accumulated Flow |
| Deform | Steering Source (1961) | Consume working field |
| Flow Field (legacy) | Flow Mode (902) | Replace via Flow Generator architecture; no redundant Behavior |
| Flow Field (legacy) | Steering Source (905) | Weighted MODE contributions |
| Feather | Mode (2038) | Segmented Blur / Eikonal |
| Feather | Feather Side (2095) | Segmented Both / Inside / Outside |
| Shared field | Combine (824) | Segmented operation choices where applicable |

Keep Warp `Mode Mix` as a 0–1 slider; do not convert continuous mixtures into binary modes. For Flow steering, use weighted Height, Slope, Curl, Constant controls. Reuse `SMixtormatSegmentedControl`, preserve linked parameter writes and serialized enum values. Hide irrelevant controls instead of leaving disabled dropdowns. Check all call sites before removing source-menu builder declarations or compatibility APIs.
