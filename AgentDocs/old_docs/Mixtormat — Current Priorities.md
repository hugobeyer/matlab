1. Generalize scoped/gated masks — any mask producer should work under Grade, effects, generators; not just texture masks.
2. Ramp module — shared scalar ramp in FMixtormatMaskShaping; LMB add/drag, RMB delete, Linear | Spline; GPU evaluator shared by all mask producers.
3. Finish mask pipeline cleanup — keep the flow strictly producer → normalize → levels → ramp → balance/contrast/offset/invert → blend.
4. Separate mask placement — shared tiling/offset/rotation/flip payload instead of repeating it across mask types.
5. Consolidate typed outputs — continue cleaning Mask / Region IDs / Flow / UV / future Height publication around one clear output contract.
6. Document/edit operations out of Slate — move copy, duplicate, remap, scope moves, identity handling into reusable services.
7. Preview/refresh orchestration — centralize invalidation and refresh rules.
8. Lattice generator — only after outputs/scoping are stable.
9. Freeze Up To Here — after dependency tracking/output references are settled.
10. Tests cleanup last — keep useful boundary tests now; later delete obsolete/dead/duplicate tests together with the dead code they protect.