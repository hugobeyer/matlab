# Mixtormat — Houdini COP OpenCL test kernels

These five independent Houdini COP kernels model the **proposed** unified Flow system; they are NOT Unreal plugin implementation or exact shader parity. All nodes are `#runover layer`, use WRAP for tileable input sampling, and keep optional `?size_ref`.

## Nodes and connections

| File | Required input layers | Optional input layers | Output layers |
|---|---|---|---|
| `01_mixtormat_flow.cl` | `height` (Flow node's own noise height) | `surface_height` (parent/preceding accumulated height), `incoming_flow` (preceding Flow), `mask`, `size_ref` | `generated`, `flow`, `magnitude` |
| `02_mixtormat_warp.cl` | `height`, `flow` | `color` (RGBA), `coverage`, `region_id` (integer), `mask`, `size_ref` | `height_out`, `color_out`, `coverage_out`, `region_id_out`, `uv` |
| `03_mixtormat_deform.cl` | `height`, `flow` | `mask`, `size_ref` | `height_out`, `uv` |
| `04_mixtormat_push.cl` | `height` (existing signed height), `push_height` (signed contribution) | `mask`, `size_ref` | `height_out`, `delta_out` |
| `05_mixtormat_sculpt.cl` | `height` (existing height), `flow` | `mask`, `size_ref` | `height_out`, `sculpt_mask`, `delta_out` |

Each source and destination name above is a **layer name**, not a wire/socket type. Create an OpenCL COP for each file, paste the contents, and bind named layers in Houdini. Required outputs are explicitly declared and produced even when optional inputs are not bound.

## Suggested quick test

1. Make two tileable scalar images: a procedural noise height (`height`) for **Flow**, and a rock or other height image (`surface_height`) for its Slope and Gravity. `surface_height` is optional; if omitted, Slope/Gravity fall back to `height`.
2. Set Flow `curl_weight=1`, all other weights `0`. Inspect `flow` and `magnitude`.
3. Set `curl_weight=0`, `force_weight=1`, `angle=270`, `gravity=0` for uniform downward Force. Raise `gravity` to steer that Force by the surface Height gradient. Test negative `force_weight` too.
4. Feed the Flow `flow` output into Warp, Deform, or Sculpt. **Warp** resamples all its bound fields (including region IDs with nearest-neighbor sampling). **Deform** resamples only Height. For Warp/Deform, `mode=0` is Standard (one displacement); `mode=1` is Traced (RK2), `strength` is signed, and `uv` is the *lifted*, destination-to-source coordinate.
5. **Push** ignores Flow and changes Height using `push_height * strength * mask`. Its `blend`: `0` Add, `1` Multiply (`base*(1+delta)`), `2` Min, `3` Max. `reverse=1` reverses sign.
6. **Sculpt** (proposed replacement name for Carve/Deposit) requires the Flow vectors for tracing. `depth>0` cuts downhill/downstream; `depth<0` raises upstream. `width`, `falloff`, `trace_length` and `trace_steps` shape the traced Height gather. No SDF and no Groove/Deposit selector.

## Semantics / caveats

- Flow contributes four weighted bases: **Height + Slope + Curl + Force**. Force owns `angle` and `gravity` (surface-following). Only a positive mask and bounded Mix are clamped; signed weights, Add, Gravity and Strength remain signed.
- `generated` is the new Flow vector *before* composition. `flow` is `mix(incoming_flow,generated,clamp(mix_amount*mask,0,1)) + generated*(add*mask)`.
- The curl is an OpenCL port of Mixtormat's periodic **analytic gradient noise derivatives**, not a finite-difference approximation. Integer periods keep all octaves tileable. It uses the current Mixtormat weight-sum normalization; the deferred Houdini energy-normalization option isn't included.
- Flow only writes vector layers; tracing is exclusively in Warp/Deform/Sculpt. Push does not read Flow. SDF is absent from every kernel.
- `mode=0` for Standard vs `mode=1` for Traced is an experimental COP control, not a source selector. Warp and Deform trace incoming vectors rather than running a private Flow generator.
- Warp's companion channels are an explicitly declared sample set, not a generic runtime-discovered collection. In Unreal the full generator bundle must be moved consistently. Normals derived from Height should be recalculated after warping.
- Every field connected to one COP node should have matching resolution. If the integer ID layer is unavailable in your COP setup, you can leave it unbound and omit its output from use.
- These OpenCL kernels have been statically reviewed against the planned math and prior Houdini binding patterns. **They have not been compiled or run inside Houdini.** Unreal-specific validity channels, field masks, driver bindings, and caching are not emulated.
