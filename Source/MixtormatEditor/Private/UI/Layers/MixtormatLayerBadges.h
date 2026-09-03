#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaterial.h"

struct FMixtormatLayer;
struct FMixtormatLayerChild;

// What the fixed-width mark on the right of a layer row says.
//
// A badge is derived, never typed. It answers one question -- how does this row composite with
// what is under it -- and it is the only field in the stack a user cannot edit directly, which is
// what makes it trustworthy when a dozen layers are all called "Untitled".
//
// These are free functions rather than widget methods because the derivation is the interesting
// part and it should be readable, and testable, without constructing Slate.
//
// Every abbreviation fits MixtormatTokens::BadgeMaxCharacters. The badge box is a fixed width so
// the marks form a column down the edge of the stack; a longer word would clip rather than widen
// it, so the tables below are written against that limit.
namespace MixtormatLayerBadges
{
	// How a layer composites, as one choice instead of the three fields that encode it.
	//
	// ChannelMode, CompositionMode and NormalBlendMode are three booleans that only make sense in
	// four combinations, and asking a user to set them separately means offering states that mean
	// nothing (a NormalDetail layer with a Coat composition). This is that cross-product collapsed
	// into the four words the design uses -- the same four the badge prints.
	enum class EComposition : uint8
	{
		Blend,   // Replace + Combine  -- reoriented onto the normal below
		Over,    // Replace + Override -- replaces the normal outright
		Coat,    // sits over what is below
		Detail,  // contributes normal only
	};

	// Read the three fields; return the one choice they encode.
	EComposition CompositionOf(const FMixtormatLayer& Layer);

	// Write the three fields the choice implies. The inverse of CompositionOf, so a round trip
	// through the segmented control cannot land the layer in a state the badge cannot name.
	void ApplyComposition(FMixtormatLayer& Layer, EComposition Choice);

	// The words, in EComposition order, for the segmented control.
	TArray<FText> CompositionOptions();
	TArray<FText> CompositionToolTips();

	// Layer marks, in resolution order. The four cases are mutually exclusive and exhaustive:
	// EMixtormatCompositionMode has only Replace and Coat, so a Replace layer always falls through
	// to its normal blend mode.
	//
	//   NormalDetail channel        -> DETAIL   contributes normal only
	//   Coat composition            -> COAT     sits over what is below
	//   Replace + Override normals  -> OVER     replaces the normal outright
	//   Replace + Combine normals   -> BLEND    reoriented onto the normal below
	FText ForLayer(const FMixtormatLayer& Layer);

	// Mask marks. The blend mode abbreviated, since that is what a mask does to the accumulated
	// coverage under it.
	FText ForMaskBlendMode(EMixtormatMaskBlendMode Mode);

	// Effect marks. An effect has no composition mode, so its badge carries the effect's own type
	// -- which is the thing a user is scanning the column for.
	FText ForEffectType(EMixtormatEffectType Type);

	// Dispatches on the child's kind: masks and generated masks show their blend mode, effects
	// show their type.
	FText ForChild(const FMixtormatLayerChild& Child);

	// The uppercase word under a child's name saying what kind of thing it is -- MASK, FX, GEN.
	// The badge says how it combines; this says what it is.
	FText KindForChild(const FMixtormatLayerChild& Child);
}
