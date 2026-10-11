**Four annotated concepts for Mixtormat generator relationships**  
Style locked to your screenshots: near-black ground (~#0E0E0E), muted gray text (#A8A8A8 / #C8C8C8), thin 1 px separators, restrained teal selection, 18–20 px row height, small glyphs (14–16 px), compact density, no neon/glow/oversized cards.

---

### Concept 1 — Target-Owned Connection Rows

**Explanation**  
Generators stay at normal generator indentation. Incoming connections appear as sibling-style rows *under the target generator*, not under the source. Connection rows use a distinct left glyph (← or small connector diamond), a two-part text structure (`Warp ← Rock Formation / Flow Carve`), and a 1 px inset surface or hairline separator so they never read as owned tools. Owned tools (Flow Carve) keep their normal ownership indentation and icon. No permanent “Connections” header; rows simply interleave with owned tools.

**Hierarchy (example)**  
```
▾ Generator Layer 1                          GEN  REP
  ▾ Rock Formation                           GEN
      ⚡ Flow Carve          TARGET·GEN  CARVE   ← owned tool
  ▾ Pebbles                                  GEN
      ⬡ Warp ← Rock Formation / Flow Carve   WARP  ← connection row
      ▸ Height Push …                        HPUSH
  ▸ Cliff Strata                             GEN     (unconnected)
```

**Annotations (proposed px from screenshots)**  
- Generator indent: 8 px from panel edge  
- Owned-tool indent: +16 px  
- Connection-row indent: +16 px (same as owned tools) + 4 px extra left padding for glyph  
- Row height: 20 px (fixed)  
- Icon size: 14 px visible, 20×20 invisible hit target  
- Enable checkbox / disclosure: 12×12 visible, 18×18 hit  
- Text: 11–12 px, truncation with “…” + full-name tooltip on hover  
- Connection row subtle surface: #161616 fill or 1 px top/bottom hairline #2A2A2A  
- Selected: teal left bar 2 px + #1A2A2A fill (existing treatment)  
- Hover: #1A1A1A  
- Disabled / missing source: dimmed text + “?” glyph + tooltip “Source missing or disabled”  
- Temporary endpoint highlight on hover/select: thin dashed teal line from source row to connection row (disappears on deselect)  
- Collapsed generator: only name + badge; connection count appears as small right-aligned “1←” chip only when collapsed  

**Inspector**  
When connection row selected: compact header `Source → Warp → Target` (Rock Formation / Flow Carve → Warp → Pebbles) before any Amount/Radius sliders.

**Tradeoffs**  
+ Extremely clear ownership of the relationship (target owns the row).  
+ No extra chrome.  
– Multiple incoming connections lengthen the target’s children list.  
– Source in another layer still requires a short path string (“Layer2 / Cliff Strata”).

---

### Concept 2 — Collapsed Summary + Local Connection Details

**Explanation**  
Every generator that can receive connections shows a compact right-aligned summary chip (`1 WARP` or `2←`). Clicking the chip or a disclosure triangle expands *only that generator’s connection section* inline, revealing full operation / source / enable / edit actions. Owned tools remain above or below the connection block with a thin separator. Users distinguish:  
- **Connected** = filled chip + expanded row  
- **Available output** = source listed in “Warp using…” menu with green/ready state  
- **Instance** = existing “link” glyph next to name (settings-shared, not field-connected)

**Hierarchy (expanded)**  
```
▾ Generator Layer 1
  ▾ Rock Formation                    GEN
      ⚡ Flow Carve
  ▾ Pebbles                           GEN          1 WARP
      ── connections ──
      ⬡ Warp  Rock Formation › Flow Carve   [✓]  [⋯]
  ▸ Cliff Strata                      GEN
```

**Annotations**  
- Summary chip: 14 px tall, right-aligned, 4 px padding, muted teal text when active  
- Expanded connection block: 4 px top/bottom padding, 1 px separator, same 20 px rows  
- Enable toggle lives on the connection row (existing checkbox style)  
- Source navigation: click source name jumps selection to that generator  
- Actions: “…” menu = Edit / Break / Swap Source  
- Collapsed: chip only; no extra rows  
- Narrow panel: chip truncates to “1←”; full tooltip  
- Missing source: chip turns muted red “1?”  

**Tradeoffs**  
+ Hierarchy stays short until needed.  
+ Clear count at a glance.  
– One extra click to inspect details.  
– Risk of “hidden connections” if users never expand.

---

### Concept 3 — Target-First Popup Workflow

**Explanation**  
Right-click any generator → “Warp using…” (or “Height Push from…”). Menu lists only compatible sources grouped by layer, with output type shown. Selecting a source *creates and wires* the modifier in one action. Result appears immediately as a connection row under the target (same visual language as Concept 1). Disabled entries show reason (“No Flow output”, “Would create cycle”, “Different layer group”).

**Menu mock (compact)**  
```
Warp using…
  This layer
    Rock Formation › Flow Carve          ✓
    Rock Formation › Height              (no Flow)
  Generator Layer 2
    Cliff Strata › …                     (incompatible)
  Search…
```

**After creation**  
Same hierarchy as Concept 1. Inspector opens with the new Warp already selected and source→target header filled.

**Annotations**  
- Menu: existing context-menu style, 22 px rows, 12 px icons, muted headers  
- Disabled: gray text + tooltip reason (never silent)  
- Invalid execution order: menu entry disabled with “Would run after target — reorder first”  
- No permanent lines; temporary highlight of source + target on menu hover  

**Tradeoffs**  
+ Fastest creation path; zero empty Warp objects.  
+ Discoverable via right-click.  
– Requires users to start from the *target*.  
– Menu can grow long with many layers (search/grouping mitigates).

---

### Concept 4 — Two-Selection Marking Menu

**Explanation**  
Select two generators (Ctrl/Cmd-click). Invoke marking menu (right-click or dedicated “Connect” hotkey). Menu shows only supported directed relationships with actual names:  
`Rock Formation / Flow Carve → Warp → Pebbles`  
If direction is ambiguous, two explicit choices appear. A normal popup alternative is always available for accessibility. After connection the hierarchy updates exactly as Concept 1.

**States shown**  
1. Before: both rows selected (teal multi-select).  
2. Menu open: radial or compact list with source → op → target.  
3. After: connection row under target; temporary dashed highlight fades.

**Annotations**  
- Marking menu: 180–220 px diameter or vertical list matching existing menus  
- Only valid ops shown; invalid direction grayed with reason  
- Accessibility: same options in ordinary context menu under “Connect…”  
- Narrow panel: menu still fits because it is transient  

**Tradeoffs**  
+ Extremely fast for power users who already know both ends.  
+ Explicit direction removes ambiguity.  
– Least discoverable for new users (mitigated by popup alternative).  
– Multi-select is an extra step.

---

### Recommendation

**Concept 1 (Target-Owned Connection Rows) + Concept 3 (Target-First Popup) as the primary pair.**

- Concept 1 solves the “looks the same” problem with the least new chrome and keeps the hierarchy readable.  
- Concept 3 removes the empty-Warp intermediate step and makes creation a single, directed action.  
- Concept 2 is a good progressive-disclosure fallback if hierarchy length becomes an issue.  
- Concept 4 is excellent as a power-user accelerator but should not be the only path.

Invalid order is never silently fixed: the creation menu or connection row shows a persistent warning glyph + tooltip (“Source executes after target — move source earlier or break link”). Temporary hover/selection lines are allowed; permanent rails are forbidden.

All four concepts stay inside the existing 20 px density, teal selection, and icon language of your screenshots.