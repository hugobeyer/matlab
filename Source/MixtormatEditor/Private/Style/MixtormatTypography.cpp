// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatTypography.h"

#include "Fonts/CompositeFont.h"
#include "Interfaces/IPluginManager.h"
#include "MixtormatEditorModule.h"
#include "Misc/Paths.h"
#include "Style/MixtormatResolvedStyle.h"
#include "Styling/CoreStyle.h"

namespace Mixtormat
{
	namespace
	{
		// The typeface names Slate will look up inside the composite font's DefaultTypeface. These
		// are the same names the engine uses for Roboto in
		// FLegacySlateFontInfoCache::GetDefaultFont, so the resolution path is the engine's own
		// rather than something Mixtormat invented.
		const FName InterRegularTypeface(TEXT("Regular"));
		const FName InterSemiBoldTypeface(TEXT("SemiBold"));
		const FName InterBoldTypeface(TEXT("Bold"));

		// The shipped faces: static instances generated from the OFL variable font
		// (Resources/Fonts/Inter.ttf, wght 100..900) with fontTools.varLib.instancer.
		//
		// Static files, not one variable file. Inter.ttf *does* contain these weights, but SlateCore
		// 5.8 has no variation-axis support of any kind -- no FT_Set_Var_Design_Coordinates, no
		// weight axis, nothing under SlateCore/Public or SlateCore/Private -- so a typeface entry
		// resolves to a file and nothing else. Two typeface names pointed at one variable TTF would
		// produce two entries with identical outlines.
		//
		// Each was instanced with opsz pinned to 14, its axis minimum: Mixtormat renders 8-11px
		// type, and leaving optical size live would mean the outlines were never actually resolved
		// for the sizes this UI actually uses.
		struct FInterFace
		{
			EMixtormatFontWeight Weight;
			const TCHAR* FileName;
			FName Typeface;
		};

		const FInterFace InterFaces[] = {
			{ EMixtormatFontWeight::Regular, TEXT("Inter-Regular.ttf"), InterRegularTypeface },
			{ EMixtormatFontWeight::SemiBold, TEXT("Inter-SemiBold.ttf"), InterSemiBoldTypeface },
			{ EMixtormatFontWeight::Bold, TEXT("Inter-Bold.ttf"), InterBoldTypeface },
		};

		// The typeface Slate's own default font uses for a given weight. Only meaningful on the
		// fallback path, where the engine's composite font is in play and its names do resolve.
		const TCHAR* EngineTypefaceFor(const EMixtormatFontWeight Weight)
		{
			return Weight == EMixtormatFontWeight::Regular ? TEXT("Regular") : TEXT("Bold");
		}

		// Module-lifetime ownership.
		//
		// FStandaloneCompositeFont derives FGCObject precisely so a font that is not embedded in a
		// UObject keeps its bulk data referenced; holding it in a function-local static keeps it
		// alive for as long as Slate can be asked for a Mixtormat font. A TSharedRef member of a
		// namespace-scope variable would work equally, but a function-local static initialises on
		// first use, which means no module-load-order dependency on IPluginManager.
		//
		// This is the same construction FLegacySlateFontInfoCache uses for the engine's own default
		// font.
		struct FFontState
		{
			TSharedPtr<FStandaloneCompositeFont> CompositeFont;
			// Which weights genuinely resolved, per weight. Reported by IsWeightAvailable so the
			// authoring UI can refuse to offer a face that renders identically to another.
			bool bAvailable[3] = { false, false, false };
			FString FontDirectory;
		};

		FFontState& GetState()
		{
			static FFontState State;
			return State;
		}

		int32 FaceIndex(const EMixtormatFontWeight Weight)
		{
			return static_cast<int32>(Weight);
		}

		FString GetFontDirectory()
		{
			// Resolved through the plugin manager rather than FPaths::ProjectDir or a relative path:
			// the plugin may be installed anywhere, and a relative path would be relative to the
			// project, not to the plugin.
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Mixtormat"));
			if (!Plugin.IsValid())
			{
				return FString();
			}
			return Plugin->GetBaseDir() / TEXT("Resources") / TEXT("Fonts");
		}
	}

