// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatOutputReference.h"
#include "MixtormatEffect.h"
#include "MixtormatBehaviorTypes.generated.h"

// The Behavior authoring contract. There is exactly one system: a Behavior is a child of an
// earlier Generator in the same layer, addressed by ScopeOwnerChildId, and it rewrites that
// generator's own output.
//
// A Field is a typed producer. It is either owned locally by a Behavior (Kind on the socket,
// Origin = a semantic local snapshot) or explicitly referenced through Sources (Origin =
// PublishedOutput, an FMixtormatOutputReference that names a kind and an output name). Nothing
// converts between published types implicitly: a Flow socket consuming a Vector2 output is an
// invalid authoring state that Gather rejects, not a value it silently reinterprets.
//
// The old generator/structural modules are gone; their algorithms live here. Structural Warp is
// Warp, Height Push is Push, and Shape Deform / Generator Flow / Flow Carve / Gravity Flow are
// Warp or Carve. See AgentDocs/BEHAVIOR_V2.md.

// The published field types a Behavior socket may consume. The set is deliberately narrower than
// EMixtormatPublishedFieldKind: it is the kinds a Behavior has an algorithm for, not every kind
// the reference system can address.
UENUM(BlueprintType)
enum class EMixtormatBehaviorFieldKind : uint8
{
	None UMETA(DisplayName = "None"),
	// Direction bundle: (direction, ..., winding). The only kind a coordinate socket accepts.
	Flow UMETA(DisplayName = "Flow"),
	// Destination->source UV map (float2), identity when unwarped.
	UVMap UMETA(DisplayName = "UV Map"),
	// Two-component vector field. Not a coordinate contract.
	Vector2 UMETA(DisplayName = "Vector 2"),
	// Signed distance field, negative inside.
	SDF UMETA(DisplayName = "Signed Distance"),
	// Centred signed relief; owns the surface's own level.
	ScalarSigned UMETA(DisplayName = "Signed Scalar"),
	// 0..1 coverage or magnitude.
	Scalar01 UMETA(DisplayName = "Unsigned Scalar"),
	// Colour.
	Color UMETA(DisplayName = "Color")
};

// The four kinds of Behavior. Each is one operation on the owning generator's own output:
//
//   Warp   -- coordinate/bundle displacement. PreGeneration moves the sampling coordinates the
//             generator resolves against; PostGeneration remaps the completed bundle. Nothing
//             between the two, so the two stages never disagree about what the generator still
//             owes its consumers.
//   Push   -- adds signed height. Explicit source field, explicit strength, gated and drivable.
//   Carve  -- modifies signed relief from a distance and an influence. Positive Strength removes
//             height, negative Strength deposits it, so one kind covers carving and deposition.
//   Deform -- remaps the generator's signed height only, leaving its Region IDs, coverage and
//             named attributes at their authored generator-domain placement.
//
// Deform is the one kind that cannot be expressed through the other three and the reason is
// structural, not cosmetic: Warp remaps the whole bundle, Push and Carve add a scalar they do not
// resample, and only Deform resamples relief while deliberately not touching its companions.
UENUM(BlueprintType)
enum class EMixtormatBehaviorType : uint8
{
	Warp UMETA(DisplayName = "Warp"),
	Push UMETA(DisplayName = "Push"),
	Carve UMETA(DisplayName = "Carve / Deposit"),
	Deform UMETA(DisplayName = "Deform"),
	FlowField UMETA(DisplayName = "Flow Field")
};

// The stage is an explicit part of the behavior contract, not inferred from its UI parent.
// PreGeneration acts on the generator's sampling coordinates; PostGeneration acts on its
// native height/bundle before the shared height normalization and output scale.
//
// Only Warp has meaning at both stages: a coordinate displacement either happens before the
// generator resolves (PreGeneration) or it remaps what it already resolved (PostGeneration).
// Push, Carve and Deform all operate on produced relief, so they are PostGeneration only;
// Gather rejects them at PreGeneration rather than running them at an arbitrary point.
UENUM(BlueprintType)
enum class EMixtormatBehaviorStage : uint8
{
	PreGeneration UMETA(DisplayName = "Before Generation"),
	PostGeneration UMETA(DisplayName = "After Generation")
};

