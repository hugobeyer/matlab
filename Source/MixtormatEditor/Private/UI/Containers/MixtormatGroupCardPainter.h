// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"

// Card-only geometry/composition. Colour math and falloff stay in the shared primitives.
namespace MixtormatGroupCard
{
	struct FSurface
	{
		FVector2f Size = FVector2f::ZeroVector;
		float HeaderTop = 0.0f;
		float HeaderHeight = 0.0f;
		float BodyTop = 0.0f;
		float BodyHeight = 0.0f;
		float AuthoredHeaderHeight = 1.0f;
		float Radius = 0.0f;
		float Reach = 0.0f;
		float Power = 1.0f;
		float HeaderOpacity = 1.0f;
		float BodyOpacity = 0.0f;
		float HeaderSaturation = 1.0f;
		float BodySaturation = 1.0f;

		FLinearColor Ground = FLinearColor::Black;

		bool operator==(const FSurface& Other) const;
	};

	class FSurfacePainter
	{
	public:
		void Paint(FSlateWindowElementList& Elements, int32 LayerId, const FGeometry& Geometry,
			const FSurface& Surface, const FLinearColor& Tint, ESlateDrawEffect Effect);

	private:
		void Rebuild(const FSurface& Surface);
		FSurface CachedSurface;
		bool bHasSurface = false;
		TArray<FVector2f> Positions;
		TArray<FLinearColor> Colors;
		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
	};

	// Convex tangent strips approximate the rounded overflow boundary without textures/materials.
	// The caller must pop exactly the returned number of zones, after painting the children.
	int32 PushRoundedClip(FSlateWindowElementList& Elements, const FGeometry& Geometry, float Radius);
}
