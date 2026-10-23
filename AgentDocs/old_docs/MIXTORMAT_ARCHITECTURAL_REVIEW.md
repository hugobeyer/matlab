# Mixtormat — Independent Technical & Product Review

## 1. Executive Summary

Mixtormat is not a wrapper around Unreal material graphs or a simple texture-blending utility. It is a **complete, GPU-native procedural material and heightfield authoring engine** built directly on Unreal Engine's Render Dependency Graph (RDG) and a bespoke Slate design system.

Most procedural material tools fall into one of two camps:
- **Node graphs (Substance Designer):** Maximum procedural power (SDFs, flood fill/region IDs, slope/directional warps, distance transforms), but high cognitive load, slow iteration for surface mixing, and external round-tripping.
- **Layer mixers (Quixel Mixer / Substance Painter):** Fast, approachable layer stacks, but weak procedural geometry creation—limited to static height maps, basic noise, and simple threshold blends.

**Mixtormat bridges both worlds inside Unreal Engine.** It embeds a genuine signed-height, SDF, Region-ID, and vector-flow solver pipeline inside a hierarchical layer stack.

---

## 2. What Makes the Engine Underneath Truly Special

### A. First-Class Signed Fields, Region IDs, and Boundary Solvers
Unlike typical layer tools that only pass 0..1 grayscale masks, Mixtormat's GPU pipeline (`MixtormatShaders`) transports structured geometric fields between modules:
- **Signed Height Deltas (`PF_R32_FLOAT`):** Generators (`RockFormation`, `CliffStrata`, `Pebbles`, `Cracks`, `Noise`) and `Boolean` containers operate in unclipped signed relief space before final normalization and height-derived normal generation.
- **Discrete Region IDs (`PF_R32_UINT`):** Generators, Voronoi/Fracture Pattern IDs, and Surface IDs emit integer region labels that travel down the stack.
- **Jump-Flood / Eikonal Distance & Boundary Fields (`PF_G32R32F`):** Real distance-to-seam, Eikonal sweeps (`MixtormatCliffStrata.usf`), and crack propagation (`MixtormatCraquelureGrow.usf`) enable true bevels, chamfers, per-piece random tilts (`ReliefFromIds`), per-island UV variation (`UvFromIds`), and directional push/sculpt behaviors.
- **Jacobian-Filtered Structural Warps (`MixtormatGeneratorBundle.usf`):** Warping a generator bundle doesn't just distort a height texture—it transports heights, discrete Region IDs, anchors, and distance metrics together while applying 9-tap Gaussian footprint anti-aliasing from the local UV Jacobian.

### B. Multi-Pass Physical & Morphological Effects
The effects pipeline goes far beyond triplanar grunge projection:
- **Propagated Craquelure:** Iterative stress/toughness fracture growth with collision limits and jump-flood distance fields.
- **Stain & Runoff:** Transport and absorption solvers driven by underlying surface roughness, concavity, contact AO, and slope.
- **Peeling, Erosion, Breakup & Worn Edges:** Curvature- and region-aware surface degradation that modifies both coverage and relief normals coherently.

### C. Clean Three-Module Architecture & Caching
- **`MixtormatRuntime` ← `MixtormatShaders` ← `MixtormatEditor`** is strictly respected.
- **Prefix & Node Caching:** Expensive generator networks and propagated craquelure distance fields hash their clamped parameters so scrubbing downstream sliders (like warp, bevel, or color) runs at interactive frame rates without recomputing upstream solves.
- **Sources Shelf & Parameter Bindings:** Reusable non-compositing generator producers on the Shelf plus parameter linking/driving give technical artists node-graph reuse without turning the main stack into spaghetti.

### D. Custom Slate UI & Design Token System
- Unreal's default Editor UI (`IDetailsView`) is notoriously clunky for creative tools. Mixtormat replaces it with a cohesive, token-driven Slate architecture (`MixtormatThemeStore`, `SMixtormatSurfaceBox`, `SMixtormatRow`, custom scalar/color ramp editors, and PostFX viewport diagnostic overlays).

---

## 3. Honest Critique: Where the Friction Was (and How It's Evolving)

Because the GPU engine grew rapidly to support both **surface mixing** and **deep procedural generation**, the UX temporarily accumulated "two mental models" on top of one engine:
1. **Artificial walls between Layer Types (`Material`, `Fill`, `Generator`):**
   - Previously, adding a generator to an authored Material layer hijacked the layer into a height-only generator mode and wiped out the material's albedo, roughness, and normals.
   - **Now resolved:** Generators on a Material or Fill layer act as true height sculptors/manipulators—adding their signed relief onto the layer's height and blending their relief normals while preserving the material's PBR channels.
2. **Hidden Output Routing:**
   - The capability system (`GetChildCapabilities`) published rich outputs (`Height`, `Region IDs`, `Flow`, `Edge Distance`, `Crack Mask`), but discovering and copying them required digging into nested context menus blind.
   - **Now resolved:** Selecting any module surfaces a vertical **Outputs** strip in the top-left of the viewport where every output can be previewed live with one click and copied right beside it.
3. **Inconsistent Scoped Masking & ID Filtering:**
   - Previously, only basic masks could scope to a child while `Generated Mask`, `Color ID`, `Random From IDs`, and `Craquelure` were layer-only, and ID Filters floated at the layer root.
   - **Now resolved:** Every maskable child shares the same `Masks ▸` submenu and GPU scoped-mask evaluation, and ID Filters (`HSV`, `Ramp`, `UV`, `Relief`, `Boundary From IDs`) nest directly under their ID producers or group via `Ctrl+G`.

---

## 4. Industry Impact & What Mixtormat Can Become

### Immediate Impact for Unreal Artists
- **Zero Round-Trip Environment Art:** Environment artists can take 2–3 base Megascans/library surfaces, sculpt custom cliff strata, Voronoi rock formations, cobbles, or cracked plaster with Boolean containers, apply physical runoff/peeling/edge wear, and bake to static textures or Material Instances without ever leaving Unreal.
- **ADHD-Friendly, Visual-First Authoring:** By keeping relationships spatial in the stack (indented children for Booleans, ID Filters, Behaviors, and Masks + top-left live output pills), artists get 80% of Substance Designer's power with 20% of the mental overhead.

### Future Potential ("What Could Be")
1. **PBR / Substrate Channel Modules:**
   - Just as `Color Ramp` colors a generator's height, lightweight **Material / PBR Stamp** children scoped under a Generator or ID producer could assign full library surfaces or roughness/metallic shifts to specific Boolean cutters or Region IDs.
2. **Smart Presets / Smart Materials (`.uasset` Recipes):**
   - Because an entire Mixtormat recipe is a lightweight, resolution-independent `UMixtormatMaterial` UObject, shipping 20+ procedural smart recipes (Cliff Face, Medieval Cobble, Peeling Painted Wood, Fractured Concrete, Eroded Canyon) gives users infinite seed/parameter variations at any resolution up to 4K/8K.
3. **Runtime / PCG / Landscape Integration:**
   - Because `MixtormatRuntime` is cleanly separated, the same RDG compute pipeline can eventually drive dynamic runtime material variations, Landscape layer sculpting, or PCG-driven surface stamping.
