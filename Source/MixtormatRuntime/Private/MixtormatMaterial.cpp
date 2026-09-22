// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatMaterial.h"
#include "MixtormatLayerGroups.h"
#include "MixtormatParameterBinding.h"

namespace
{
	// A member of a disabled group renders as hidden without its own bEnabled ever being touched
	// -- the same rule BuildEffectiveLayers applies, for the aggregates that never go through it.
	bool IsLayerEffectivelyEnabled(
		const FMixtormatLayer& Layer,
		const TArray<FMixtormatLayerGroup>& Groups)
	{
		if (!Layer.bEnabled)
		{
			return false;
		}
		const FMixtormatLayerGroup* Group = MixtormatLayerGroups::FindGroup(Groups, Layer.GroupId);
		return !Group || Group->bEnabled;
	}
}

bool MixtormatCompositionReferences::Validate(
	const TArray<FMixtormatLayer>& Layers,
	const FSoftObjectPath& OwnerPath,
	FText& OutError)
{
	OutError = FText::GetEmpty();
	TSet<const UMixtormatMaterial*> Active;
	TMap<const UMixtormatMaterial*, int32> ValidatedDepths;
	TFunction<bool(const TArray<FMixtormatLayer>&, int32)> Visit;
	Visit = [&](const TArray<FMixtormatLayer>& CurrentLayers, const int32 Depth)
	{
		for (const FMixtormatLayer& Layer : CurrentLayers)
		{
			if (Layer.SourceComposition.IsNull())
			{
				continue;
			}
			const FSoftObjectPath SourcePath = Layer.SourceComposition.ToSoftObjectPath();
			const FText SourceName = FText::FromString(SourcePath.ToString());
			if (Depth >= 32)
			{
				OutError = FText::Format(NSLOCTEXT("MixtormatMaterial", "CompositionReferenceTooDeep",
					"Reference nesting exceeds the supported 32 levels at {0}."), SourceName);
				return false;
			}
			if (Layer.Type != EMixtormatLayerType::Material || !Layer.SourceSurface.IsNull())
			{
				OutError = FText::Format(NSLOCTEXT("MixtormatMaterial", "InvalidReferenceSource",
					"Reference {0} must be a Material layer with no surface source."), SourceName);
				return false;
			}
			if (!OwnerPath.IsNull() && SourcePath == OwnerPath)
			{
				OutError = FText::Format(NSLOCTEXT("MixtormatMaterial", "SelfCompositionReference",
					"Reference {0} leads back to the composition being saved."), SourceName);
				return false;
			}
			const UMixtormatMaterial* Source = Layer.SourceComposition.LoadSynchronous();
			if (!Source)
			{
				OutError = FText::Format(NSLOCTEXT("MixtormatMaterial", "MissingCompositionReference",
					"Referenced composition {0} could not be loaded."), SourceName);
				return false;
			}
			// Compare the resolved path too, so a redirector cannot conceal a self reference.
			if (!OwnerPath.IsNull() && FSoftObjectPath(Source) == OwnerPath)
			{
				OutError = FText::Format(NSLOCTEXT("MixtormatMaterial", "SelfCompositionReference",
					"Reference {0} leads back to the composition being saved."), SourceName);
				return false;
			}
			if (Active.Contains(Source))
			{
				OutError = FText::Format(NSLOCTEXT("MixtormatMaterial", "CyclicCompositionReference",
					"Reference cycle detected at {0}."), SourceName);
				return false;
			}
			const int32 NextDepth = Depth + 1;
			if (const int32* ValidatedDepth = ValidatedDepths.Find(Source);
				ValidatedDepth && *ValidatedDepth >= NextDepth)
			{
				continue;
			}
			Active.Add(Source);
			if (!Visit(Source->Layers, NextDepth))
			{
				return false;
			}
			Active.Remove(Source);
			ValidatedDepths.Add(Source, NextDepth);
		}
		return true;
	};
	return Visit(Layers, 0);
}

float MixtormatCompositionReferences::ComputeFuzzInfluence(
	const TArray<FMixtormatLayer>& Layers)
{
	return ComputeFuzzInfluence(Layers, TArray<FMixtormatLayerGroup>());
}

