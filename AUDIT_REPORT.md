# MaterialLab / Mixtormat — Codebase Audit Report

## 1. Executive Summary & Market Valuation

- **Product Category**: In-Engine Procedural Material Compositor & Texture Synthesizer (Unreal Engine GPU/RDG-based alternative to Substance Painter/Mixer).
- **Estimated Commercial Market Value (FAB Store)**:
  - **Individual / Indie License**: **$149 – $249**
  - **Studio / Enterprise License**: **$499 – $799**
- **Core Strengths**: High-performance compute pipeline (RDG), advanced procedural algorithms (propagated craquelure simulation, eikonal peeling solver, directional erosion, cellular Voronoi), zero external DCC dependency.
- **Critical Blockers**: Monolithic god files, dual plugin identity (`MaterialLab` vs `Mixtormat`), hardcoded content paths, and lack of dynamic parameter driver architecture.

---

## 2. Priority Action List

| Priority | Category | Finding | Impact |
| :--- | :--- | :--- | :--- |
| **P0 (Blocker)** | **Hardcoding / FAB** | Dual identity naming mismatch (`MaterialLab` folder/uplugin vs `Mixtormat` modules/classes) | Fails FAB ingestion; packaging failure on rename |
| **P0 (Blocker)** | **Hardcoding** | Hardcoded `/MaterialLab/...` and `/Game/MaterialLab/...` asset/content paths | Breaks immediately if project/plugin folder is moved |
| **P1 (High)** | **Architecture** | Massive God Files (`MixtormatGpuCompositor.cpp` >6,200 LOC, `SMixtormat_*.cpp` >8,500 LOC) | Extreme maintenance friction, compile time bloat |
| **P1 (High)** | **Architecture** | Static parameter structs block upcoming Parameter Drivers / Expressions | Blocks Houdini-like reference parameters & AI drivers |
| **P2 (Medium)** | **Dead Code** | Orphaned shader files (`MixtormatFlow.usf`, `MixtormatFlow.ush`) | Shader compilation overhead & codebase confusion |
| **P2 (Medium)** | **Deprecation** | 24+ deprecated UPROPERTIES kept in runtime structs | Bloated asset serialization & schema technical debt |
| **P3 (Low)** | **Duplicates** | Copy-pasted shader code (`BlendReorientedNormals`, normal decoders, shaping props) | Code divergence risk across shaders |

---

## 3. Detailed Audit Findings

### A. Deprecation
- **Layer Effect Properties (`FMixtormatLayerEffect`)**:
  - 13 deprecated Stain properties (`StainColor`, `StainRoughness`, `StainPorosity`, `StainFlowAmount`, etc.).
  - 2 deprecated Erosion properties (`ErosionColor`, `ErosionColorAmount`).
  - 2 deprecated Chipping properties (`ChipColor`, `ChipColorAmount`).
- **Effect Asset Defaults (`UMixtormatEffect`)**:
  - 6 deprecated properties (`DefaultStainColor`, `DefaultStainRoughness`, `DefaultStainHeightInfluence`, etc.).
- **Legacy Enum Entries**:
  - `EMixtormatHeightSource::Automatic = 0` flagged explicitly as `(Legacy)`.

### B. Duplicates
- **Shader Code Duplication**:
  - `BlendReorientedNormals()` copy-pasted verbatim in `MixtormatComposite.usf` (line 165) and `MixtormatPeeling.usf` (line 51).
  - Normal decode/encode logic duplicated in `MixtormatComposite.usf` and `MixtormatPeeling.usf`.
  - Color conversions (`RGBToHSV`) implemented locally in `MixtormatComposite.usf` instead of shared in `MixtormatColorOps.ush`.
- **Property Declarations**:
  - `FMixtormatMaskShaping` was introduced to unify shaping (`Balance`, `Contrast`, `Offset`, `bInvert`), but duplicate manual property sets still exist in `FMixtormatGeneratedMask`, `FMixtormatColorIdMask`, and `UMixtormatMask`.
- **Slate UI Duplication**:
  - Repetitive manual slider row creation across `SMixtormat_Inspector.cpp` (>3,900 lines) instead of metadata-driven property generators.

