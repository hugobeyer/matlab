# Moss Rewrite — Done

## Shader: `Shaders/Private/MixtormatMoss.usf`
- Biological Eden expansion replaces Gray-Scott. Habitat-gated (cavity/AO/slope/moisture).
- Age-driven cushion bulge + downslope droop with lip pile-up.
- Fixed ClumpDensity attenuation bug, fixed ShootDensity resolution dependence.

## Runtime: `Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h`
Removed: Feed, Kill, DiffusionA, DiffusionB, TimeStep, BiomassAccumulation, SeedHabitatWeight, SeedNoiseWeight, SeedMaskWeight, SeedThreshold, bNormalizeSeedWeights, bConstrainGrowthToHabitat, BoundaryThreshold, ColonyConnectivity, Weight, Adhesion, GravityInfluence.
Kept: HeightScale, bNormalizeHeight, Preview, GrowthIterations, SimulationResolution, SeedMask/Texture/Tiling/Invert, CavityAttraction, OcclusionInfluence, Moisture, SurfaceAnglePreference, GravityAngle, ExposureTolerance, ColonyDensity, GrowthSpread, Coverage, Seed, CushionSize/Height, MatureHeight, ClumpDensity, ShootDensity/Length, FiberThickness, Droop.

## Shaders C++: `MixtormatGpuGeneratorPasses.cpp` + `MixtormatGeneratorGather.cpp`
- Removed SHADER_PARAMETER and P-> assignments for deleted uniforms.
- Removed sanitization for deleted fields. Updated SimulationHasher exclusion list.

## Inspector: `MixtormatInspectorGenerators.cpp`
- Removed SIMULATION Feed/Kill/Diffusion/TimeStep rows.
- Removed SEEDING weight mixer (Habitat/Noise/Mask weights + Normalize + Threshold).
- Removed HABITAT Constrain/Boundary rows.
- Removed GROWTH Connectivity + BiomassAccumulation.
- Removed PHYSICS Weight/Adhesion/GravityInfluence — kept only Droop.
- Growth Spread hint updated to habitat-gated description.

## Remaining: update `AgentDocs/GENERATORS.md` Moss section if needed.
