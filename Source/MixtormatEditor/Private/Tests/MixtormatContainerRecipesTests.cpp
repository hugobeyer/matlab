// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Style/MixtormatRecipes.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MixtormatContainerRecipesTests
{
	bool Near(const float A, const float B)
	{
		return FMath::IsNearlyEqual(A, B, 1.0e-5f);
	}

	bool NearColor(const FLinearColor& A, const FLinearColor& B)
	{
		return Near(A.R, B.R) && Near(A.G, B.G) && Near(A.B, B.B) && Near(A.A, B.A);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMixtormatContainerLocalColorTest,
	"Mixtormat.Style.Recipes.Containers.LocalColor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatContainerLocalColorTest::RunTest(const FString& Parameters)
{
	using namespace Mixtormat;
	using namespace MixtormatContainerRecipesTests;

	FMixtormatResolvedPalette Palette;
	const FLinearColor RoleColor(0.12f, 0.25f, 0.4f, 0.8f);
	Palette.Set(EMixtormatColorRole::Panel, RoleColor);
	FMixtormatColorRef Ref = MakeColorRef(EMixtormatColorRole::Panel);
	Ref.Multiplier = FLinearColor(0.5f, 0.8f, 0.6f, 1.0f);
	Ref.Opacity = 0.4f;
	Ref.Saturation = 1.7f;
	const auto Expected = [&Ref](FLinearColor Color)
	{
		Color.R *= Ref.Multiplier.R;
		Color.G *= Ref.Multiplier.G;
		Color.B *= Ref.Multiplier.B;
		Color.A *= Ref.Opacity;
		return MixtormatCompositing::Saturate(Color, Ref.Saturation);
	};
	TestTrue(TEXT("Unset local source retains palette tint, opacity and saturation"),
		NearColor(ResolveColor(Palette, Ref), Expected(RoleColor)));

	const FLinearColor Local(0.35f, 0.2f, 0.1f, 0.6f);
	Ref.LocalColor = Local;
	TestTrue(TEXT("Local source replaces role before modifiers, applied once"),
		NearColor(ResolveColor(Palette, Ref), Expected(Local)));
	Palette.Set(EMixtormatColorRole::Panel, FLinearColor::White);
	TestTrue(TEXT("Local source is independent of palette role changes"),
		NearColor(ResolveColor(Palette, Ref), Expected(Local)));
	Ref.LocalColor = FLinearColor::Transparent;
	TestTrue(TEXT("Set transparent local source does not fall back to palette"),
		NearColor(ResolveColor(Palette, Ref), FLinearColor::Transparent));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMixtormatFoldoutContainerRecipeTest,
	"Mixtormat.Style.Recipes.Containers.Foldout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatFoldoutContainerRecipeTest::RunTest(const FString& Parameters)
{
	using namespace Mixtormat;
	using namespace MixtormatContainerRecipesTests;
	using MixtormatCompositing::EMixtormatBlendMode;

	FMixtormatTheme Theme = MakeDefaultTheme();
	TestTrue(TEXT("Authored hover hairline hue"), NearColor(Theme.Foldout.HairlineHoverTint,
		FLinearColor::FromSRGBColor(FColor(127, 196, 219))));
	Theme.FoldoutLayout.Radius = 2.5f;
	// Non-default endpoints prove both passes share the full authored falloff, not just power.
	Theme.Foldout.LiftFalloff.Start = 0.75f;
	Theme.Foldout.LiftFalloff.End = 0.15f;
	Theme.Foldout.LiftFalloff.Power = 0.6f;
	for (const bool bHovered : { false, true })
	{
		const FMixtormatSurfaceRecipe Recipe = MakeFoldoutRecipe(Theme, bHovered);
		if (!TestEqual(TEXT("Lift then accent"), Recipe.Layers.Num(), 2)
			|| !TestEqual(TEXT("One enabled border"), Recipe.Borders.Num(), 1))
		{
			return false;
		}
		TestTrue(TEXT("Ground base, not Panel"), Recipe.Base.Role == EMixtormatColorRole::Ground
			&& !Recipe.Base.LocalColor.IsSet());
		TestTrue(TEXT("Layout owns foldout radius"), Near(Recipe.Radius, 2.5f));
		const FMixtormatPaintLayer& Lift = Recipe.Layers[0];
		const FMixtormatPaintLayer& Accent = Recipe.Layers[1];
		TestTrue(TEXT("Independent authored local lift hue"), Lift.Source.LocalColor.IsSet()
			&& NearColor(Lift.Source.LocalColor.GetValue(),
				bHovered ? Theme.Foldout.HoverTint : Theme.Foldout.LiftTint));
		TestTrue(TEXT("Independent lift opacity"), Near(Lift.Source.Opacity,
			bHovered ? Theme.Foldout.HoverTintOpacity : Theme.Foldout.LiftOpacity));
		TestTrue(TEXT("Accent follows lift and stays a palette role"),
			Accent.Source.Role == EMixtormatColorRole::Accent && !Accent.Source.LocalColor.IsSet());
		TestTrue(TEXT("Independent accent opacity"), Near(Accent.Source.Opacity,
			bHovered ? Theme.Foldout.AccentHoverOpacity : Theme.Foldout.AccentOpacity));
		const float Saturation = bHovered ? Theme.Foldout.HoverSaturation : Theme.Foldout.LiftSaturation;
		TestTrue(TEXT("Both passes carry state saturation once"),
			Near(Lift.Source.Saturation, Saturation) && Near(Accent.Source.Saturation, Saturation));
		TestTrue(TEXT("Authored blend order"), Lift.Blend == EMixtormatBlendMode::Normal
			&& Accent.Blend == EMixtormatBlendMode::SoftLight);
		TestEqual(TEXT("Six lift samples"), Lift.OpacityRamp.Points.Num(), 6);
		TestEqual(TEXT("Six accent samples"), Accent.OpacityRamp.Points.Num(), 6);
		for (const FMixtormatRampPoint& Stop : Lift.OpacityRamp.Points)
		{
			const float Expected = EvaluateFalloff(Theme.Foldout.LiftFalloff, Stop.Position);
			TestTrue(TEXT("Shared full falloff"), Near(Stop.Value, Expected)
				&& Near(EvaluateRamp(Accent.OpacityRamp, Stop.Position), Expected));
		}
		const FMixtormatBorderLayer& Line = Recipe.Borders[0];
		TestTrue(TEXT("Top-only 1px additive line"), Line.bTop && !Line.bBottom && !Line.bLeft
			&& !Line.bRight && Near(Line.Width, 1.0f) && Line.Blend == EMixtormatBlendMode::Additive);
		TestTrue(TEXT("Rest hairline role, hover local hue"), Line.Source.Role == EMixtormatColorRole::Hairline
			&& Line.Source.LocalColor.IsSet() == bHovered);
		if (bHovered && Line.Source.LocalColor.IsSet())
		{
			TestTrue(TEXT("Hover line keeps its authored local source"),
				NearColor(Line.Source.LocalColor.GetValue(), Theme.Foldout.HairlineHoverTint));
		}
		TestTrue(TEXT("Independent line opacity and saturation"),
			Near(Line.Source.Opacity, bHovered ? Theme.Foldout.HairlineHoverOpacity : Theme.Foldout.HairlineOpacity)
			&& Near(Line.Source.Saturation, bHovered ? Theme.Foldout.HairlineHoverSaturation : Theme.Foldout.HairlineSaturation));
		const FMixtormatSurfaceRecipe Disabled = MakeFoldoutRecipe(Theme, bHovered, false);
		TestEqual(TEXT("Disabled omits hairline"), Disabled.Borders.Num(), 0);
		TestEqual(TEXT("Disabled retains both paint layers"), Disabled.Layers.Num(), 2);
	}
	const FMixtormatSurfaceRecipe DefaultRecipe = MakeFoldoutRecipe(MakeDefaultTheme());
	TestTrue(TEXT("Default passes dissolve to zero"),
		Near(DefaultRecipe.Layers[0].OpacityRamp.Points.Last().Value, 0.0f)
		&& Near(DefaultRecipe.Layers[1].OpacityRamp.Points.Last().Value, 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMixtormatCardContainerRecipeTest,
	"Mixtormat.Style.Recipes.Containers.Cards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatCardContainerRecipeTest::RunTest(const FString& Parameters)
{
	using namespace Mixtormat;
	using namespace MixtormatContainerRecipesTests;

	const FMixtormatTheme Theme = MakeDefaultTheme();
	const FMixtormatSurfaceRecipe Ground = MakeGroundRecipe();
	TestTrue(TEXT("Ground-only recipe has no layers, borders or radius"),
		Ground.Base.Role == EMixtormatColorRole::Ground && !Ground.Base.LocalColor.IsSet()
		&& Ground.Layers.IsEmpty() && Ground.Borders.IsEmpty() && Near(Ground.Radius, 0.0f));
	FMixtormatFalloff Falloff;
	Falloff.Start = Theme.Card.HeaderOpacity;
	Falloff.End = Theme.Card.BodyOpacity;
	Falloff.Power = Theme.Card.FalloffPower;

	for (const float Seam : { 1.0f, 0.4f })
	{
		const FMixtormatSurfaceRecipe Header = MakeCardHeaderRecipe(Theme, Seam);
		if (!TestEqual(TEXT("One header layer"), Header.Layers.Num(), 1)) { return false; }
		const FMixtormatRamp& HeaderRamp = Header.Layers[0].OpacityRamp;
		if (!TestEqual(TEXT("Six header samples"), HeaderRamp.Points.Num(), 6)) { return false; }
		TestTrue(TEXT("Header uses Ground base and source"), Header.Base.Role == EMixtormatColorRole::Ground
			&& Header.Layers[0].Source.Role == EMixtormatColorRole::Ground
			&& !Header.Layers[0].Source.LocalColor.IsSet());
		TestTrue(TEXT("Header has its own saturation and no border/radius"),
			Near(Header.Layers[0].Source.Saturation, Theme.Card.HeaderSaturation)
			&& Header.Borders.IsEmpty() && Near(Header.Radius, 0.0f));
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const float T = static_cast<float>(Index) / 5.0f;
			TestTrue(TEXT("Header samples mapped domain through shared power curve"),
				Near(HeaderRamp.Points[Index].Position, T)
				&& Near(HeaderRamp.Points[Index].Value, EvaluateFalloff(Falloff, T * Seam)));
		}
		for (const float Tail : { -0.1f, 0.0f, 0.2f, 0.5f })
		{
			const FMixtormatSurfaceRecipe Body = MakeCardBodyRecipe(Theme, Seam, Tail);
			if (!TestEqual(TEXT("One body layer"), Body.Layers.Num(), 1)) { return false; }
			const FMixtormatRamp& Ramp = Body.Layers[0].OpacityRamp;
			TestTrue(TEXT("Body uses Ground with separate saturation and no border/radius"),
				Body.Base.Role == EMixtormatColorRole::Ground
				&& Body.Layers[0].Source.Role == EMixtormatColorRole::Ground
				&& !Body.Layers[0].Source.LocalColor.IsSet()
				&& Near(Body.Layers[0].Source.Saturation, Theme.Card.BodySaturation)
				&& Body.Borders.IsEmpty() && Near(Body.Radius, 0.0f));
			TestTrue(TEXT("Middle holds body floor"), Near(EvaluateRamp(Ramp, 0.5f), Theme.Card.BodyOpacity));
			if (Tail <= 0.0f)
			{
				TestEqual(TEXT("Zero/negative reach has two constant stops"), Ramp.Points.Num(), 2);
				TestTrue(TEXT("Zero reach is flat regardless of seam"),
					Near(EvaluateRamp(Ramp, 0.0f), Theme.Card.BodyOpacity)
					&& Near(EvaluateRamp(Ramp, 1.0f), Theme.Card.BodyOpacity));
				if (Seam == 1.0f)
				{
					TestTrue(TEXT("Zero-reach seam is continuous"),
						Near(HeaderRamp.Points.Last().Value, EvaluateRamp(Ramp, 0.0f)));
				}
				continue;
			}
			if (!TestEqual(TEXT("Mirrored stops omit only duplicate center"),
				Ramp.Points.Num(), Tail == 0.5f ? 11 : 12)) { return false; }
			TestTrue(TEXT("Header/body opacity seam is continuous"),
				Near(HeaderRamp.Points.Last().Value, Ramp.Points[0].Value));
			for (int32 Index = 0; Index < 6; ++Index)
			{
				const float T = static_cast<float>(Index) / 5.0f;
				const FMixtormatRampPoint& Stop = Ramp.Points[Index];
				TestTrue(TEXT("Body domain continues header falloff"), Near(Stop.Position, T * Tail)
					&& Near(Stop.Value, EvaluateFalloff(Falloff, Seam + T * (1.0f - Seam))));
				TestTrue(TEXT("Bottom mirrors body tail, not full header"),
					Near(EvaluateRamp(Ramp, 1.0f - Stop.Position), Stop.Value));
			}
			for (int32 Index = 1; Index < Ramp.Points.Num(); ++Index)
			{
				TestTrue(TEXT("Stops strictly ordered"), Ramp.Points[Index].Position > Ramp.Points[Index - 1].Position);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMixtormatContainerBlendRecipeTest,
	"Mixtormat.Style.Recipes.Containers.BlendsAndCompositing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatContainerBlendRecipeTest::RunTest(const FString& Parameters)
{
	using namespace Mixtormat;
	using namespace MixtormatContainerRecipesTests;
	using namespace MixtormatCompositing;

	FMixtormatTheme Theme = MakeDefaultTheme();
	FMixtormatResolvedPalette Palette;
	const FLinearColor Ground(0.12f, 0.18f, 0.22f, 1.0f);
	const FLinearColor Accent(0.4f, 0.25f, 0.1f, 1.0f);
	Palette.Set(EMixtormatColorRole::Ground, Ground);
	Palette.Set(EMixtormatColorRole::Accent, Accent);
	Palette.Set(EMixtormatColorRole::Panel, FLinearColor::White);
	const EMixtormatBlendMode Blends[] = { EMixtormatBlendMode::Normal, EMixtormatBlendMode::Additive,
		EMixtormatBlendMode::Multiply, EMixtormatBlendMode::SoftLight };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Theme.Foldout.LiftBlend = Blends[Index];
		Theme.Foldout.AccentBlend = Blends[(Index + 1) % 4];
		Theme.Card.Blend = Blends[Index];
		for (const bool bHovered : { false, true })
		{
			const FMixtormatSurfaceRecipe Recipe = MakeFoldoutRecipe(Theme, bHovered);
			TestTrue(TEXT("Each independent foldout blend propagates"),
				Recipe.Layers[0].Blend == Theme.Foldout.LiftBlend
				&& Recipe.Layers[1].Blend == Theme.Foldout.AccentBlend);
			const float Saturation = bHovered ? Theme.Foldout.HoverSaturation : Theme.Foldout.LiftSaturation;
			FLinearColor Lift = Saturate(bHovered ? Theme.Foldout.HoverTint : Theme.Foldout.LiftTint, Saturation);
			Lift.A *= bHovered ? Theme.Foldout.HoverTintOpacity : Theme.Foldout.LiftOpacity;
			FLinearColor Cross = Saturate(Accent, Saturation);
			Cross.A *= bHovered ? Theme.Foldout.AccentHoverOpacity : Theme.Foldout.AccentOpacity;
			const FLinearColor Expected = ApplyBlend(Theme.Foldout.AccentBlend,
				ApplyBlend(Theme.Foldout.LiftBlend, Ground, Lift), Cross);
			FMixtormatSurfaceSamples Samples;
			CompositeSurface(Recipe, Palette, FMixtormatStateModifier(), Samples);
			if (!TestTrue(TEXT("Foldout composites samples"), Samples.Colors.Num() > 0)) { return false; }
			TestTrue(TEXT("Ordered composition saturates each source once"), NearColor(Samples.Colors[0], Expected));
			Palette.Set(EMixtormatColorRole::Panel, FLinearColor::Black);
			FMixtormatSurfaceSamples WithoutPanel;
			CompositeSurface(Recipe, Palette, FMixtormatStateModifier(), WithoutPanel);
			TestTrue(TEXT("Foldout is independent of Panel"), WithoutPanel.Colors.Num() > 0
				&& NearColor(Samples.Colors[0], WithoutPanel.Colors[0]));
			Palette.Set(EMixtormatColorRole::Panel, FLinearColor::White);
		}
		for (const bool bBody : { false, true })
		{
			const FMixtormatSurfaceRecipe Recipe = bBody
				? MakeCardBodyRecipe(Theme, 1.0f, 0.0f) : MakeCardHeaderRecipe(Theme, 1.0f);
			TestTrue(TEXT("All four card blends propagate"), Recipe.Layers[0].Blend == Blends[Index]);
			FLinearColor Source = Saturate(Ground, bBody ? Theme.Card.BodySaturation : Theme.Card.HeaderSaturation);
			Source.A *= bBody ? Theme.Card.BodyOpacity : Theme.Card.HeaderOpacity;
			FMixtormatSurfaceSamples Samples;
			CompositeSurface(Recipe, Palette, FMixtormatStateModifier(), Samples);
			TestTrue(TEXT("Cards composite Ground, not Accent, with saturation once"), Samples.Colors.Num() > 0
				&& NearColor(Samples.Colors[0], ApplyBlend(Theme.Card.Blend, Ground, Source)));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMixtormatContainerValidationTest,
	"Mixtormat.Style.Recipes.Containers.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatContainerValidationTest::RunTest(const FString& Parameters)
{
	using namespace Mixtormat;
	using namespace MixtormatContainerRecipesTests;
	FMixtormatTheme Theme = MakeDefaultTheme();
	Theme.Card.Reach = -5.0f;
	Theme.Foldout.HairlineHoverTint.A = 1.5f;
	TArray<FText> Issues;
	ValidateTheme(Theme, Issues);
	TestTrue(TEXT("Negative reach clamped"), Near(Theme.Card.Reach, 0.0f));
	TestTrue(TEXT("Local hover hue alpha clamped"), Near(Theme.Foldout.HairlineHoverTint.A, 1.0f));
	TestEqual(TEXT("New invalid values reported"), Issues.Num(), 2);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
