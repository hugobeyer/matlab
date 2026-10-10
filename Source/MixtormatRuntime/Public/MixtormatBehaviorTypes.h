// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatOutputReference.h"
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
	Deform UMETA(DisplayName = "Deform")
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