### C. Dead Code
- **Unused Shaders**:
  - `Shaders/Private/MixtormatFlow.usf` and `MixtormatFlow.ush`: Not referenced by any C++ shader declaration or RDG pass in `MixtormatGpuCompositor.cpp`.
- **Trivial Stubs / Relics**:
  - `UMixtormatMaterial::CanAddLayer()`: Always returns hardcoded `true`.
  - `LiveThemeWindow` / Theme Switcher in `SMixtormat.h`: Documented relics left over after removal of older preset mockups.
  - Unused member variables in `SMixtormat` (e.g. `WorkingStatusText`).

### D. Bad Hardcoding
- **Plugin Identity Split**:
  - Root directory & descriptor: `MaterialLab` / `MaterialLab.uplugin`.
  - Module names: `MixtormatRuntime`, `MixtormatShaders`, `MixtormatEditor`.
  - Shader mapping: Fixed to `/Plugin/MaterialLab` in `MixtormatShadersModule.cpp`.
- **Hardcoded Content Paths**:
  - Master Material: `TEXT("/MaterialLab/Materials/M_MaterialLab_Substrate.M_MaterialLab_Substrate")` in `MixtormatBakeService.cpp` and `MixtormatSurfaceImporter.cpp`.
  - Asset Registry Scan Paths: Fixed strings `TEXT("/MaterialLab/Surfaces")`, `TEXT("/MaterialLab/Masks")`, `TEXT("/MaterialLab/Normals/")`.
  - Bake / Export Destination: `TEXT("/Game/MaterialLab/Materials")` in `SMixtormat_Document.cpp`.
  - Lighting HDRI Path: `TEXT("/MaterialLab/Lighting")` in `SMixtormat_Preview.cpp`.
  - Save Directory: Fixed `Saved/MaterialLab/LiveTheme.json`.

### E. Architecture & Structural Concerns
- **Monolithic God Objects**:
  - `MixtormatGpuCompositor.cpp` (6,244 LOC): Implements >25 compute shader passes, texture cache, ping-pong state, and readback in a single compilation unit.
  - `SMixtormat` (Widget): Centralizes preview viewport, asset browser, layer stack, undo/redo history, baking, and inspector in one gigantic class (>4,500 LOC).
  - `MixtormatMaterial.h` (1,989 LOC): Giant monolithic layer structs with ~100 direct member variables per layer.
- **No Detail Customization / Reflection**:
  - Every single property widget in Slate is manually wired with boilerplate lambdas instead of leveraging Unreal's `PropertyEditor` / `IDetailCustomization`.
- **Preparedness for Parameter Drivers (Houdini / ChatGPT reference style)**:
  - Current layer properties are raw primitive types (`float`, `int32`, `bool`).
  - **Requirement**: Needs a driver abstraction layer (e.g. `TVariant<float, FMixtormatParameterDriver>` or a Driver Component / Expression binding table) so parameters can subscribe to external expressions, curve drivers, or node links.

---

## 4. FAB Store Requirements & Release Checklist

1. **Brand & Naming Standardization**:
   - Unify entire plugin to **one** consistent naming scheme (`MaterialLab` or `Mixtormat`). Plugin name, folder name, uplugin name, module names, and shader directory mapping must match.
2. **Dynamic Asset Root Resolution**:
   - Replace all hardcoded `/MaterialLab/` strings with paths resolved via `IPluginManager::Get().FindPlugin(TEXT("..."))->GetMountedAssetPath()`.
3. **Multi-Engine Compatibility**:
   - Verify shader compilation and RDG builders on UE 5.3, 5.4, and 5.5 across DX12 (SM6), Vulkan, and Metal.
4. **Clean Asset Serialization & Versioning**:
   - Implement `Serialize()` with custom `FGuid` versioning to retire deprecated properties safely without breaking legacy test assets.
5. **Content Separation**:
   - Ensure all shipped default materials, textures, and HDRI assets reside in the plugin's `Content/` folder with proper LOD and compression settings.
6. **Documentation & Demo Project**:
   - Provide clean sample project demonstrating layered surfaces, procedural weathering effects, and the texture baking pipeline.
