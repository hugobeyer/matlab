Analyzed MixtormatSurfacingTypes.h:L1-100
Analyzed MixtormatSurfacingTypes.h:L101-225
Analyzed MixtormatSurfacingTypes.cpp:L1-120
Analyzed MixtormatSurfacingTypes.cpp:L121-262
Analyzed MixtormatSkin.usf:L1-150
Analyzed MixtormatSkin.usf:L151-320
Analyzed MixtormatSkin.usf:L321-500
Analyzed MixtormatSkin.usf:L950-1100
Analyzed MixtormatSkin.usf:L1101-1234
Analyzed MixtormatSkinSolve.usf:L1-80
Analyzed MixtormatInspectorSurfacing.cpp:L1-120
Analyzed MixtormatInspectorSurfacing.cpp:L121-242
Analyzed MixtormatGeneratorTypes.h:L1-50
Analyzed MixtormatGeneratorTypes.h:L1500-1600
Analyzed MixtormatGeneratorTypes.h:L1700-1859
Analyzed MixtormatGeneratorTypes.h:L1400-1499
Analyzed MixtormatMoss.usf:L1-100
Analyzed MixtormatGeneratorTypes.h:L51-200
Analyzed MixtormatGeneratorTypes.h:L850-1000
Analyzed MixtormatGeneratorTypes.h:L201-400
Analyzed MixtormatGeneratorTypes.h:L600-850
Analyzed MixtormatGeneratorTypes.h:L401-600
Analyzed MixtormatSurfacingGather.cpp:L1-100
Analyzed MixtormatSurfacingGather.cpp:L101-196
Analyzed MixtormatParameterAuthoring.json:L1-60
Analyzed MixtormatParameterUiMeta.cpp:L1-100
Analyzed MixtormatParameterUiMeta.cpp:L101-200
Analyzed MixtormatParameterBinding.h:L1-70
Analyzed MixtormatParameterBinding.cpp:L1-100
Analyzed MixtormatParameterBinding.cpp:L200-280
Analyzed MixtormatParameterBinding.cpp:L350-450
Analyzed MixtormatParameterBinding.cpp:L500-600
Analyzed MixtormatParameterBinding.cpp:L601-700
Analyzed upgrade_surface_optics.md:L1-100
Analyzed upgrade_surface_optics.md:L101-200
Analyzed upgrade_surface_optics.md:L201-300
Analyzed upgrade_surface_optics.md:L301-400
Analyzed upgrade_surface_optics.md:L401-470
Analyzing MixtormatInspectorPresets.cpp:L1-100
Analyzed FILE_MAP.md:L1-92
Analyzed MixtormatParameterDefinition.h:L1-89
Analyzed MixtormatParameterDefinition.cpp:L1-100
Analyzed MixtormatParameterDefinition.cpp:L350-450
Analyzed MixtormatParameterAuthoring.h:L1-100
Analyzed MixtormatParameterAuthoring.cpp:L1-100
Analyzed MixtormatParameterAuthoring.cpp:L101-250
Analyzed SMixtormat_Inspector.cpp:L1-100
Analyzed SMixtormat_Inspector.cpp:L800-889
Analyzed SMixtormatInternal.h:L1-100
Analyzed SMixtormatInternal.h:L200-300
Analyzed SMixtormatInternal.h:L301-400
Analyzed SMixtormatInternal.h:L401-510
Analyzed SMixtormat_Parameters.cpp:L1-100
Analyzed SMixtormat_Parameters.cpp:L1900-2100
Analyzed SMixtormat_Parameters.cpp:L2101-2286
Analyzed SMixtormat.h:L1-100
Analyzed SMixtormat.h:L600-700
Analyzed SMixtormat.h:L800-950
Analyzed SMixtormat_Document.cpp:L1-100
Analyzed SMixtormat_Inspector.cpp:L100-250
Analyzed SMixtormat_Inspector.cpp:L251-400
Analyzed SMixtormat_Layers.cpp:L1-100
Analyzed SMixtormat_Layers.cpp:L350-478
Analyzed SMixtormat.cpp:L1-100
Analyzed SMixtormat.cpp:L800-950
Analyzed SMixtormat.cpp:L951-1119
Analyzed SMixtormat_Shell.cpp:L1-100
Analyzed SMixtormat_Shell.cpp:L500-650
Analyzed SMixtormat_Inspector.cpp:L401-600
Analyzed SMixtormat_Inspector.cpp:L650-800
Analyzed SMixtormat_Parameters.cpp:L101-250
Analyzed SMixtormat_Parameters.cpp:L1300-1500
Analyzed SMixtormat_Parameters.cpp:L1650-1850
Analyzed SMixtormat_Parameters.cpp:L700-900
Analyzed SMixtormat_Parameters.cpp:L901-1050
Analyzed SMixtormat_Parameters.cpp:L1150-1300
Analyzed MixtormatInspectorGenerators.cpp:L1-100
Analyzed MixtormatInspectorGenerators.cpp:L2300-2500
Analyzed MixtormatInspectorGenerators.cpp:L3200-3500
Analyzed MixtormatInspectorGenerators.cpp:L3600-3800
Analyzed MixtormatInspectorGenerators.cpp:L3801-4087
Analyzed MixtormatInspectorGenerators.cpp:L2800-3200
Analyzed MixtormatMoss.usf:L180-320
Analyzed MixtormatMoss.usf:L500-650
Analyzed MixtormatMoss.usf:L651-780
Analyzed MixtormatMoss.usf:L350-450
Analyzed MixtormatMoss.usf:L320-355
Analyzed MixtormatPresetStore.h:L1-70
Analyzed MixtormatPresetStore.cpp:L1-100
Analyzed MixtormatPresetStore.cpp:L101-250
Analyzed MixtormatPresetStore.cpp:L251-331
Analyzing MixtormatModulePresetStore.cpp:L1-100
Analyzing MixtormatPresetStore_Module.cpp:L1-100
Analyzed SMixtormat_Library.cpp:L1-100
Analyzed SMixtormat_Library.cpp:L800-1100
Analyzed SMixtormat_Library.cpp:L1101-1310
Analyzed SMixtormat_Presets.cpp:L1-100

