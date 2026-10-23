// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Services/MixtormatPresetStore.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <initializer_list>

#define LOCTEXT_NAMESPACE "MixtormatPresetStore"

namespace MixtormatPresetStore
{
	FString GetUserPresetDirectory()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Mixtormat"), TEXT("Presets"));
	}

	namespace Private
	{
		constexpr int32 SchemaVersion = 1;
		constexpr int64 MaxFileBytes = 64 * 1024;
		constexpr int32 MaxNameLength = 128;
		constexpr int32 MaxJsonDepth = 8;

		FString Directory()
		{
			return GetUserPresetDirectory();
		}

		bool Fail(FText& Error, const FText& Message)
		{
			Error = Message;
			return false;
		}

		bool Validate(const FRampPreset& Preset, FText& Error)
		{
			if (!Preset.Id.IsValid() || Preset.Name.TrimStartAndEnd().IsEmpty()
				|| Preset.Name.Len() > MaxNameLength || Preset.Name.Contains(TEXT("\n"))
				|| Preset.Name.Contains(TEXT("\r")))
			{
				return Fail(Error, LOCTEXT("Identity", "A ramp preset needs a valid ID and a single-line name (1–128 characters)."));
			}
			if (!FMath::IsFinite(Preset.DomainMin) || !FMath::IsFinite(Preset.DomainMax)
				|| Preset.DomainMin >= Preset.DomainMax || !FMath::IsFinite(Preset.DomainMax - Preset.DomainMin))
			{
				return Fail(Error, LOCTEXT("Domain", "The ramp domain must have finite, increasing bounds."));
			}
			const bool bScalar = Preset.Kind == ERampKind::Scalar;
			if (!bScalar && Preset.Kind != ERampKind::Color)
			{
				return Fail(Error, LOCTEXT("Kind", "Unsupported ramp preset kind."));
			}
			const int32 Count = bScalar ? Preset.Scalar.Points.Num() : Preset.Color.Stops.Num();
			const int32 Maximum = bScalar ? FMixtormatScalarRamp::MaxPoints : FMixtormatColorRamp::MaxStops;
			const int32 Interpolation = bScalar ? static_cast<int32>(Preset.Scalar.Interpolation)
				: static_cast<int32>(Preset.Color.Interpolation);
			if (Count < 2 || Count > Maximum || Interpolation < 0 || Interpolation > (bScalar ? static_cast<int32>(EMixtormatScalarRampInterpolation::BSpline)
								: static_cast<int32>(EMixtormatColorRampInterpolation::Smooth))
				|| (bScalar && (Preset.Scalar.DomainMin != Preset.DomainMin || Preset.Scalar.DomainMax != Preset.DomainMax)))
			{
				return Fail(Error, LOCTEXT("Shape", "Invalid ramp point count, interpolation or scalar domain."));
			}
			float Previous = Preset.DomainMin;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const float X = bScalar ? Preset.Scalar.Points[Index].X : Preset.Color.Stops[Index].X;
				if (!FMath::IsFinite(X) || X < Previous || X > Preset.DomainMax)
				{
					return Fail(Error, LOCTEXT("Position", "Ramp points must be sorted and inside the authored domain."));
				}
				Previous = X;
				if (bScalar)
				{
					const float Y = Preset.Scalar.Points[Index].Y;
					if (!FMath::IsFinite(Y) || Y < FMixtormatScalarRamp::MinOutput || Y > FMixtormatScalarRamp::MaxOutput)
					{
						return Fail(Error, LOCTEXT("ScalarValue", "A scalar ramp value is outside the supported output range."));
					}
				}
				else
				{
					const FLinearColor& Color = Preset.Color.Stops[Index].Color;
					if (!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G)
						|| !FMath::IsFinite(Color.B) || !FMath::IsFinite(Color.A)
												|| Color != Color.GetClamped())
					{
						return Fail(Error, LOCTEXT("ColorValue", "Ramp colors must contain finite components in the supported 0–1 range."));
					}
				}
			}
			if (bScalar)
			{
				FMixtormatScalarRamp Sanitized = Preset.Scalar;
				Sanitized.Sanitize();
				for (int32 Index = 0; Index < Count; ++Index)
				{
					if (Sanitized.Points[Index].X != Preset.Scalar.Points[Index].X)
					{
						return Fail(Error, LOCTEXT("Spacing", "Scalar ramp endpoints or spacing would require correction. Preset rejected without changes."));
					}
				}
			}
			return true;
		}

		bool Read(const FString& Filename, FRampPreset& OutPreset, FText& Error)
		{
			const int64 Size = IFileManager::Get().FileSize(*Filename);
			FString Text;
			if (Size <= 0 || Size > MaxFileBytes || !FFileHelper::LoadFileToString(Text, *Filename))
			{
				return Fail(Error, LOCTEXT("Read", "Cannot read preset JSON, or file exceeds 64 KiB."));
			}
			// Bound nesting before handing untrusted JSON to the recursive parser.
			int32 Depth = 0;
			bool bString = false;
			bool bEscape = false;
			for (const TCHAR Character : Text)
			{
				if (bString)
				{
					if (bEscape) { bEscape = false; }
					else if (Character == TEXT('\\')) { bEscape = true; }
					else if (Character == TEXT('"')) { bString = false; }
				}
				else if (Character == TEXT('"')) { bString = true; }
				else if (Character == TEXT('{') || Character == TEXT('['))
				{
					if (++Depth > MaxJsonDepth) { return Fail(Error, LOCTEXT("Depth", "Preset JSON nesting exceeds the supported limit.")); }
				}
				else if (Character == TEXT('}') || Character == TEXT(']')) { --Depth; }
			}
			TSharedPtr<FJsonObject> Object;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) || !Object.IsValid())
			{
				return Fail(Error, LOCTEXT("Json", "Invalid preset JSON object."));
			}
			FRampPreset Preset;
			double Version = 0, Min = 0, Max = 0, Interpolation = 0;
			FString Id, Kind;
			const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
			if (!Object->TryGetNumberField(TEXT("schema"), Version) || Version != SchemaVersion
				|| !Object->TryGetStringField(TEXT("id"), Id) || !FGuid::Parse(Id, Preset.Id)
				|| !Object->TryGetStringField(TEXT("name"), Preset.Name)
				|| !Object->TryGetStringField(TEXT("kind"), Kind)
				|| (Kind != TEXT("scalarRamp") && Kind != TEXT("colorRamp"))
				|| !Object->TryGetNumberField(TEXT("domainMin"), Min)
				|| !Object->TryGetNumberField(TEXT("domainMax"), Max)
				|| !Object->TryGetNumberField(TEXT("interpolation"), Interpolation)
				|| !FMath::IsFinite(Interpolation) || Interpolation < 0 || Interpolation > static_cast<int32>(EMixtormatScalarRampInterpolation::BSpline)
				|| Interpolation != FMath::FloorToDouble(Interpolation)
				|| !Object->TryGetArrayField(TEXT("points"), Points))
			{
				return Fail(Error, LOCTEXT("Envelope", "Unsupported schema or incomplete typed ramp preset."));
			}
			Preset.Kind = Kind == TEXT("scalarRamp") ? ERampKind::Scalar : ERampKind::Color;
			Preset.DomainMin = static_cast<float>(Min);
			Preset.DomainMax = static_cast<float>(Max);
			Preset.Scalar.DomainMin = Preset.DomainMin;
			Preset.Scalar.DomainMax = Preset.DomainMax;
			Preset.Scalar.Interpolation = static_cast<EMixtormatScalarRampInterpolation>(static_cast<uint8>(Interpolation));
			Preset.Color.Interpolation = static_cast<EMixtormatColorRampInterpolation>(static_cast<uint8>(Interpolation));
			Preset.Scalar.Points.Reset();
			Preset.Color.Stops.Reset();
			const bool bScalar = Preset.Kind == ERampKind::Scalar;
			if (Points->Num() > (bScalar ? FMixtormatScalarRamp::MaxPoints : FMixtormatColorRamp::MaxStops))
			{
				return Fail(Error, LOCTEXT("Capacity", "Preset exceeds the supported ramp capacity."));
			}
			for (const TSharedPtr<FJsonValue>& Point : *Points)
			{
				const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
				if (!Point.IsValid() || !Point->TryGetArray(Values) || Values->Num() != (bScalar ? 2 : 5))
				{
					return Fail(Error, LOCTEXT("Point", "Invalid typed ramp point."));
				}
				float Components[5] = {};
				for (int32 Index = 0; Index < Values->Num(); ++Index)
				{
					double Number = 0;
					if (!(*Values)[Index].IsValid() || !(*Values)[Index]->TryGetNumber(Number)
						|| !FMath::IsFinite(Number) || !FMath::IsFinite(static_cast<float>(Number)))
					{
						return Fail(Error, LOCTEXT("Number", "Ramp components must be finite numbers."));
					}
					Components[Index] = static_cast<float>(Number);
				}
				if (bScalar) { Preset.Scalar.Points.Emplace(Components[0], Components[1]); }
				else { Preset.Color.Stops.Emplace(Components[0], FLinearColor(Components[1], Components[2], Components[3], Components[4])); }
			}
			if (!Validate(Preset, Error)) { return false; }
			OutPreset = MoveTemp(Preset);
			return true;
		}
	}

	bool ExportRamp(const FString& Filename, const FRampPreset& Preset, FText& OutError)
	{
		OutError = FText::GetEmpty();
		if (!Private::Validate(Preset, OutError)) { return false; }
		if (FPaths::GetExtension(Filename).ToLower() != TEXT("json"))
		{
			return Private::Fail(OutError, LOCTEXT("Extension", "Choose a .json destination."));
		}
		if (IFileManager::Get().FileExists(*Filename))
		{
			return Private::Fail(OutError, LOCTEXT("Exists", "A preset already exists at that path. Choose a new destination."));
		}
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("schema"), Private::SchemaVersion);
		Object->SetStringField(TEXT("id"), Preset.Id.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetStringField(TEXT("name"), Preset.Name);
		const bool bScalar = Preset.Kind == ERampKind::Scalar;
		Object->SetStringField(TEXT("kind"), bScalar ? TEXT("scalarRamp") : TEXT("colorRamp"));
		Object->SetNumberField(TEXT("domainMin"), Preset.DomainMin);
		Object->SetNumberField(TEXT("domainMax"), Preset.DomainMax);
		Object->SetNumberField(TEXT("interpolation"), bScalar ? static_cast<int32>(Preset.Scalar.Interpolation) : static_cast<int32>(Preset.Color.Interpolation));
		TArray<TSharedPtr<FJsonValue>> Points;
		const auto AddPoint = [&Points](std::initializer_list<float> Components)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (float Component : Components) { Values.Add(MakeShared<FJsonValueNumber>(Component)); }
			Points.Add(MakeShared<FJsonValueArray>(Values));
		};
		if (bScalar)
		{
			for (const FMixtormatScalarRampPoint& Point : Preset.Scalar.Points) { AddPoint({Point.X, Point.Y}); }
		}
		else
		{
			for (const FMixtormatColorRampStop& Stop : Preset.Color.Stops)
			{
				AddPoint({Stop.X, Stop.Color.R, Stop.Color.G, Stop.Color.B, Stop.Color.A});
			}
		}
		Object->SetArrayField(TEXT("points"), Points);
		FString Text;
		if (!FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text)))
		{
			return Private::Fail(OutError, LOCTEXT("Encode", "Cannot encode ramp preset JSON."));
		}
		const FString Temporary = Filename + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
		if (!FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			return Private::Fail(OutError, LOCTEXT("Write", "Cannot write preset temporary file."));
		}
		if (!IFileManager::Get().Move(*Filename, *Temporary, false, false))
		{
			IFileManager::Get().Delete(*Temporary);
			return Private::Fail(OutError, LOCTEXT("Publish", "Cannot publish preset JSON; destination may exist or be read-only."));
		}
		return true;
	}

	bool SaveRamp(const FRampPreset& Preset, FText& OutError)
	{
		OutError = FText::GetEmpty();
		if (!Private::Validate(Preset, OutError)) { return false; }
		if (!IFileManager::Get().MakeDirectory(*Private::Directory(), true))
		{
			return Private::Fail(OutError, LOCTEXT("Directory", "Cannot create the project user-preset directory."));
		}
		TArray<FRampPreset> Existing;
		FText ListingError;
		ListRamps(Preset.Kind, Existing, ListingError);
		// Saving over a created preset replaces it (same id/file), never duplicates.
		for (const FRampPreset& Entry : Existing)
		{
			if (Entry.Name.Equals(Preset.Name, ESearchCase::IgnoreCase))
			{
				FRampPreset Replacement = Preset;
				Replacement.Id = Entry.Id;
				const FString Path = FPaths::Combine(Private::Directory(), Entry.Id.ToString(EGuidFormats::Digits) + TEXT(".json"));
				IFileManager::Get().Delete(*Path, false, true);
				return ExportRamp(Path, Replacement, OutError);
			}
		}
		return ExportRamp(FPaths::Combine(Private::Directory(), Preset.Id.ToString(EGuidFormats::Digits) + TEXT(".json")), Preset, OutError);
	}

	bool ImportRamp(const FString& Filename, ERampKind ExpectedKind, FRampPreset& OutPreset, FText& OutError)
	{
		OutError = FText::GetEmpty();
		FRampPreset Preset;
		if (!Private::Read(Filename, Preset, OutError)) { return false; }
		if (Preset.Kind != ExpectedKind)
		{
			return Private::Fail(OutError, LOCTEXT("ImportKind", "This preset has a different ramp type. Import it from the matching ramp control."));
		}
		Preset.Id = FGuid::NewGuid();
		if (!SaveRamp(Preset, OutError)) { return false; }
		OutPreset = MoveTemp(Preset);
		return true;
	}

	bool ListRamps(ERampKind Kind, TArray<FRampPreset>& OutPresets, FText& OutError)
	{
		OutError = FText::GetEmpty();
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(Private::Directory(), TEXT("*.json")), true, false);
		TArray<FRampPreset> Presets;
		TArray<FString> Issues;
		Files.Sort();
		for (const FString& File : Files)
		{
			FRampPreset Preset;
			if (!Private::Read(FPaths::Combine(Private::Directory(), File), Preset, OutError))
			{
				OutError = FText::Format(LOCTEXT("InvalidFile", "{0}: {1}"), FText::FromString(File), OutError);
				Issues.Add(OutError.ToString());
				continue;
			}
			if (Preset.Kind == Kind) { Presets.Add(MoveTemp(Preset)); }
		}
		Presets.Sort([](const FRampPreset& A, const FRampPreset& B) { return A.Name < B.Name; });
		OutPresets = MoveTemp(Presets);
		OutError = FText::FromString(FString::Join(Issues, TEXT("\n")));
		return Issues.IsEmpty();
	}
}

#undef LOCTEXT_NAMESPACE
