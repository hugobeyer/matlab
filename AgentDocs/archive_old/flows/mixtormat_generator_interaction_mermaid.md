# Mixtormat — Height Push / Structural Warp UX (Proposal)

**Status:** Proposed v1, not implemented. Based on the combined audits of the `main` source at `0e8b6cf`. Preserve Runtime → Shaders → Editor ownership, existing serialized reference types and authored order. No implicit source/target fallback.

## 1. Scope and authored order

Solid arrows indicate one *example* flat child order. Dashed arrows indicate reference or mask relationships. Push and Warp may be reordered deliberately; their composition is not commutative.

```mermaid
flowchart TB
    subgraph Earlier["Optional earlier generator layer"]
        E["Completed compatible typed output"]
    end
    subgraph Stack["Generator layer — authored top-to-bottom order"]
        direction TB
        A["Noise A<br/>Generator · signed Height"]
        B["Rock Formation<br/>Generator"]
        F["Flow effect<br/>Scoped to Rock · Flow / UVMap"]
        H["Height Push<br/>Structural module"]
        M["Mask<br/>Scoped to Height Push"]
        W["Structural Warp<br/>Structural module"]
        T["Strata Carver<br/>Target generator"]
        A --> B --> F --> H --> M --> W --> T
    end
    A -. "signed Height" .-> H
    F -. "typed Flow / UVMap" .-> W
    H -. "TargetChildId" .-> T
    W -. "TargetChildId" .-> T
    M -. "gates Push only" .-> H
    E -. "optional earlier source" .-> H
    E -. "optional earlier source" .-> W
    classDef module fill:#17364b,stroke:#62b6d9,color:#ffffff
    classDef target fill:#254534,stroke:#8ace9a,color:#ffffff
    class H,W module
    class T target
```

**Rules:** Source must evaluate earlier. Height Push consumes completed `ScalarSigned` Height from an eligible generator and targets later unscoped **Strata Carver only** in its same layer. Structural Warp consumes `Flow` or `UVMap` from a compatible earlier completed scoped effect and targets a later enabled unscoped generator in the same layer. Earlier-layer source references are permitted when valid. Scoped masks remain beneath and gate their interaction module; the module itself is **not** nested under the target.

## 2. Target-first user flow

```mermaid
flowchart TD
    A["Right-click target generator"] --> B{"Add structural module"}
    B -->|Height Push| C["Insert Height Push<br/>before target"]
    B -->|Structural Warp| D["Insert Structural Warp<br/>before target"]
    C --> E["Assign explicit target GUID"]
    D --> E
    E --> F["Source chip: Set Source"]
    F --> G["Choose compatible earlier output"]
    G --> H{"Type, order and scope valid?"}
    H -->|Yes| I["Save Source reference<br/>Show connection in stack"]
    H -->|No| J["Show reason<br/>Keep existing reference"]
    F -->|Leave unset| K["Explicit unconnected no-op"]
    I --> L{"Reorder later?"}
    L -->|Still valid| M["Allow move"]
    L -->|Would invalidate link| N["Block move and explain"]
    J --> G
    classDef success fill:#254534,stroke:#8ace9a,color:#ffffff
    classDef warning fill:#583c25,stroke:#dfab65,color:#ffffff
    class I,M success
    class J,K,N warning
```

**UI:** Compact collapsed rows show `Source · Output → Target`; selected interaction module expands to show chips and main parameter. Target generator shows derived incoming count (`1 PUSH · 1 WARP`). Continue showing existing inspector controls for the first release. Chip menus use the same validation as Inspector.

## 3. Scope and implementation ownership

```mermaid
flowchart TB
    subgraph P1["Phase 1 — Correctness / parity"]
        A["Height Push row toggle, icons and menus"]
        B["Clipboard, GUID remap, duplicate, group/paste parity"]
    end
    subgraph P2["Phase 2 — Connection validation"]
        C["Shared Runtime typed-link status helper<br/>no new serialized fields"]
        D["Editor reorder/move guards<br/>actionable invalid reasons"]
    end
    subgraph P3["Phase 3 — Stack presentation"]
        F["Source/Target chips + status"]
        G["Linked-row highlighting + incoming counts"]
    end
    subgraph P4["Phase 4 — Target-first creation"]
        H["Target context menu → Add module"]
        I["Insert before target, set TargetChildId<br/>user picks Source"]
    end
    P1 --> P2 --> P3 --> P4
    subgraph Unchanged["Behavior to preserve"]
        J["Flat authored child ordering, typed source refs"]
        K["Gather/GPU/shader execution, per-target D/B state"]
        Q["Signed Height, tiling, IDs, UVs and bed behavior"]
    end
    subgraph Deferred["Out of v1 scope"]
        X["Drag-to-connect"]
        Y["True nesting / interaction-group types"]
        Z["New shaders or implicit fallbacks"]
    end
```

| Ownership | Proposed scope |
|---|---|
| **Runtime** | Reuse source/target reference model; add small read-only validation/status resolver (and any confirmed identity remap parity fix). No serialization/schema changes. |
| **Editor** | Fix parity + clipboard; update hierarchy rows/badges/chips, connector highlighting, inspector menu reuse, target-first creation, reorder guards. |
| **Gather/GPU/Shaders** | Preserve evaluation, source completion/demand, per-target signed bedding/warp fields and coordinate contracts; no algorithm changes. |

### Validity feedback

- **Valid** — resolved typed Source and later Target.
- **Unset** — explicit no-op; never guess a source.
- **Disabled** — preserved connection while module or dependency is off.
- **Missing** — stale or absent GUID; show reason.
- **Wrong type or scope** — rejected with reason.
- **Forward-order** — source not completed earlier, or target not later; prevent invalid reorder by default.
- **Neutral value** — valid but current amount makes no effect.
- **Cyclic** — not a normal stored state in the current strictly ordered structural-module model; do not introduce a generic cycle badge.

### Approval boundaries

1. **First release:** phases 1–4 above.
2. **Validation:** share actual Runtime predicates between gather and editor where possible; do not duplicate rules that may drift.
3. **Keep Inspector:** mirror current controls until inline source/target selection is proven.
4. **Later:** source-to-target drag menu and optional connector lines, without changing authored order.
5. **Not approved:** real target nesting, new interaction groups, implicit auto-source, changing signed height / UV / IDs / tiling or shader behavior.
