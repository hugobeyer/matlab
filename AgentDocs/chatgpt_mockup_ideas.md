The four images explore two separate problems in Mixtormat:

1. How connections appear in the hierarchy.
2. How users create connections without manually configuring `None → None`.

## The four concepts

1\. Target-owned connection rows

The relationship appears directly underneath the generator receiving it.

▾ Generator Layer 1     ▾ Rock Formation         ⚡ Flow Carve     ▾ Pebbles         ↳ Warp ← Rock Formation / Flow Carve       Cliff Strata

You can immediately tell that Pebbles receives Flow from Rock Formation. The Warp belongs to Pebbles, not Rock Formation.

2\. Collapsed summary

▾ Pebbles       2 incoming     ↳ Warp ← Rock Formation / Flow Carve     ↳ Warp ← Noise / Flow  ▸ Pebbles       2 incoming

Same relationships, but you can collapse connection details. Better for large stacks, although connections become less visible.

3\. Target-first popup

Instead of creating an empty Warp:

Right-click Pebbles   → Warp using...       Rock Formation         Flow Carve / Flow    ✓       Cliff Strata         No Flow output      unavailable

Selecting `Flow Carve` automatically creates the Warp with the correct source and target.

This eliminates the current two-endpoint setup.

4\. Two-selection marking menu

Select Rock Formation and Pebbles, then open a compact menu that offers:

- Warp Pebbles using Rock Formation.
- Reverse direction, only when compatible.
- Other valid operations.

Good for experienced users, but less discoverable and more complex when there are multiple possible outputs.

## My recommendation

Combine Concept 1 + Concept 3.

Use target-owned rows for displaying connections and target-first popups for creating them.

The ideal interaction becomes:

Pebbles

GEN

Warp ← Rock Formation / Flow Carve

Right-click Pebbles → Warp using…

Three important rules:

- Owned tools remain visually different from connections.
- Cliff Strata being below Pebbles does not imply any relationship.
- If a source executes after its target, show an invalid-order warning; never silently reorder.

I would keep the two-selection marking menu as a possible later shortcut, not the primary workflow. It adds interaction complexity without improving clarity as much as the first and third concepts.