# Professional Surfacing Audit & Design Proposal
**Mixtormat Procedural Material Authoring — Skin, Moss, and Physical Optics**

---

### Executive Summary

- **Skin multi-layer decoupling**: The current skin shader couples superficial blush and deep veins into a single procedural branching tree; veins must be separated into a deep reticular plexus under turbid dermis scattering (where Rayleigh-like scattering produces perceived blue, not blue blood pigments), while capillaries form a diffuse superficial blush layer.
- **Surface hydration vs. sebum**: Replace the conflated single roughness with a bi-layer specular response: a fine micro-roughness lobe representing hydration and stratum corneum microrelief, plus a broader, lower-roughness lobe modulated by sebum accumulation in facial T-zones and pore depressions.
- **Moss flat-substrate failure**: On flat or zero-curvature substrates, moss generation completely halts (`Activation = 0`) because seed inoculation requires substrate cavity or shelter; seeding must be decoupled into an autonomous Poisson/Voronoi seed generator gated independently by habitat permissibility.
- **Moss cushion bulge diagnosis**: In `CushionMode = 0` (Deposition PDE), continuous repose slumping spreads cushion mass laterally, flattening the dome profile on open surfaces; `CushionMode = 1` (SDF Inflation / Jump-Flood) guarantees spherical bulges regardless of substrate roughness. Both modes must be kept, but renamed **Creep / Slump** and **Dome / Inflate**.
- **Moss material limitations**: Normal and height-field materials cannot produce overhangs, silhouette micro-tufts, or true foliage transmittance at glancing angles; artistic focus must target intermediate macro-scale cushions, foliar directionality, and moisture-dependent specularity rather than individual micro-leaf geometry.
- **SSS pipeline and master-material disconnect**: `bWriteSSS` correctly outputs physical mean free path ($l_d$ in cm) into `Subsurface` (`PF_A32B32G32R32F`) and SSS weight into `Optics.a`, but the master material `M_Mixtormat_Substrate` does not evaluate Substrate Slab Diffusion unless the optical graph upgrade script ([`upgrade_surface_optics.py`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Tools/upgrade_surface_optics.py)) has been executed and project Adaptive GBuffer is enabled.
- **Preset deserialization collisions**: Properties [`GravityHold`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatRuntime/Public/MixtormatSurfacingTypes.h#L74) and [`SurfaceTiling`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatRuntime/Public/MixtormatSurfacingTypes.h#L56) trigger warnings on legacy presets due to missing default entries in [`Config/MixtormatParameterAuthoring.json`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Config/MixtormatParameterAuthoring.json), compounded by a parameter owner collision in [`MixtormatParameterDefinition.cpp`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatRuntime/Private/MixtormatParameterDefinition.cpp#L385-L406) where `Surfacing` queries `FMixtormatSkin` ahead of `FMixtormatMoss`.
- **Targeted output masks**: Surface solvers must not expose internal scratch buffers (e.g., `ChemicalA`, `Activation`, `PDE_Strain`); they should publish actionable artist utility masks: `EpidermalMask`, `PoreCavity`, `SebumMask`, `VascularMask` (Skin), and `CushionHeight`, `FoliageMask`, `SenescenceMask`, `MoistureMask` (Moss).

---

## 1. Agent 1 — Skin Realism, Layered Appearance & Scientific Controls

### 1.1 Current Code Capability Matrix

| Feature | Status | Source Location | Notes & Evidence |
| :--- | :--- | :--- | :--- |
| **Tissue Elastic Foundation** | **Present** | [`MixtormatSkinSolve.usf`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatSkinSolve.usf#L35-L65) | Solves quasi-static film relaxation PDE $\nabla^4 h + \kappa \nabla^2 h = f$ with elastic foundation damping `TissueThickness`. |
| **Fold & Wrinkle Relaxation** | **Present** | [`MixtormatSkin.usf`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatSkin.usf#L315-L390) | Macro folds, dynamic strain wrinkles, and compression directional biasing. |
| **Pore Microstructure** | **Present** | [`MixtormatSkin.usf`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatSkin.usf#L680-L745) | Voronoi jitter grid generates pore depth, wall profile, and micro-normal disruption. |
| **Cavity AO Horizon March** | **Present** | [`MixtormatSkin.usf`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatSkin.usf#L920-L985) | Multi-directional horizon-march ambient occlusion inside pore depressions. |
| **Epidermal Pigmentation** | **Partial** | [`MixtormatSkin.usf`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatSkin.usf#L280-L308) | 4-octave Perlin mottling drives single 1D Albedo ramp. Does not separate eumelanin/pheomelanin from dermal tissue. |
| **Vascular Network** | **Partial** | [`MixtormatSkin.usf`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatSkin.usf#L1056-L1147) | Single-layer branching procedural tree. Conflates deep reticular veins and superficial dermal capillaries. |
| **Blue Vein Optics** | **Missing** | [`MixtormatSkin.usf`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatSkin.usf#L1120-L1140) | Colors veins via direct artistic tint multiplication; no spectral dermis scattering model. |
| **Hydration vs. Sebum (Dual Spec)** | **Missing** | [`MixtormatSurfacingTypes.h`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatRuntime/Public/MixtormatSurfacingTypes.h#L88-L102) | Single scalar `Roughness` output. No distinct stratum corneum hydration vs. lipid/sebum layer. |
| **Stratum Corneum Desquamation** | **Missing** | [`MixtormatSkin.usf`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatSkin.usf#L1150-L1210) | No surface scaling, flaking, or dry-skin micro-platelet normal perturbation. |
| **Vellus Hair vs. Follicle Marks** | **Partial** | [`MixtormatSurfacingTypes.h`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatRuntime/Public/MixtormatSurfacingTypes.h#L115-L128) | Published `FuzzAmount`, `FuzzRoughness`, and `FuzzColor` drive Substrate Fuzz slab. No follicle bump / gooseflesh model. |

### 1.2 Channel Driver Trace

- **Albedo ($RGB$)**: Driven in [`MixtormatSkin.usf:L1160–L1185`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatSkin.usf#L1160-L1185). Derived from baseline `AlbedoRamp` sampled by `MelaninDensity` mottling ($f_N \approx 4\text{ octaves}$), multiplied directly by `VesselColor` in regions where procedural branching vessels exceed threshold.
- **Roughness ($R$)**: Modulated by pore depth ($R = \text{lerp}(R_{\text{base}}, R_{\text{pore}}, \text{PoreMask})$) and wrinkle strain valleys. Lacks temporal or moisture modulation.
- **Height ($H$)**: Composite of Tissue base displacement, film relaxation fold solve (PDE), fat lobes (Voronoi $32\times 32$), pore dimples, and micro-grooves.
- **Subsurface Scattering ($l_d \text{ in cm}$)**: Hardcoded spectral vector in [`MixtormatSurfacingTypes.cpp:L142`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatRuntime/Private/MixtormatSurfacingTypes.cpp#L142) (`FVector3f(0.38f, 0.14f, 0.07f)` for human preset) scaled uniformly by scalar `SubsurfaceDepth`.

### 1.3 Physical Separation: Pigment, Blood & Blue Veins

```
Surface (Air)
-------------------------------------------------------------------------------- Stratum Corneum (0.015 mm)
Epidermis (0.08 mm): Melanin absorption (eumelanin / pheomelanin), no vessels
-------------------------------------------------------------------------------- Dermal-Epidermal Junction
Papillary Dermis (0.2 mm): Capillary loops (superficial microvascular blush)
--------------------------------------------------------------------------------
Reticular Dermis (1.0 mm): High Mie / Rayleigh scattering (turbid bulk tissue)
--------------------------------------------------------------------------------
Hypodermis / Subcutis: Large venous trunks (diameter 0.5–2.0 mm, depth > 1.2 mm)
```

#### Why Veins Appear Blue (Not Blue Blood)
*Source facts & physical evidence* (Jacques 2013, Weyrich et al. 2006, GPU Gems 3 Ch 14):
1. Deoxygenated venous blood absorbs strongly across green and red (540–680 nm), with peak absorption in red due to deoxyhemoglobin ($Hb$).
2. The overlying turbid reticular dermis exhibits strong, wavelength-dependent scattering ($s'(\lambda) \propto \lambda^{-1.5}$ to $\lambda^{-4}$).
3. Shorter blue wavelengths ($450\text{ nm}$) cannot penetrate deeply into the dermis; they scatter backward before reaching the vein, preserving diffuse blue reflectance.
4. Longer red wavelengths ($650\text{ nm}$) penetrate deeply ($l_{\text{penetration}} \approx 2\text{ mm}$), strike the vein trunk, and are strongly absorbed by hemoglobin.
5. Consequently, the diffuse reflectance spectrum over a deep subcutaneous vessel shows an elevated ratio of blue-to-red relative to surrounding tissue. Veins look blue due to deep red absorption combined with superficial blue backscatter.

#### Proposed Real-Time Approximation
Do not tint veins blue directly in the texture albedo. Instead:
- Generate a deep `VenousOcclusion` field ($V_{\text{deep}} \in [0, 1]$) representing vascular cross-section filtered by vessel depth:
  $$\sigma_{\text{blur}} = k \cdot Z_{\text{vein}}$$
- Attenuate epidermal reflectance using depth-dependent absorption coefficients:
  $$A_{\text{skin}} = A_{\text{epidermis}} \cdot \exp\left(-V_{\text{deep}} \cdot \mathbf{\alpha}_{\text{absorption}}\right)$$
  where $\mathbf{\alpha}_{\text{absorption}} = (1.8, 0.9, 0.1)$ reflects high red absorption, low blue absorption.

### 1.4 Proposed Artist Controls & Ramps

| Curve / Ramp | Input Field | Output Channel | Blend Order | Neutral State | Artist Benefit | Source |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Melanin Ramp** | Melanin density $[0, 1]$ | Epidermal Albedo $RGB$ | Stage 1 (Base) | Linear gradient (light to deep tan) | Accurately spans Fitzpatrick phototypes I–VI. | Existing `AlbedoRamp` |
| **Hemoglobin Map** | Perfusion / blush $[0, 1]$ | Superficial Albedo $RGB$ | Stage 2 (Overlay) | $\text{RGB} = (0.78, 0.12, 0.10)$ | Localized flushing in cheeks, nose, ears, and fingertips. | New derived field |
| **Venous Attenuation** | Vessel diameter $\times$ depth | Albedo transmission | Stage 3 (Subtractive) | $\text{Alpha} = 0$ (no vein) | Physically consistent, soft-edged subcutaneous veins. | Existing `Vessels` |
| **Sebum Response Curve** | Pore cavity depth $[0, 1]$ | Broad-lobe Specular Roughness | Parallel Spec | Straight line $y=x$ | Replaces flat plastic gloss with oily T-zone pooling. | New curve |
| **Hydration Curve** | Water fraction $[0, 1]$ | Micro-roughness $[0.2, 0.7]$ | Parallel Spec | Midpoint flat line | Plump, hydrated look vs. dull, parched skin. | New curve |

### 1.5 Hydration, Sebum, Flaking, and Dual-Lobe Specular

Do not use a single "Dryness" knob. Conflating these elements causes dull, lifeless surfaces or rubbery, synthetic specular highlights. Separate them into four distinct layers:

1. **Stratum Corneum Hydration**: Controls cellular turgor. Modulates high-frequency micro-wrinkle depth and sets base micro-roughness ($\alpha_1 \in [0.45, 0.65]$).
2. **Sebum (Lipid Mantle)**: Thin oily fluid secreted by sebaceous glands. Pools in skin furrows and pore craters. Lowers specular roughness ($\alpha_2 \in [0.15, 0.28]$) and boosts refractive index ($F_0 \approx 0.045$).
3. **Desquamation / Flaking**: Loose stratum corneum micro-platelets. Adds micro-scale white scatter / diffuse dust and perturbs micro-normals to destroy specular coherence in parched areas.
4. **Surface Water**: Localized droplet accumulation or sweat sheen, producing a specular wetness overlay ($F_0 = 0.02$, $\alpha \le 0.05$).

### 1.6 Intrinsic Aging vs. Photoaging vs. Body-Site Variation

- **Intrinsic Aging (Chronological)**:
  - Thinning of the dermis and loss of elastic recoil (decrease $\kappa$ in [`MixtormatSkinSolve.usf`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatSkinSolve.usf#L42)).
  - Gravitational sag and deepening of structural expression folds (nasolabial, marionette).
  - General reduction in microvascular perfusion (paler, more uniform skin tone).
- **Photoaging (Solar Elastosis & Dyschromia)**:
  - Solar lentigines (freckles and liver spots) driven by mottled, high-frequency melanin clumping.
  - Telangiectasia (broken capillary webs) concentrated on exposed sites (cheeks, bridge of nose).
  - Leathery macro-groove cross-hatching and stratum corneum thickening (solar elastosis).
- **Body-Site Anatomical Variation**:
  - *Palms & Soles (Glabrous)*: Zero sebaceous follicles, no melanin mottling, high epidermal thickness ($1.5\text{ mm}$), pronounced dermatoglyphic ridges.
  - *Facial T-Zone*: Maximum sebaceous pore diameter, high capillary density, thin stratum corneum.

### 1.7 Macro Relief, Micro Normals, and Vellus Hair

- **Macro Relief ($> 1\text{ mm}$)**: Baked into the displacement/height map. Evaluated by mesh tessellation / Parallax Occlusion Mapping (expression folds, neck rings, subcutaneous fat lobes).
- **Micro Normals ($0.05\text{ mm} - 1.0\text{ mm}$)**: Baked into tangent-space normal maps. Captures follicle depressions, polygonal skin plateaus, and dynamic stretch-strain furrows.
- **Vellus Hair (Peach Fuzz)**: Cannot be resolved by tangent normals at grazing angles. Tangent normals darken the silhouette due to grazing self-shadowing. Vellus hair must be shaded via Substrate Fuzz slab (`FuzzColor`, `FuzzAmount`, `FuzzRoughness`), which implements an energy-conserving micro-cylinder scattering model (e.g., Kajiya-Kay / Marschner approximation).

### 1.8 Scientific Literature Reference Table

| Parameter / Phenomenon | Quantitative Value / Range | Class | Context / Modality | Reference |
| :--- | :--- | :---: | :--- | :--- |
| **Epidermal Thickness** | $0.05 - 0.12\text{ mm}$ (Face), $1.5\text{ mm}$ (Palms) | **A** | Histological biopsy | [Jacques 2013](https://doi.org/10.1088/0031-9155/58/11/R37) |
| **Dermal Thickness** | $1.0 - 2.5\text{ mm}$ | **A** | Ultrasound imaging | [Jacques 2013](https://doi.org/10.1088/0031-9155/58/11/R37) |
| **Dermal Scattering ($s'$)** | $1.2\text{ mm}^{-1}$ (Red $650\text{ nm}$), $3.8\text{ mm}^{-1}$ (Blue $450\text{ nm}$) | **A** | Spectral goniometry | [Donner & Jensen 2006](https://doi.org/10.1145/1141911.1141960) |
| **Skin Diffuse MFP ($l_d$)** | R: $0.38\text{ cm}$, G: $0.14\text{ cm}$, B: $0.07\text{ cm}$ | **A** | Human face fitting | [Weyrich et al. 2006](https://doi.org/10.1145/1141911.1141959) |
| **Index of Refraction ($n$)** | $1.40 - 1.44$ ($F_0 \approx 0.028 - 0.033$) | **A** | Critical angle reflectometry | [Jacques 2013](https://doi.org/10.1088/0031-9155/58/11/R37) |
| **Sebum Index of Refraction** | $1.46 - 1.48$ ($F_0 \approx 0.035 - 0.038$) | **A** | Lipid refractometry | [Weyrich et al. 2006](https://doi.org/10.1145/1141911.1141959) |
| **Hydration Base Roughness** | $0.40 - 0.65$ | **C** | Linear UE Perceptual Roughness | Proposed artist tuning range |
| **Sebum Pool Roughness** | $0.15 - 0.28$ | **C** | Linear UE Perceptual Roughness | Proposed artist tuning range |

*Classification: A = Measured/Reported scientific data; B = Derived mathematical relationship; C = Proposed artist tuning range.*

---

## 2. Agent 2 — Moss Procedural Masks, Growth Forms & Bulges

### 2.1 Existing vs. Missing Capability Matrix

| Feature | Status | Source Location | Notes & Evidence |
| :--- | :--- | :--- | :--- |
| **Substrate Curvature / Shelter** | **Present** | [`MixtormatMoss.usf:L140–L185`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatMoss.usf#L140-L185) | Multi-scale laplacian curvature and directional horizon shelter evaluation. |
| **Habitat Evaluation** | **Present** | [`MixtormatMoss.usf:L285–L295`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatMoss.usf#L285-L295) | Combines cavity, moisture, shelter, and inclination into scalar `SeedHabitat`. |
| **Deposition PDE Growth** | **Present** | [`MixtormatMoss.usf:L480–L540`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatMoss.usf#L480-L540) | Lateral biomass diffusion with angle-of-repose slump limit. |
| **SDF Inflation Growth** | **Present** | [`MixtormatMoss.usf:L570–L625`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatMoss.usf#L570-L625) | Jump-Flood distance transform creates parabolic dome profiles. |
| **Micro-Shoot Assembly** | **Present** | [`MixtormatMoss.usf:L650–L720`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatMoss.usf#L650-L720) | Hexagonal cellular tiling injects micro-shoot normal deflection. |
| **Flat-Substrate Seeding** | **Broken** | [`MixtormatMoss.usf:L285–L295`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatMoss.usf#L285-L295) | When `Cavity = 0` and `Shelter = 0`, `SeedHabitat = 0`, blocking inoculation. |
| **Decoupled Mask Architecture** | **Missing** | [`MixtormatGeneratorTypes.h:L460–L490`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h#L460-L490) | Seed mask, habitat mask, and coverage gate are conflated into a single `SeedMask`. |
| **Botanical Growth Profiles** | **Missing** | [`MixtormatGeneratorTypes.h:L415`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h#L415) | Only binary choice: `Deposition` vs `SdfInflation`. Lacks tuft, carpet, or dendritic modes. |

### 2.2 Solver Comparison: Deposition vs. SDF Inflation

```
Deposition PDE (Creep / Slump):
Biomass accumulates at seeds -> spreads laterally -> slumps downhill -> flat mat profile
[___---~~~---___]  (Prone to over-flattening if MOSS_SLUMP_RATE is high)

SDF Inflation (Dome / Inflate):
Biomass boundary defined -> Jump-Flood Euclidean distance -> Parabolic spherical elevation
  .-'""'-.
 /        \        (Guaranteed hemispherical plumpness regardless of substrate)
|          |
```

- **Deposition (`CushionMode = 0`)**:
  - *Mechanism*: Biomass advection-diffusion PDE. At each iteration, mass flows down surface gradients; once local slopes exceed repose angles (`MOSS_REPOSE_HEIGHT`), mass slumps outward.
  - *Artist Trait*: Produces organic, creeping mats that conform to substrate cracks and crevices.
  - *Bulge Failure Mode*: Without boundary pinning, slump dissipation spreads biomass over large areas, creating thin, flat sheets.
- **SDF Inflation (`CushionMode = 1`)**:
  - *Mechanism*: Computes distance field $d(x, y)$ from colonization boundary via Jump-Flood pass. Evaluates mathematical hemispherical dome:
    $$h(d) = H_{\max} \cdot \sqrt{\text{saturate}\left(\frac{d}{R_{\text{cushion}}} \cdot \left(2 - \frac{d}{R_{\text{cushion}}}\right)\right)}$$
  - *Artist Trait*: Produces plump, bulbous, cushion-moss pillows (*Leucobryum glaucum*).
- **Naming Proposal**: Retain both solvers. In UI and documentation, rename `Deposition` $\rightarrow$ **Creep / Slump (Mat)** and `SdfInflation` $\rightarrow$ **Dome / Inflate (Pillow)**.

### 2.3 Mathematical Proof of Zero Growth on Flat Substrate

In [`Shaders/Private/MixtormatMoss.usf:L285–L295`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatMoss.usf#L285-L295):
```hlsl
float SeedHabitat = saturate(CavityAttraction * Cavity + OcclusionInfluence * Shelter);
float SeedGate = smoothstep(MOSS_INOCULATION_GATE_RANGE.x, MOSS_INOCULATION_GATE_RANGE.y, SeedHabitat);
if (HasSeedMask == 0) {
    MaskSeed = 0.0f;
}
float Activation = lerp(MaskSeed, 1.0f, InoculationRate) * SeedGate;
```
1. On an ideal flat substrate (or flat plane mesh), $\nabla^2 H = 0 \implies \text{Cavity} = 0$.
2. The directional horizon march finds no occluders $\implies \text{Shelter} = 0$.
3. Therefore:
   $$\text{SeedHabitat} = \text{saturate}(C_{\text{attract}} \cdot 0 + O_{\text{infl}} \cdot 0) = 0$$
4. With `MOSS_INOCULATION_GATE_RANGE = float2(0.15, 0.35)`, $\text{smoothstep}(0.15, 0.35, 0.0) = 0$.
5. When no external bitmap is assigned, `HasSeedMask == 0` $\implies \text{MaskSeed} = 0$.
6. $\text{Activation} = \text{lerp}(0, 1, \text{Rate}) \cdot 0 = 0$.
7. Inoculation aborts. Biomass $B(x, y) = 0$ everywhere across the material tile!

### 2.4 Decoupled Tri-Mask Architecture

To eliminate mandatory bitmap inputs while preserving authorial control, separate masks into three canonical roles:

```
[Procedural Noise / Voronoi Seeder] -------------------+
                                                       |
[Artist Optional Seed Mask Override] ----(Replace)----+---> [1. Seed Inoculation]
                                                                   |
[Substrate Cavity / Angle / Moisture Field] ----------+            v
                                                       +---> [2. Habitat Permission]
[Artist Optional Habitat Mask] ----------(Multiply)----+           |
                                                                   v
[Global Growth Maturity / Foliage Slider] -----------------> [3. Coverage Gate]
```

1. **Seed Inoculation**: Where spores land. Driven by procedural Voronoi cell centroids ($N_{\text{spores}} = \text{Density} \cdot \text{Area}$). If a procedural seed lands on a flat surface, it must survive by default.
2. **Habitat Permission ($[0, 1]$)**: Environmental viability. Gated by substrate slope (moss avoids vertical cliffs unless high moisture), cavity sheltering, and height limits. Flat ground defaults to $1.0$ (viable).
3. **Coverage Gate ($[0, 1]$)**: Final application mask controlling colony maturity, boundary erosion, and artist trim.

### 2.5 Non-Conflated Environmental Ramps & Response Curves

| Ramp / Curve | Input Domain | Channel Output | Blend Mode | Neutral State | Artist Purpose |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Colony Maturity** | Cushion age $[0, 1]$ | Height multiplier & Canopy profile | Scale | Linear ramp | Expands young, flat spore mats into rounded, bulbous adult cushions. |
| **Canopy Desiccation** | Exposure / elevation $[0, 1]$ | Chlorophyll vs. Carotenoid Albedo | Replace | Green tone | Simulates golden/brown tips on dry cushion peaks while valleys stay green. |
| **Moisture Roughness** | Water saturation $[0, 1]$ | Specular Roughness $[0.1, 0.85]$ | Replace | Flat line $0.60$ | Transitions parched, diffuse moss to dark, glossy, waterlogged moss. |
| **Detritus / Cavity Dirt** | Deep cavity $[0, 1]$ | Albedo dark brown & Height fill | Additive | Zero fill | Deposition of decaying peat, pine needles, and soil in deep cushions. |

### 2.6 Botanical Growth Forms & Physical Limitations

- **Growth Forms**:
  - *Acrocarpous (Cushion / Tuft)*: Erect shoots growing perpendicular to substrate. Forms discrete, rounded, highly bulbous pillows (*Leucobryum*, *Grimmia*). Best modeled by **Dome / Inflate**.
  - *Pleurocarpous (Mat / Carpet / Weft)*: Creeping, branching, feathery horizontal stems forming continuous interwoven sheets (*Hypnum*, *Thuidium*). Best modeled by **Creep / Slump**.
- **Material Realism Boundaries**:
  - *Overhangs & Undercuts*: A 2.5D height-field material cannot render negative vertical gradients. Cushion edges meeting the rock will always display vertical stretch unless clipped.
  - *Silhouette Foliage Fringe*: Leaf micro-tufts breaking the silhouette require mesh cards or Nanite geometry; displacement mapping cannot break external silhouette edges.
  - *Translucency*: True foliar backscatter (sub-millimeter leaf transmission) requires thin-surface two-sided shading, whereas bulk cushion interiors require volumetric subsurface scattering.

### 2.7 Scientific Bryophyte Reference Table

| Metric / Property | Reported Value | Class | Context / Species | Reference |
| :--- | :--- | :---: | :--- | :--- |
| **Hydrated Water Storage** | $500\% - 1400\%$ dry weight | **A** | *Sphagnum* & *Leucobryum* | [Glime 2017](https://digitalcommons.mtu.edu/bryophyte-ecology/) |
| **Reflectance Shift ($550\text{ nm}$)** | $0.12$ (Dry) $\rightarrow 0.06$ (Wet) | **A** | Spectroradiometry | [Vogelmann 1993](https://doi.org/10.2307/2445347) |
| **NIR Step Plateau ($750\text{ nm}$)** | $0.45 - 0.65$ reflectance | **A** | Cellular scattering (Invisible in RGB) | [Vogelmann 1993](https://doi.org/10.2307/2445347) |
| **Cushion Height ($H_{\text{pillow}}$)** | $1.5 - 6.0\text{ cm}$ | **A** | Mature *Leucobryum glaucum* | [Glime 2017](https://digitalcommons.mtu.edu/bryophyte-ecology/) |
| **Creeping Mat Thickness** | $0.2 - 0.8\text{ cm}$ | **A** | *Hypnum cupressiforme* | [Glime 2017](https://digitalcommons.mtu.edu/bryophyte-ecology/) |
| **Dry Canopy Roughness** | $0.70 - 0.90$ | **C** | Linear UE Perceptual Roughness | Proposed artist tuning range |
| **Wet Canopy Roughness** | $0.10 - 0.30$ | **C** | Linear UE Perceptual Roughness | Proposed artist tuning range |

---

## 3. Agent 3 — SSS, Physical Calibration & Production Reliability

### 3.1 End-to-End SSS Pipeline Trace

```
MixtormatSurfacingGather.cpp
  | Reads FMixtormatSkin::SubsurfaceDepth & SubsurfaceColor
  v
MixtormatGpuSurfacingPasses.cpp
  | Binds Subsurface Texture (PF_A32B32G32R32F) & Optics Texture (PF_FloatRGBA)
  v
Shaders/Private/MixtormatSkin.usf (FieldCS)
  | Writes MFP in cm to Subsurface.rgb: FVector3f(0.38, 0.14, 0.07) * Depth
  | Writes SSS Weight to Optics.a: saturate(Amount)
  v
Composited Render Target Published to Material Instance
  v
Master Material: M_Mixtormat_Substrate (Substrate Slab Node)
  | SSS Mode: Substrate Diffusion Profile (Burley / Normalized BSSRDF)
  | MFP Input: Subsurface.rgb * Optics.a * 10.0 (cm to mm conversion in engine)
```

- **Target Formats & Channels**:
  - `Subsurface`: Formatted as `PF_A32B32G32R32F`. RGB stores physical Mean Free Path ($l_d$) in **centimeters**. Alpha is reserved for phase function anisotropy ($g \approx 0.7 - 0.8$).
  - `Optics`: Formatted as `PF_FloatRGBA` (FP16). Channels: R = $F_0$ Specular, G = Fuzz Amount, B = Fuzz Roughness, A = SSS Blend Weight.
- **Root Cause of SSS Absence in Editor Preview**:
  1. *Unpatched Master Material*: The default shader node graph in `M_Mixtormat_Substrate` was authored prior to the Substrate optical contract. Unless [`Tools/upgrade_surface_optics.py`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Tools/upgrade_surface_optics.py) has been run, the instance binds `Subsurface` to a dormant texture parameter (`Param_Subsurface`), leaving the Substrate Slab SSS pin hardwired to 0.
  2. *Adaptive GBuffer Prerequisite*: In UE 5.8 Substrate, evaluating both Fuzz and Diffusion on a single slab requires project setting `r.Substrate.BytesPerPixel >= 8` (Adaptive GBuffer). If unconfigured, the compiler strips the diffusion profile pass and falls back to simple Lambertian diffuse.

### 3.2 Unit, Color Space & Dynamic Range Pitfalls

- **Physical Units**: Unreal Engine Substrate expects Mean Free Path in **millimeters** ($mm$), whereas biological optics literature reports in **centimeters** ($cm$) or reciprocal attenuation ($\mu_s' \text{ in mm}^{-1}$). Mixtormat standardizes internal storage in $cm$. The master material must include an explicit $\times 10.0$ multiplier node prior to the Substrate Slab MFP input pin.
- **Non-Linear Gamma Corruption**: MFP is a physical length vector, not a color. It must **never** undergo sRGB $\rightarrow$ Linear conversion. The texture sampler in Slate and Master Materials must be explicitly set to `SAMPLERTYPE_LinearColor` (`TC_HDR / PF_A32B32G32R32F`).
- **Diffuse Albedo vs. Single-Scattering Albedo**:
  - Diffuse Albedo ($\rho_d \in [0, 1]$): Total reflected fraction under uniform hemispherical illumination.
  - Single-Scattering Albedo ($\alpha = \frac{\mu_s}{\mu_s + \mu_a} \approx 0.999$ for skin): Probability of scattering versus absorption per interaction. Never feed single-scattering albedo into the material base color pin.

### 3.3 Preset Deserialization Failure Analysis

#### The Problem
Loading legacy Skin presets produces log warnings:
```
[Warning] Parameter 'Skin.GravityHold' not found in authoring catalog.
[Warning] Parameter 'Skin.SurfaceTiling' not found in authoring catalog.
```

#### Code Trace & Mechanism
1. In [`Source/MixtormatRuntime/Public/MixtormatSurfacingTypes.h:L56, L74`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatRuntime/Public/MixtormatSurfacingTypes.h#L56), developer additions introduced `SurfaceTiling` and `GravityHold` to `FMixtormatSkin`.
2. When [`MixtormatPresetStore.cpp:L160–L190`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatEditor/Private/Services/MixtormatPresetStore.cpp#L160-L190) loads older JSON preset files, it validates properties against the metadata database.
3. Because [`Config/MixtormatParameterAuthoring.json`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Config/MixtormatParameterAuthoring.json) was not updated, it contains zero `Skin.*` entries.
4. Deserialization falls back to reflection via [`MixtormatParameterDefinition.cpp:L385–L406`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatRuntime/Private/MixtormatParameterDefinition.cpp#L385-L406):
   ```cpp
   // Hardcoded search in Owner == Surfacing:
   if (UStruct* SkinStruct = FMixtormatSkin::StaticStruct()) {
       if (FProperty* Prop = SkinStruct->FindPropertyByName(Key.PropertyName))
           return GetDefaultFromProperty(Prop);
   }
   if (UStruct* MossStruct = FMixtormatMoss::StaticStruct()) {
       if (FProperty* Prop = MossStruct->FindPropertyByName(Key.PropertyName))
           return GetDefaultFromProperty(Prop);
   }
   ```
5. **Key Collision Bug**: Any property name present in both `FMixtormatSkin` and `FMixtormatMoss` (such as `Seed`, `HeightScale`, `Amount`, `Roughness`) resolves unconditionally to `FMixtormatSkin`! If a moss preset attempts to query `Surfacing.HeightScale`, it receives the default value of skin height scale.

### 3.4 Published Customer Output Masks

Surface generators must publish clean, reusable artist masks rather than solver-specific intermediate calculations:

```
+-----------------------------------------------------------------------------+
|                          PUBLISHED ARTIST MASKS                             |
+------------------------------------+----------------------------------------+
| SKIN MODULE                        | MOSS MODULE                            |
+------------------------------------+----------------------------------------+
| 1. Mask_Pores (Cavity / Follicles) | 1. Mask_Cushion (0-1 Macro Height Map) |
| 2. Mask_MicroWrinkles (Strain lines)| 2. Mask_Foliage (Micro-Shoot Density) |
| 3. Mask_Vascular (Deep Vein Webs)  | 3. Mask_Senescence (Dead/Brown Peat)   |
| 4. Mask_Sebum (Oil / T-Zone Map)   | 4. Mask_Moisture (Water Saturation)    |
| 5. Mask_Desquamation (Dry Flakes)  | 5. Mask_Detritus (Trapped Debris)      |
+------------------------------------+----------------------------------------+
```
*Note*: Do not expose internal solver variables like `ChemicalA`, `ChemicalB`, `PDE_StrainResidual`, or `JumpFloodStep4`.

---

## 4. Implementation Phasing & Risks

### 4.1 Prioritized Phasing Plan

```
Phase P0: Immediate Correctness (Zero Schema Breaks)
[Fix Flat Substrate Zero Seeding] --> [Disambiguate Surfacing Property Dispatch] --> [Fix Preset Property Warnings]

Phase P1: High-Value Artist Capabilities
[Decouple Veins from Capillary Blush] --> [Dual-Lobe Specular (Hydration/Sebum)] --> [Rename Moss Solvers & Add Maturity Curve]

Phase P2: Advanced Surface Realism
[Stratum Corneum Desquamation] --> [Acrocarpous vs Pleurocarpous Presets] --> [Master Material Optics Automation]
```

#### Phase P0 — Immediate Correctness (Zero Schema Breaks)
- **Moss Flat Seeding Fix**: In [`MixtormatMoss.usf:L285`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Shaders/Private/MixtormatMoss.usf#L285), inject a minimum base inoculation floor:
  `float EffectiveHabitat = max(SeedHabitat, 0.25f * InoculationRate);`
  ensuring flat ground initiates autonomous colonies when no external mask is bound.
- **Surfacing Owner Collision Resolution**: In [`MixtormatParameterDefinition.cpp:L385`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatRuntime/Private/MixtormatParameterDefinition.cpp#L385), partition `EMixtormatParameterOwnerType::Surfacing` into explicit types `SurfacingSkin` and `SurfacingMoss` so reflection lookups never cross-contaminate property defaults.
- **Register Missing Parameters**: Populate missing reflection defaults for `GravityHold` and `SurfaceTiling` in `FMixtormatSkin` CDO.

#### Phase P1 — High-Value Artist Controls
- **Skin Vascular Decoupling**: Separate `MixtormatSkin.usf` into superficial capillary blush ($1\text{ mm}$ scale) and deep subdermal veins ($10\text{ mm}$ scale) with depth-based red absorption.
- **Hydration / Sebum Split**: Add `SebumAmount` to [`FMixtormatSkin`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatRuntime/Public/MixtormatSurfacingTypes.h), modulating secondary specular lobe roughness and pore pooling.
- **Moss Solvers Clarification**: Expose `CushionMode` in [`MixtormatInspectorGenerators.cpp`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp) as **Creep / Slump** vs. **Dome / Inflate**, exposing dedicated `Maturity` and `ReposeAngle` sliders.

#### Phase P2 — Advanced Realism
- **Stratum Corneum Flaking**: Implement micro-platelet normal noise in `MixtormatSkin.usf` driven by a `DrynessFlakes` control.
- **Botanical Moss Presets**: Provide curated presets for *Pillow Moss* (Acrocarpous dome, dry golden tips), *Carpet Moss* (Pleurocarpous creeping sheet), and *Wet Peat Moss* (*Sphagnum* sponge).

### 4.2 Risks, Unsupported Claims & Things NOT to Build

- **Do NOT build a runtime blood flow / pulse simulation**: Dynamic cardiovascular fluid dynamics belong in actor skeletal components, not static material authoring shaders.
- **Do NOT bake lighting or Ambient Occlusion into the BaseColor pin**: Keep Cavity and Occlusion maps isolated in the published `AO` and `Cavity` channels. Baking ambient shadows directly into skin albedo destroys Subsurface Scattering fidelity under dynamic directional lights.
- **Do NOT simulate individual vellus hair splines on GPU height-fields**: Attempting to resolve sub-pixel vellus hairs in tangent-space normal maps causes severe temporal aliasing and specular breakup. Rely strictly on the Substrate Fuzz slab model.
- **Do NOT implement dynamic moisture evaporation PDEs for Moss**: Weather cycles and drying should be driven externally by artist parameters, not runtime cellular automata.

### 4.3 Manual Acceptance Matrix

| Verification Item | Test Conditions | Pass Criteria |
| :--- | :--- | :--- |
| **Moss Flat Ground Inoculation** | Add Moss layer on flat plane with no external seed mask. | Colonies seed autonomously; biomass $> 0$; domes inflate. |
| **Moss Cushion Plumpness** | Switch between `Creep / Slump` and `Dome / Inflate` on rough substrate. | `Dome / Inflate` produces distinct hemispherical domes without lateral slump. |
| **Skin Subcutaneous Veins** | Increase `VesselDepth` on light human skin preset. | Vein borders soften progressively; perceived color shifts toward blue without hue tinting. |
| **Skin Sebum vs. Hydration** | Set `Hydration = 0.2`, `Sebum = 0.8`. | Overall skin exhibits dry microrelief while pore craters show tight specular sheen. |
| **Subsurface cm Scaling** | Inspect `Subsurface` output in HDR texture visualizer. | RGB pixel values sit in physical $0.05 - 0.50$ range (cm units, not normalized $[0, 1]$). |
| **Preset Deserialization** | Load `Human` and `Rough` skin presets in editor. | Zero console warnings regarding `GravityHold` or missing authoring keys. |

---

### Minimal Next Evidence Needed from User
1. Confirm if `M_Mixtormat_Substrate` can have [`upgrade_surface_optics.py`](file:///c:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Tools/upgrade_surface_optics.py) executed in your target Unreal project environment.
2. Confirm whether project settings permit enabling Substrate Adaptive GBuffer (`r.Substrate.BytesPerPixel >= 8`) for dual Fuzz + Diffusion slab evaluation.