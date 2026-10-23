# Moss Rewrite — Done

## Shader: `Shaders/Private/MixtormatMoss.usf`
- Biological Eden expansion replaces Gray-Scott. Habitat-gated (cavity/AO/slope/moisture).
- Solver v2: `Chemical.x` is a diffusing moisture field (replenished by habitat supply in `ActivationHabitat.a`, depleted by biomass); front advances by biomass/maturity/moisture pressure, diagonal steps cost √2, foreign-colony contact is pinched by `Competition`. `GrowthState.w` = local moisture.
- No gravity: `GravityAngle`/`Droop` deleted everywhere. Mush body instead: isotropic squash + flattened crown (`Softness`), turgor swelling from solver moisture (`Plumpness`), wet rim meniscus, merged slumping lobes, limp shoots. Stage 0 `.a` is now convex Ridge, not gravity alignment.
- Fixed ClumpDensity attenuation bug, fixed ShootDensity resolution dependence.
- Microshoots now use three tapered leaf blades per tuft, with midrib relief and lifted tips. Density sets spacing, Length sets relief height, and Fiber Thickness sets bounded blade width. Softness makes shoots limp; Plumpness lifts them when wet.
- Projected pixel footprints broaden blade/midrib profiles with area compensation; unresolved tuft frequencies fade. This is heightfield relief, not overlapping leaf geometry. Final-field cache version advanced; compilation and visual validation remain user-run.

## Runtime: `Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h`
Removed: Feed, Kill, DiffusionA, DiffusionB, TimeStep, BiomassAccumulation, SeedHabitatWeight, SeedNoiseWeight, SeedMaskWeight, SeedThreshold, bNormalizeSeedWeights, bConstrainGrowthToHabitat, BoundaryThreshold, ColonyConnectivity, Weight, Adhesion, GravityInfluence.
Kept: HeightScale, bNormalizeHeight, Preview, GrowthIterations, SimulationResolution, SeedMask/Texture/Tiling/Invert, CavityAttraction, OcclusionInfluence, Moisture, SurfaceAnglePreference, ExposureTolerance, ColonyDensity, GrowthSpread, Coverage, Seed, CushionSize/Height, MatureHeight, ClumpDensity, ShootDensity/Length, FiberThickness. Added: Competition (Growth), Softness + Plumpness (Body). Removed: GravityAngle, Droop.

## Shaders C++: `MixtormatGpuGeneratorPasses.cpp` + `MixtormatGeneratorGather.cpp`
- Removed SHADER_PARAMETER and P-> assignments for deleted uniforms.
- Removed sanitization for deleted fields. Updated SimulationHasher exclusion list.

## Inspector: `MixtormatInspectorGenerators.cpp`
- Removed SIMULATION Feed/Kill/Diffusion/TimeStep rows.
- Removed SEEDING weight mixer (Habitat/Noise/Mask weights + Normalize + Threshold).
- Removed HABITAT Constrain/Boundary rows.
- Removed GROWTH Connectivity + BiomassAccumulation.
- PHYSICS card replaced by BODY (MUSH): Softness + Plumpness. HABITAT loses Gravity Angle; GROWTH gains Competition.
- Growth Spread hint updated to habitat-gated description.

## Remaining: update `AgentDocs/GENERATORS.md` Moss section if needed.