float MixtormatCompositionReferences::ComputeFuzzInfluence(
	const TArray<FMixtormatLayer>& Layers,
	const TArray<FMixtormatLayerGroup>& Groups)
{
	TSet<const UMixtormatMaterial*> Active;
	TFunction<float(const TArray<FMixtormatLayer>&, const TArray<FMixtormatLayerGroup>&)> Visit;
	Visit = [&](const TArray<FMixtormatLayer>& CurrentLayers,
		const TArray<FMixtormatLayerGroup>& CurrentGroups)
	{
		float Result = 0.0f;
		for (const FMixtormatLayer& Layer : CurrentLayers)
		{
			// Fuzz is resolved outside the compositor, so it never sees the effective stack and
			// has to apply the group gate itself.
			if (!IsLayerEffectivelyEnabled(Layer, CurrentGroups))
			{
				continue;
			}

			float Target = FMath::Clamp(Layer.FuzzInfluence, 0.0f, 1.0f);
			if (!Layer.SourceComposition.IsNull())
			{
				const UMixtormatMaterial* Source = Layer.SourceComposition.LoadSynchronous();
				if (!Source || Active.Contains(Source))
				{
					continue;
				}
				Active.Add(Source);
				Target = Visit(Source->Layers, Source->LayerGroups);
				Active.Remove(Source);
			}
			if (Target > 0.0f)
			{
				Result = FMath::Lerp(
					Result, Target, FMath::Clamp(Layer.Opacity, 0.0f, 1.0f));
			}
		}
		return FMath::Clamp(Result, 0.0f, 1.0f);
	};
	return Visit(Layers, Groups);
}

TOptional<FLinearColor> MixtormatCompositionReferences::ComputeFuzzColor(
	const TArray<FMixtormatLayer>& Layers)
{
	return ComputeFuzzColor(Layers, TArray<FMixtormatLayerGroup>());
}

TOptional<FLinearColor> MixtormatCompositionReferences::ComputeFuzzColor(
	const TArray<FMixtormatLayer>& Layers,
	const TArray<FMixtormatLayerGroup>& Groups)
{
	TOptional<FLinearColor> Result;
	float Strongest = 0.0f;
	TSet<const UMixtormatMaterial*> Active;
	TFunction<void(const TArray<FMixtormatLayer>&, const TArray<FMixtormatLayerGroup>&)> Visit;
	Visit = [&](const TArray<FMixtormatLayer>& CurrentLayers,
		const TArray<FMixtormatLayerGroup>& CurrentGroups)
	{
		for (const FMixtormatLayer& Layer : CurrentLayers)
		{
			if (!IsLayerEffectivelyEnabled(Layer, CurrentGroups) || Layer.Opacity <= 0.0f)
			{
				continue;
			}
			if (!Layer.SourceComposition.IsNull())
			{
				const UMixtormatMaterial* Source = Layer.SourceComposition.LoadSynchronous();
				if (Source && !Active.Contains(Source))
				{
					Active.Add(Source);
					Visit(Source->Layers, Source->LayerGroups);
					Active.Remove(Source);
				}
				continue;
			}
			const float Influence = FMath::Clamp(Layer.FuzzInfluence, 0.0f, 1.0f);
			if (Influence > 0.0f && Influence >= Strongest)
			{
				Strongest = Influence;
				Result = Layer.FuzzColor;
			}
		}
	};
	Visit(Layers, Groups);
	return Result;
}

void UMixtormatMaterial::PostLoad()
{
	Super::PostLoad();
	// Layer identity first: group validation reconciles group children against the layer child
	// IDs, so those have to be settled before it looks at them.
	MixtormatParameterBinding::EnsureStableIds(Layers, LayerGroups);
	MixtormatLayerGroups::ValidateGroups(Layers, LayerGroups);
}

FPrimaryAssetId UMixtormatMaterial::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MixtormatMaterial"), GetFName());
}

bool UMixtormatMaterial::CanAddLayer() const
{
	return true;
}

bool UMixtormatMaterial::AddLayer(const EMixtormatLayerType Type)
{
	if (!CanAddLayer())
	{
		return false;
	}

	FMixtormatLayer& Layer = Layers.AddDefaulted_GetRef();
	Layer.Type = Type;

	switch (Type)
	{
	case EMixtormatLayerType::Material:
		Layer.DisplayName = NSLOCTEXT("MixtormatMaterial", "MaterialLayer", "Material Layer");
		break;
	case EMixtormatLayerType::Fill:
		Layer.DisplayName = NSLOCTEXT("MixtormatMaterial", "FillLayer", "Fill Layer");
		Layer.bOverrideBaseColor = true;
		Layer.bOverrideRoughness = true;
		Layer.bOverrideIOR = true;
		Layer.bOverrideMetallic = true;
		break;
	case EMixtormatLayerType::Effect:
		Layer.DisplayName = NSLOCTEXT("MixtormatMaterial", "EffectLayer", "Effect Layer");
		break;
	}

	return true;
}