// How a field is folded into the operation's result.
//
// Operation is value 0 on purpose: it is the combine each operation documents
// (Push adds a signed delta, Carve subtracts a distance profile, Warp/Deform
// transport a coordinate map), so a Behavior saved before this enum existed
// resolves to Operation and produces exactly the result it used to.
//
// The remaining values are signed-scalar combines and are only accepted where a
// base actually exists to combine against -- Push. Warp/Deform own a coordinate
// contract rather than a value, and Carve's field is a distance rather than a
// contribution, so Gather rejects a non-Operation blend on both instead of
// quietly treating it as if it had been authored as Operation.
UENUM(BlueprintType)
enum class EMixtormatBehaviorFieldBlend : uint8
{
	Operation = 0 UMETA(DisplayName = "Operation Default"),
	Add UMETA(DisplayName = "Add"),
	Multiply UMETA(DisplayName = "Multiply"),
	Min UMETA(DisplayName = "Minimum"),
	Max UMETA(DisplayName = "Maximum")
};

UENUM(BlueprintType)
enum class EMixtormatBehaviorFieldOrigin : uint8
{
	None UMETA(DisplayName = "None"),
	OwnNativeHeight UMETA(DisplayName = "Own Native Height"),
	OwnBoundary UMETA(DisplayName = "Own Boundary"),
	PreviousRunningHeight UMETA(DisplayName = "Previous Running Height"),
	PublishedOutput UMETA(DisplayName = "Published Output")
};

// A field socket owns either a semantic local snapshot or an explicit typed reference.
// Published references are never silently reinterpreted (Vector2 != Flow != UVMap).
//
// Kind is the *socket's* contract. It is what this Behavior knows how to consume, and it is what
// decides whether an authored reference is accepted: a coordinate socket referencing a Vector2
// output is rejected rather than read as if it were a Flow. With no reference authored, Kind
// describes the local snapshot the Behavior derives itself.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatBehaviorFieldInput
{
	GENERATED_BODY()

	// The published type this socket consumes. Drives reference acceptance and, for a local
	// snapshot, which snapshot is derived.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Field")
	EMixtormatBehaviorFieldKind Kind = EMixtormatBehaviorFieldKind::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Field")
	EMixtormatBehaviorFieldOrigin Origin = EMixtormatBehaviorFieldOrigin::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Field",
		meta = (EditCondition = "Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput"))
	FMixtormatOutputReference Published;

	bool HasPublishedSource() const
	{
		return Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput && Published.HasSource();
	}

	// Per-field scalar weight, applied after the Behavior's own (drivable) Strength.
	// Keeping it on the socket rather than only on the Behavior is what lets one
	// Behavior weight a direction field and a height field independently.
	// A non-finite amplitude fails validation closed instead of defaulting to 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Field",
		meta = (UIMin = "-4.0", UIMax = "4.0"))
	float Amplitude = 1.0f;

	// Sign flip for the fields where a sign is meaningful: a Flow/UVMap direction
	// and a signed height or distance. Scalar01 coverage and Color have no sign,
	// so a reverse on them is rejected rather than silently ignored.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Field")
	bool bReversed = false;

	// Signed-scalar combine. Only Push has a base to combine against.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Field")
	EMixtormatBehaviorFieldBlend Blend = EMixtormatBehaviorFieldBlend::Operation;

	// Which kinds carry a sign that can be flipped.
	static bool KindSupportsReverse(const EMixtormatBehaviorFieldKind Kind)
	{
		return Kind == EMixtormatBehaviorFieldKind::Flow
			|| Kind == EMixtormatBehaviorFieldKind::UVMap
			|| Kind == EMixtormatBehaviorFieldKind::ScalarSigned
			|| Kind == EMixtormatBehaviorFieldKind::SDF;
	}

	// Which kinds carry a value that can be folded into a base rather than only
	// transported as a coordinate or read as a distance.
	static bool KindSupportsBlend(const EMixtormatBehaviorFieldKind Kind)
	{
		return Kind == EMixtormatBehaviorFieldKind::ScalarSigned;
	}
};

