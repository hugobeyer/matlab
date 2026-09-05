# Compositor performance, and the colour ID mask

Two pieces of work in one pass: cutting what the compositor recomputes on every frame of a slider
drag, and adding the one placement control that comes from outside the tool.

Everything here is verified by `Mixtormat.Compositor.*` in
`Source/MixtormatEditor/Private/Tests/MixtormatCompositorTests.cpp`, which drives the compositor
end to end on a real RHI. Run the suite after any change to the graph:

```
UnrealEditor-Cmd.exe MatLab.uproject -ExecCmds="Automation RunTests Mixtormat; Quit" -unattended -nopause -nosplash
```

## Where the time was going

The panel recomposites the entire layer stack on every frame the mouse is down. Nothing was
cached and nothing was skipped, so the cost of moving any slider was the cost of the whole stack.

### The craquelure network, kept between composites

Growing a propagated network is one full-resolution dispatch per pixel of reach — up to a
thousand of them, each doing on the order of eighty texture loads per pixel. It is by a wide
margin the most expensive thing in the graph.

Almost nothing a user touches while tuning changes the network. Width, Contrast, Balance, Offset,
Weight, Invert, the blend mode and all four relief controls are applied to the finished distance
field, not during growth. Regrowing it for those was the dominant cost of using the tool.

`FMixtormatNetworkCache` keys the finished distance field on exactly the parameters the seed and
growth passes read. A hit registers the kept pooled target straight into the new graph and skips
the seed, the growth loop and the whole jump flood; everything downstream still runs every frame,
so the controls that shape a crack stay live.

Bounded by bytes rather than entry count, because an entry is a full-resolution RGBA32F — 16MB at
1K but 268MB at 4K. A fixed count that is comfortable at preview resolution would pin well over a
gigabyte after a 4K export.

Lattice mode is deliberately not cached: its distance falls out of the same single pass that
writes its mask, so there is nothing to skip.

The one case this makes slower is dragging a growth parameter, where every frame is a miss and
now also pays a `ConvertToExternalTexture`. That is small next to the growth passes themselves.

### Filters that were paying full price to do nothing

- **Erosion at Amount 0** is an exact identity in the shader — Placement falls to zero, the height
  comes back as it went in, the normal is copied through. It was still running six passes at twice
  the composition resolution, so four times the pixels, plus the resample pair. It now costs one
  clear. The clear is what makes the skip exact: at Amount 0 the filter writes a ridge of zero, so
  a layer that skipped it and copied the previous ridge forward would hand the next layer a
  different signal.
- **Grade at Amount 0** states its own Filter contract in the shader and was still paying a
  full-resolution pass and a full-resolution copy to reproduce its input.
- **Any mask child at Weight 0.** Every mask shader ends on `saturate(lerp(Previous, Result,
  Weight))` over an already-saturated input, so the output is the input bit for bit. Skipping is
  only exact from the second mask child onward: the first establishes the chain with `Initialize`,
  where Previous is zero rather than what the layer already had. For craquelure both halves have
  to be idle — relief reads the same network through its own weights.
- **Engine default textures** were resolved through `LoadObject` on every composite, on the game
  thread, for two objects that never change.

## RDG validation failures the tests found

Both of these were live on every composite and invisible in normal use: RDG's checks are handled
ensures, which fire once per session into the log.

- `MaskTargets` were never cleared. The first mask child on a layer binds the half it is not
  writing as `PreviousMask` and ignores the value, but RDG validates the binding rather than the
  use: *"Pass ... has a read dependency on Mixtormat.MaskB, but it was never written to."*
- `EffectTargets` had the same problem for the first surface effect on a layer.

The read itself was always harmless. Clearing both pairs up front makes the graph honest about it.

## The colour ID mask

A new mask child, `EMixtormatLayerChildType::ColorId`, that selects the regions of an ID map
carrying one of a set of chosen colours.

This is the one placement control that comes from outside the tool. A painted mask, a generated
one and a curvature mask all describe where something is in the abstract; an ID map describes
where something is *by name*, because whoever built the mesh already decided which polygons were
the handle and which were the panel.

Design notes worth keeping:

- **Point sampled, and the only point sampler in the compositor.** Every other map here is a
  continuous signal that wants filtering. An ID map is a set of labels, and the average of two
  labels is a third label that names nothing. Softness feathers the match instead of the sample.
- **Several colours per node, unioned.** Selecting a set — every bolt, the three parts sharing a
  material — is the common case, and one node per colour would be a stack whose blend modes all
  have to agree just to express an OR. The cap is `FMixtormatColorIdMask::MaxColors`, stated once
  and deferred to by both the shader class and the inspector.
- **Maximum, not sum, over the set.** Overlapping tolerances cannot push a partial match above one
  and produce a hard edge where two selections sit near each other in colour.
- **Import matters.** sRGB off, uncompressed. Both settings move the colours the map stores, and a
  selection is a comparison against a colour picked out of them; DXT invents intermediate values
  along every ID boundary.

Tolerance is a distance in RGB, so it is small by nature — the diagonal of the colour cube is
about 1.73. If widening it starts claiming neighbouring IDs, the map wants a cleaner import rather
than a wider tolerance.

## Two bugs fixed in passing

Both were in the branch that routes generated-family children, which craquelure already used and
the ID mask now joins:

- `SetGeneratedEnabled` rejected anything that was not literally `Generated`, so the eye icon on a
  craquelure row did nothing.
- `RemoveGeneratedFromLayer` carried the same guard, so a craquelure node could be added and never
  deleted. Its menu entry is now named after the row it is on.
