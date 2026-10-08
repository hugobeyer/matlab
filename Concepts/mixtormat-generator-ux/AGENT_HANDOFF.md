# Mixtormat — Generator Relationship UX Mockups

## Files

- `index.html`: four interactive HTML/CSS concepts in one page.
- `styles.css`: all visual styling and proposed dimensions.
- No JS, icon libraries, SVG, frameworks, screenshots, external fonts, or network dependencies.

Open `index.html` in a browser. Switch the four concepts through the text tabs. Concept 2 uses a native expandable section. Concept 3 lets you click **Flow Carve / Flow** in the open source picker to reveal the connected state. Concept 4 has three state buttons: Selected, Menu open, Connected. Other controls illustrate proposed placement, not functional plugin commands.

## Recommended combination

**Concept 1 (display) + Concept 3 (creation).**

- Render incoming structural connections directly beneath their **target** generator.
- Visually distinguish incoming connections from generator-owned tools with text structure and a subtle inset/separator; do not rely on color alone.
- Context menu on target: **Warp using… → compatible source/output**. Selecting an option creates and binds the structural modifier in one action.
- Advanced inspector: always show **Source → Operation → Target** above parameters. Provide explicit source navigation and source replacement.
- Concept 2 can provide optional compact summaries for generators with many connections.
- Concept 4 is an optional expert shortcut, not the initial interaction.

## Canonical example

```text
Generator Layer 1
  Rock Formation
    Flow Carve                      [owned tool, publishes Flow]
  Pebbles
    Warp ← Rock Formation / Flow Carve  [incoming connection, consumes Flow]
  Cliff Strata                      [unconnected]
```

The position of one generator after another does **not** create a connection. Flow Carve remains owned by Rock Formation. A source-to-target operation is different from a settings-linked instance, copied generator, or grouped/composited output.

## Relationship semantics to verify in code

1. Generators publish typed fields (Height, Flow, UV, IDs, etc.).
2. Owned tools mutate their generator's output and retain ownership.
3. Structural Warp and Height Push have explicit source and target addresses/roles.
4. Only allow compatibility proven by the existing output capability definitions and runtime execution rules. **Do not infer compatibility from these visual examples.**
5. Source below target, cyclic dependency, missing source, disabled source, and unsupported output need distinct explanations, while preserving existing authored data for repair.
6. Do not silently reorder layers/generators. Show an inactive invalid-order connection and a manual fix path when required by actual execution constraints.
7. Copying duplicates settings independently; instance sharing links settings. Neither action implies an active field connection.
8. Generators grouped and operated on together are not equivalent to one generator supplying another.

## Proposed dimensions (not existing code measurements)

| Item | Proposed |
|---|---:|
| Generator/tool/connection row | 27 px |
| Hierarchy indent per level | 16 px |
| Owned-tool indent below generator | 32 px cumulative |
| Target connection indent | 32 px cumulative + 6 px inset |
| Visible text/icon size | Match shipped Mixtormat text; no icons in these mockups |
| Invisible click region around checkbox/disclosure | 20 px minimum |
| Horizontal padding | 8–11 px |
| Popup entry minimum height | 28 px |
| Popup / source chooser width | ~244 / 285 px |
| Two-selection menu | ~340 × 190 px |

Truncate source breadcrumbs with ellipsis inside narrow panels and show the full path in a tooltip. For duplicate names, include owning layer/breadcrumb even at normal panel width when ambiguous.

## Interaction rules

- Enable checkbox at row start; row body selects; click source name in inspector to navigate; right-click opens actions.
- Generator disclosure affects only that generator's subrows; hierarchy width and inspector docking remain unchanged, and the viewport keeps the remaining space.
- A connection may temporarily highlight the source and target on hover/selection, but never draw permanent full-stack wires.
- Row states: default, hover, selected, disabled, unresolved/missing, invalid order. Disabled/unresolved states must have a text reason.
- Prefer readable action labels over unexplained status abbreviations or small endpoint badges.
- Menu disabled choices must say **why** they cannot be used.

## Test matrix for implementation

- One incoming connection.
- Multiple incoming connections to the same generator.
- A source in a different layer.
- A missing source and a disabled source.
- Two generators with identical display names; resolve by stable identity, not by label.
- Independent copy versus settings-linked instance.
- Narrow hierarchy with truncated labels and full-name tooltip.
- Invalid execution order: explicit inactive state, no auto-reordering.
- Collapsed generator and all row interaction states.
- Confirm support and execution consequences of every typed output and operation combination.

## Agent directive

Before modifying Mixtormat, read `AGENTS.md`, relevant `AgentDocs/` documents, current hierarchy/inspector UI, source/output reference types, capability definitions, and generator gather/compose pipeline. Verify current UE/Slate API signatures against the available engine code/documentation. Preserve all existing features, parameters, source connections and authored assets. **Do not remove existing functionality without asking for approval.**

Implement the recommended concept with minimal duplication, clear typed connection semantics, and deterministic serialization. Document any runtime compatibility limitation instead of inventing generic connections that the shader graph cannot execute. Treat these HTML/CSS files as UX proposals, not as runtime specifications.