// The kind a socket actually carries, which is what the composition rules and
// the renderer must both judge. A local snapshot has a fixed semantic; a
// published reference takes its contract from the reference kind, because that
// is what will actually be bound. Authoring Kind alone is not enough to judge
// a local snapshot -- an Own Native Height direction is signed whether or not
// the author remembered to say so -- so every caller resolves through here
// rather than re-deriving the rule per site.
inline EMixtormatBehaviorFieldKind MixtormatBehaviorFieldEffectiveKind(
	const FMixtormatBehaviorFieldInput& Input)
{
	switch (Input.Origin)
	{
	case EMixtormatBehaviorFieldOrigin::OwnBoundary:
		return EMixtormatBehaviorFieldKind::SDF;
	case EMixtormatBehaviorFieldOrigin::OwnNativeHeight:
	case EMixtormatBehaviorFieldOrigin::PreviousRunningHeight:
		return EMixtormatBehaviorFieldKind::ScalarSigned;
	case EMixtormatBehaviorFieldOrigin::PublishedOutput:
		switch (Input.Published.Kind)
		{
		case EMixtormatPublishedFieldKind::Flow: return EMixtormatBehaviorFieldKind::Flow;
		case EMixtormatPublishedFieldKind::UVMap: return EMixtormatBehaviorFieldKind::UVMap;
		case EMixtormatPublishedFieldKind::Vector2: return EMixtormatBehaviorFieldKind::Vector2;
		case EMixtormatPublishedFieldKind::SDF: return EMixtormatBehaviorFieldKind::SDF;
		case EMixtormatPublishedFieldKind::Scalar01: return EMixtormatBehaviorFieldKind::Scalar01;
		case EMixtormatPublishedFieldKind::ScalarSigned: return EMixtormatBehaviorFieldKind::ScalarSigned;
		case EMixtormatPublishedFieldKind::Color: return EMixtormatBehaviorFieldKind::Color;
		default: return EMixtormatBehaviorFieldKind::None;
		}
	default:
		return EMixtormatBehaviorFieldKind::None;
	}
}

UENUM(BlueprintType)
enum class EMixtormatBehaviorFlowMode : uint8
{
	None = 0 UMETA(Hidden),
	Transport = 1 UMETA(DisplayName = "Flow"),
	Gravity = 3 UMETA(DisplayName = "Gravity")
};

// Which field of the owning generator seeds the flow direction. Behavior-owned: the old
// Effect-owned definition moved here with the rest of the flow architecture.
UENUM(BlueprintType)
enum class EMixtormatBehaviorFlowSource : uint8
{
	// Boundary normal from the generator's signed boundary distance (negative inside).
	SignedDistance = 0 UMETA(DisplayName = "Signed Distance"),
	// Downhill direction of the generator's own height.
	Height = 1 UMETA(DisplayName = "Height")
};

UENUM(BlueprintType)
enum class EMixtormatBehaviorFlowCarveMode : uint8
{
	// Distance-biased minimum along the trace: cuts grooves.
	Groove = 0 UMETA(DisplayName = "Groove"),
	// Distance-biased maximum along the trace: raises deposits.
	Deposit = 1 UMETA(DisplayName = "Deposit")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatBehaviorFlowSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow")
	EMixtormatBehaviorFlowMode Mode = EMixtormatBehaviorFlowMode::Transport;

	// A traced operation consumes its own solved field; a Flow Field child only publishes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow")
	bool bUseTracedFlow = false;

	// ---- Behavior-owned Flow Field solver parameters ---------------
	// One shared direction field, derived from the owning generator and extended by a
	// tile-aware jump-flood solve; see Docs/flow_generation_core.md. Distances are UV units.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow")
	EMixtormatBehaviorFlowSource FlowSource = EMixtormatBehaviorFlowSource::SignedDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float FlowAmount = 1.0f;

	// 0 follows the boundary normal (or downhill), 1 its perpendicular contour direction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float FlowTangent = 0.0f;

	// Constant rotation of the seeded direction, in degrees.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow", meta = (UIMin = "-180.0", UIMax = "180.0", Delta = "1.0"))
	float FlowAngle = 0.0f;

	// Gravity mode: bounded downhill steering of texture-space gravity by the owner's height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow|Gravity", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01"))
	float GravitySurfaceFollow = 1.0f;

	// Gravity mode, Signed Distance source: remove incoming motion near the owner's boundary.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow|Gravity", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float GravityDeflection = 1.0f;

	// Peak rotation from low-frequency periodic noise, in degrees.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow", meta = (UIMin = "-180.0", UIMax = "180.0", Delta = "1.0"))
	float FlowBend = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow", meta = (UIMin = "0", UIMax = "1024", Delta = "1"))
	int32 FlowSeed = 1;

	// Texel radius of the gradient kernel the seed directions are derived with.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow", meta = (UIMin = "1", UIMax = "16", Delta = "1"))
	int32 FlowRadius = 2;

	// Bartlett (tent) blur of the extended direction field: half-width in output texels. The jump flood hands
	// each pixel its nearest seed's direction, which is piecewise constant; this smooths it.
	// 0 keeps the raw field.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow", meta = (UIMin = "0.0", UIMax = "64.0", Delta = "0.5"))
	float FlowSmooth = 8.0f;