	bool FMixtormatTypography::Initialize()
	{
		FFontState& State = GetState();

		if (State.CompositeFont.IsValid())
		{
			// Already built. Idempotent by design: several modules construct Mixtormat text and
			// none of them should have to coordinate who goes first.
			return State.bAvailable[FaceIndex(EMixtormatFontWeight::Regular)];
		}

		const FString FontDirectory = GetFontDirectory();
		if (FontDirectory.IsEmpty())
		{
			UE_LOG(LogMixtormat, Warning,
				TEXT("Mixtormat: could not resolve the plugin base directory; Inter will not load and Mixtormat text will use the engine default font."));
			return false;
		}
		State.FontDirectory = FontDirectory;

		// Default-constructed, then appended -- the same shape the engine uses to build its own
		// default font from Roboto. Each typeface entry is a distinct file.
		TSharedRef<FStandaloneCompositeFont> Font = MakeShared<FStandaloneCompositeFont>();

		for (const FInterFace& Face : InterFaces)
		{
			const FString FacePath = FontDirectory / Face.FileName;
			if (!FPaths::FileExists(FacePath))
			{
				UE_LOG(LogMixtormat, Warning,
					TEXT("Mixtormat: %s is missing. Text authored at %d will render at the nearest available weight instead."),
					Face.FileName, Mixtormat::ToCssWeight(Face.Weight));
				continue;
			}

			// LazyLoad rather than Inline: the files are on disk in both editor and packaged builds,
			// and pulling ~340KB per face up front for a plugin UI is not a trade worth making.
			// Inline is only available for font data embedded in a Font Face asset anyway.
			Font->DefaultTypeface.AppendFont(Face.Typeface, FacePath, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
			State.bAvailable[FaceIndex(Face.Weight)] = true;
		}

		if (!State.bAvailable[FaceIndex(EMixtormatFontWeight::Regular)])
		{
			UE_LOG(LogMixtormat, Warning,
				TEXT("Mixtormat: no Inter face resolved from %s. Mixtormat text will use the engine default font. Check that the plugin's Resources/Fonts directory is staged."),
				*FontDirectory);
			return false;
		}

		State.CompositeFont = Font;
		return true;
	}

	void FMixtormatTypography::Shutdown()
	{
		FFontState& State = GetState();

		// TCompositeFontCache keys on the composite font pointer, so dropping it while text may
		// still be asked for is the one unsafe moment here. Shutdown is only called when the module
		// is unloading, after which no further Mixtormat text is constructed.
		State.CompositeFont.Reset();
		State.bAvailable[0] = false;
		State.bAvailable[1] = false;
		State.bAvailable[2] = false;
		State.FontDirectory.Reset();
	}

	bool FMixtormatTypography::IsAvailable()
	{
		Initialize();
		return GetState().bAvailable[FaceIndex(EMixtormatFontWeight::Regular)];
	}

	bool FMixtormatTypography::IsWeightAvailable(const EMixtormatFontWeight Weight)
	{
		Initialize();
		const int32 Index = FaceIndex(Weight);
		// Bounds-checked rather than trusting the enum: this is reachable from an authoring UI and
		// from persisted theme data, and an out-of-range cast must read as "unavailable", not as a
		// read past the end of the array.
		return Index >= 0 && Index < UE_ARRAY_COUNT(GetState().bAvailable)
			&& GetState().bAvailable[Index];
	}

	EMixtormatFontWeight FMixtormatTypography::FromCssWeight(const float CssWeight)
	{
		// Snap to the nearest face we actually ship rather than to a midpoint. The prototype's only
		// two authored weights are 400 and 600, plus 700 for the brand mark; the bands are centred
		// on those so 400 and 600 land exactly and anything between lands on the closer neighbour.
		if (CssWeight <= 500.0f)
		{
			return EMixtormatFontWeight::Regular;
		}
		if (CssWeight <= 650.0f)
		{
			return EMixtormatFontWeight::SemiBold;
		}
		return EMixtormatFontWeight::Bold;
	}

	int32 FMixtormatTypography::TrackingToSlate(const float TrackingPx, const float SizePx)
	{
		if (SizePx <= UE_KINDA_SMALL_NUMBER)
		{
			return 0;
		}
		return FMath::RoundToInt(TrackingPx / SizePx * 1000.0f);
	}

	FString FMixtormatTypography::GetFontFilePath()
	{
		Initialize();
		const FFontState& State = GetState();
		return State.FontDirectory.IsEmpty()
			? FString()
			: State.FontDirectory / InterFaces[FaceIndex(EMixtormatFontWeight::Regular)].FileName;
	}

	FSlateFontInfo FMixtormatTypography::MakeFont(const EMixtormatFontWeight Weight, const float Size,
		const float TrackingPx, const bool bForceMonospaced)
	{
		if (!Initialize())
		{
			// The single documented fallback path in the whole typography system. It exists so a
			// missing font file degrades to a readable interface rather than to no text at all, and
			// it is the only reason FCoreStyle appears in this file.
			//
			// Note what is NOT faked here: no TypefaceFontName is overwritten. The fallback uses
			// the engine's own typeface names against the engine's own composite font, which is
			// the one case where that name resolves to what it says. SemiBold has no counterpart
			// there and is approximated by the engine's Bold -- an approximation confined to a
			// degraded state, not to the normal one.
			return FCoreStyle::GetDefaultFontStyle(EngineTypefaceFor(Weight), Size);
		}

		FFontState& State = GetState();

		// An unavailable weight resolves to Regular rather than to a name the composite font does
		// not define. Slate's own GetFontData falls back to the primary face for an unknown name,
		// so this matches its behaviour deliberately instead of relying on it.
		const int32 Index = FaceIndex(Weight);
		FName Typeface = InterRegularTypeface;
		if (Index >= 0 && Index < UE_ARRAY_COUNT(InterFaces) && State.bAvailable[Index])
		{
			Typeface = InterFaces[Index].Typeface;
		}

		// FSlateFontInfo's composite-font constructor, which sets FontObject to null and stores this
		// pointer directly -- so GetCompositeFont() hands it straight to the typeface cache with
		// no registration and no lookup by name. The conversion is spelled out rather than
		// implicit because it is a two-step Ref -> Ptr and const-and-base conversion.
		const TSharedPtr<const FCompositeFont> Composite = State.CompositeFont;
		FSlateFontInfo Result(Composite, Size, Typeface);
		Result.LetterSpacing = TrackingToSlate(TrackingPx, Size);
		Result.bForceMonospaced = bForceMonospaced;

		return Result;
	}

	FSlateFontInfo FMixtormatTypography::MakeFont(const FMixtormatTextSpec& Spec)
	{
		return MakeFont(Spec.Weight, Spec.Size, Spec.TrackingPx, Spec.bMonospacedNumbers);
	}

	FTextBlockStyle FMixtormatTypography::MakeTextStyle(const FMixtormatTextSpec& Spec, const FLinearColor& Color)
	{
		FTextBlockStyle Style;
		Style.SetFont(MakeFont(Spec));

		// The role's opacity multiplies the palette colour rather than replacing its alpha, so a
		// role can be dimmed without a second colour being authored for the dimmed case.
		Style.SetColorAndOpacity(FLinearColor(Color.R, Color.G, Color.B, Color.A * Spec.Opacity));

		return Style;
	}

	FMixtormatTextSpec FMixtormatTypography::GetSpec(const FMixtormatResolvedTypography& Typography,
		const EMixtormatTextRole Role)
	{
		const uint8 Index = static_cast<uint8>(Role);
		if (Index >= FMixtormatResolvedTypography::RoleCount)
		{
			return FMixtormatTextSpec();
		}
		return Typography.Roles[Index];
	}

	FText FMixtormatTypography::ApplyCase(const FText& In, const FMixtormatTextSpec& Spec)
	{
		return Spec.bUppercase ? FText::AsCultureInvariant(In.ToString().ToUpper()) : In;
	}
}

#undef LOCTEXT_NAMESPACE