	// Propagation distance (UV) over which influence falls to zero.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.001"))
	float Reach = 0.1f;

	// Fraction of Reach spent fading out. 0 is a hard cut at Reach.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float Feather = 0.5f;

	// Signed offsets, in fractions of Reach, along and across the extended direction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float FlowOffsetAlong = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float FlowOffsetAcross = 0.0f;

	// Deform: signed boundary expansion (+) or erosion (-), UV. Signed Distance only.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow|Deform", meta = (UIMin = "-0.25", UIMax = "0.25", Delta = "0.001"))
	float ShapeOffset = 0.0f;

	// Deform: UV displacement along (+, bulge) or against (-, pinch) the direction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow|Deform", meta = (UIMin = "-0.25", UIMax = "0.25", Delta = "0.001"))
	float Bulge = 0.0f;

	// Traced Warp / Carve: total traced distance (UV) and its RK2 step count.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow|Trace", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.001"))
	float TraceLength = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow|Trace", meta = (UIMin = "1", UIMax = "64", Delta = "1"))
	int32 TraceSteps = 16;

	// Traced Warp: signed multiplier on the traced displacement.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow|Trace", meta = (UIMin = "-4.0", UIMax = "4.0", Delta = "0.01"))
	float WarpStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow|Carve")
	EMixtormatBehaviorFlowCarveMode CarveMode = EMixtormatBehaviorFlowCarveMode::Groove;

	// Carve: gain on the gathered height difference. 1 cuts (Groove) or raises (Deposit)
	// all the way to the strongest distance-weighted sample; nothing caps it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow|Carve", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01"))
	float Depth = 1.0f;

	// Carve: half-width (UV) of the groove across the flow.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow|Carve", meta = (UIMin = "0.0", UIMax = "0.25", Delta = "0.001"))
	float Width = 0.01f;

	// Carve: exponent on the along-trace distance falloff.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow|Carve", meta = (UIMin = "0.1", UIMax = "8.0", Delta = "0.01"))
	float Falloff = 1.0f;

};

// Behaviors are ordinary children owned through ScopeOwnerChildId by an earlier Generator.
// Field inputs are slots on the Behavior, not extra serialized layer children.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatBehavior
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	EMixtormatBehaviorType Type = EMixtormatBehaviorType::Warp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	EMixtormatBehaviorStage Stage = EMixtormatBehaviorStage::PostGeneration;

	// Layer composition: nothing between the two, so the generator's own consumers
	// still see whole-system results.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior", meta = (UIMin = "-4.0", UIMax = "4.0"))
	float Strength = 1.0f;

	// UV reach for a Warp or Deform driven by the owning generator's own native height
	// gradient. Independent of texture resolution; zero disables the local-gradient
	// displacement entirely, leaving only an explicit referenced direction field.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior", meta = (UIMin = "0.0", UIMax = "0.25"))
	float GradientReach = 0.02f;

	// Signed footprint width of a Carve, in UV-distance units. The profile is a
	// saturated ramp across the width, so this is the distance at which relief stops
	// changing at all rather than a softness parameter.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior", meta = (UIMin = "0.001", UIMax = "0.25"))
	float CarveWidth = 0.02f;

	// Flow production settings are authored on Behaviors, never on legacy Effect children.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Flow")
	FMixtormatBehaviorFlowSettings Flow;

	// Coordinate source. Only Warp and Deform read it, and only Flow or UVMap may be
	// authored here: both are destination->source coordinate contracts, which is what
	// makes the bundle remap a valid resampling rather than a reinterpretation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Fields")
	FMixtormatBehaviorFieldInput Direction;

	// Signed relief source. Push reads it as the delta it adds; Carve reads it as the
	// distance it carves with (OwnBoundary from this generator, or a typed SDF).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Fields")
	FMixtormatBehaviorFieldInput Height;

	// Optional 0..1 coverage, independently resolved. It multiplies the final weight
	// alongside the generator's own coverage and any scoped Mask child, and it is not
	// a replacement for either: it shapes where this Behavior acts, not whether the
	// generator runs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior|Fields")
	FMixtormatBehaviorFieldInput Influence;

	// The typed kind a coordinate socket derives from a local snapshot, so the
	// authored contract survives a re-authored reference. Gather reads it rather than
	// assuming a kind from the operation.
	EMixtormatBehaviorFieldKind CoordinateKind() const
	{
		return Direction.Kind;
	}
};
