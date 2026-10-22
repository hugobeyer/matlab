// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "Services/MixtormatPresetJson.h"
#include "UObject/UnrealType.h"
#include "UObject/NoExportTypes.h"
#include "UObject/SoftObjectPtr.h"
#include "MixtormatScalarRamp.h"
#include "MixtormatColorRamp.h"

namespace MixtormatPresetJson
{
	namespace Private
	{
		bool Included(const FProperty* Property, bool bParametersOnly)
		{
			if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization)) { return false; }
			if (!bParametersOnly) { return true; }
			if (Property->GetFName() == TEXT("ParameterBindings") || Property->GetFName() == TEXT("PublishedSourceOutput")
							|| Property->GetFName() == TEXT("OutputName")) { return false; }
						if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
						{
							if (Enum->GetEnum()->GetFName() == TEXT("EMixtormatColorRampSource")) { return false; }
						}
			if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
			{
				const FName Name = Struct->Struct->GetFName();
				return Name != TEXT("Guid") && Name != TEXT("MixtormatOutputReference")
					&& Name != TEXT("MixtormatParameterAddress") && Name != TEXT("MixtormatBehaviorFieldInput");
			}
			return true;
		}

		bool TransferStruct(const UScriptStruct* Type, void* Data, bool bParametersOnly,
			TSharedPtr<FJsonObject>& Json, bool bRead, int32 Depth, FString& Failure);

		bool TransferValue(FProperty* Property, void* Value, bool bParametersOnly,
			TSharedPtr<FJsonValue>& Json, bool bRead, int32 Depth, FString& Failure)
		{
			if (Depth > MaxDepth || Property->ArrayDim != 1) { return false; }
			if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				FScriptArrayHelper Helper(Array, Value);
				TArray<TSharedPtr<FJsonValue>> Items;
				if (bRead)
				{
					const TArray<TSharedPtr<FJsonValue>>* Input = nullptr;
					if (!Json.IsValid() || !Json->TryGetArray(Input) || Input->Num() > MaxArrayItems) { return false; }
					Items = *Input;
					Helper.Resize(Items.Num());
				}
				else
				{
					if (Helper.Num() > MaxArrayItems) { return false; }
					Items.SetNum(Helper.Num());
				}
				for (int32 Index = 0; Index < Helper.Num(); ++Index)
				{
					if (!TransferValue(Array->Inner, Helper.GetRawPtr(Index), bParametersOnly, Items[Index], bRead, Depth + 1, Failure)) { return false; }
				}
				if (!bRead) { Json = MakeShared<FJsonValueArray>(Items); }
				return true;
			}
			if (FStructProperty* Struct = CastField<FStructProperty>(Property))
			{
				TSharedPtr<FJsonObject> Object;
				if (bRead)
				{
					if (!Json.IsValid() || Json->Type != EJson::Object) { return false; }
					Object = Json->AsObject();
				}
				if (!TransferStruct(Struct->Struct, Value, bParametersOnly, Object, bRead, Depth + 1, Failure)) { return false; }
				if (!bRead) { Json = MakeShared<FJsonValueObject>(Object); }
				return true;
			}
			if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
			{
				bool Result = false;
				if (bRead)
				{
					if (!Json.IsValid() || !Json->TryGetBool(Result)) { return false; }
					Bool->SetPropertyValue(Value, Result);
				}
				else { Json = MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Value)); }
				return true;
			}
			FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
			UEnum* Enum = nullptr;
			if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				Numeric = EnumProperty->GetUnderlyingProperty();
				Enum = EnumProperty->GetEnum();
			}
			else if (FByteProperty* Byte = CastField<FByteProperty>(Property)) { Enum = Byte->Enum; }
			if (Numeric)
			{
				double Number = 0;
				if (bRead)
				{
					if (!Json.IsValid() || !Json->TryGetNumber(Number) || !FMath::IsFinite(Number)) { return false; }
				}
				else { Number = Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(Value) : static_cast<double>(Numeric->GetSignedIntPropertyValue(Value)); }
				if (!FMath::IsFinite(Number)) { return false; }
				if (Numeric->IsInteger())
				{
					if (Number != FMath::FloorToDouble(Number) || Number < MIN_int32 || Number > MAX_uint32) { return false; }
					if (Enum && (!Enum->IsValidEnumValue(static_cast<int64>(Number))
						|| Enum->GetNameStringByValue(static_cast<int64>(Number)).EndsWith(TEXT("_MAX")))) { return false; }

					if (bRead)
					{
						Numeric->SetIntPropertyValue(Value, static_cast<uint64>(static_cast<int64>(Number)));
						if (static_cast<double>(Numeric->GetSignedIntPropertyValue(Value)) != Number) { return false; }
					}
				}
				else
				{
					if (!FMath::IsFinite(static_cast<float>(Number))) { return false; }
					if (bRead) { Numeric->SetFloatingPointPropertyValue(Value, Number); }
				}
				if (!bRead) { Json = MakeShared<FJsonValueNumber>(Number); }
				return true;
			}
			FString Text;
			if (bRead && (!Json.IsValid() || !Json->TryGetString(Text) || Text.Len() > MaxArrayItems)) { return false; }
			if (FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Property))
			{
				if (bRead)
				{
					const FSoftObjectPath Path(Text);
					if (!Text.IsEmpty() && (!Path.IsValid() || !Text.StartsWith(TEXT("/")))) { return false; }
					Soft->SetPropertyValue(Value, FSoftObjectPtr(Path));
				}
				else { Text = Soft->GetPropertyValue(Value).ToSoftObjectPath().ToString(); }
			}
			else if (FStrProperty* String = CastField<FStrProperty>(Property))
			{
				if (bRead) { String->SetPropertyValue(Value, Text); }
				else { Text = String->GetPropertyValue(Value); }
			}
			else if (FNameProperty* Name = CastField<FNameProperty>(Property))
			{
				if (bRead) { Name->SetPropertyValue(Value, FName(*Text)); }
				else { Text = Name->GetPropertyValue(Value).ToString(); }
			}
			else if (FTextProperty* Label = CastField<FTextProperty>(Property))
			{
				// Materialize displayed labels; never import executable/localization history text.
				if (bRead) { Label->SetPropertyValue(Value, FText::FromString(Text)); }
				else { Text = Label->GetPropertyValue(Value).ToString(); }
			}
			else { return false; }
			if (!bRead) { Json = MakeShared<FJsonValueString>(Text); }
			return true;
		}

		bool TransferStruct(const UScriptStruct* Type, void* Data, bool bParametersOnly,
			TSharedPtr<FJsonObject>& Json, bool bRead, int32 Depth, FString& Failure)
		{
			if (!Type || Depth > MaxDepth) { return false; }
			if (Type == TBaseStructure<FGuid>::Get())
			{
				if (bRead)
				{
					FString Text;
					FGuid Guid;
					if (!Json.IsValid() || Json->Values.Num() != 2
						|| !Json->TryGetStringField(TEXT("$type"), Text) || Text != Type->GetPathName()
						|| !Json->TryGetStringField(TEXT("value"), Text) || !FGuid::Parse(Text, Guid)) { return false; }
					*static_cast<FGuid*>(Data) = Guid;
				}
				else
				{
					Json = MakeShared<FJsonObject>();
					Json->SetStringField(TEXT("$type"), Type->GetPathName());
					Json->SetStringField(TEXT("value"), static_cast<FGuid*>(Data)->ToString(EGuidFormats::DigitsWithHyphens));
				}
				return true;
			}
			if (!bRead) { Json = MakeShared<FJsonObject>(); Json->SetStringField(TEXT("$type"), Type->GetPathName()); }
			FString TypeName;
			if (!Json.IsValid() || !Json->TryGetStringField(TEXT("$type"), TypeName) || TypeName != Type->GetPathName()) { return false; }
			int32 Expected = 1;
			for (TFieldIterator<FProperty> It(Type); It; ++It)
			{
				FProperty* Property = *It;
				if (!Included(Property, bParametersOnly)) { continue; }
				++Expected;
				TSharedPtr<FJsonValue> Value = bRead ? Json->TryGetField(Property->GetName()) : nullptr;
				if (!TransferValue(Property, Property->ContainerPtrToValuePtr<void>(Data), bParametersOnly, Value, bRead, Depth + 1, Failure))
				{
					Failure = Property->GetName() + (Failure.IsEmpty() ? FString() : TEXT(".") + Failure);
					return false;
				}
				if (!bRead) { Json->SetField(Property->GetName(), Value); }
			}
			if (Json->Values.Num() != Expected) { return false; }
			if (Type == FMixtormatScalarRamp::StaticStruct())
			{
				const FMixtormatScalarRamp& Ramp = *static_cast<FMixtormatScalarRamp*>(Data);
				FMixtormatScalarRamp Sanitized = Ramp;
				Sanitized.Sanitize();
				if (Sanitized.Points.Num() != Ramp.Points.Num() || Sanitized.DomainMin != Ramp.DomainMin
					|| Sanitized.DomainMax != Ramp.DomainMax || Sanitized.Interpolation != Ramp.Interpolation) { return false; }
				for (int32 Index = 0; Index < Ramp.Points.Num(); ++Index)
				{
					if (Sanitized.Points[Index].X != Ramp.Points[Index].X || Sanitized.Points[Index].Y != Ramp.Points[Index].Y) { return false; }
				}
			}
			if (Type == FMixtormatColorRamp::StaticStruct())
			{
				const FMixtormatColorRamp& Ramp = *static_cast<FMixtormatColorRamp*>(Data);
				FMixtormatColorRamp Sanitized = Ramp;
				Sanitized.Sanitize();
				if (Sanitized.Stops.Num() != Ramp.Stops.Num() || Sanitized.Interpolation != Ramp.Interpolation) { return false; }
				for (int32 Index = 0; Index < Ramp.Stops.Num(); ++Index)
				{
					if (Sanitized.Stops[Index].X != Ramp.Stops[Index].X || Sanitized.Stops[Index].Color != Ramp.Stops[Index].Color) { return false; }
				}
			}
			return true;
		}

		void VisitValue(FProperty* Property, void* Value,
			TFunctionRef<void(const UScriptStruct*, void*)> OnStruct,
			TFunctionRef<void(const FSoftObjectPath&)> OnAsset)
		{
			if (FStructProperty* Struct = CastField<FStructProperty>(Property)) { Visit(Struct->Struct, Value, OnStruct, OnAsset); }
			else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				FScriptArrayHelper Helper(Array, Value);
				for (int32 Index = 0; Index < Helper.Num(); ++Index) { VisitValue(Array->Inner, Helper.GetRawPtr(Index), OnStruct, OnAsset); }
			}
			else if (FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Property))
			{
				const FSoftObjectPath Path = Soft->GetPropertyValue(Value).ToSoftObjectPath();
				if (!Path.IsNull()) { OnAsset(Path); }
			}
		}
	}

	bool Encode(const UScriptStruct* Type, const void* Data, bool bParametersOnly, TSharedPtr<FJsonObject>& OutJson, FText& Error)
	{
		FString Failure;
		if (!Private::TransferStruct(Type, const_cast<void*>(Data), bParametersOnly, OutJson, false, 0, Failure))
		{
			Error = FText::Format(NSLOCTEXT("Mixtormat", "PresetEncodeField", "Unsupported or invalid preset field: {0}"), FText::FromString(Failure));
			return false;
		}
		return true;
	}

	bool Decode(const UScriptStruct* Type, void* Data, bool bParametersOnly, const TSharedPtr<FJsonObject>& Json, FText& Error)
	{
		FString Failure;
		TSharedPtr<FJsonObject> Input = Json;
		if (!Private::TransferStruct(Type, Data, bParametersOnly, Input, true, 0, Failure))
		{
			Error = FText::Format(NSLOCTEXT("Mixtormat", "PresetDecodeField", "Wrong type, missing, unknown or invalid preset field: {0}"), FText::FromString(Failure));
			return false;
		}
		return true;
	}

	bool ValidateAssetClasses(const UScriptStruct* Type, const void* Data, FText& Error)
	{
		bool bValid = true;
		Visit(Type, const_cast<void*>(Data), [&bValid, &Error](const UScriptStruct* Struct, void* StructData)
		{
			TFunction<void(FProperty*, void*)> Check;
			Check = [&Check, &bValid, &Error, Struct](FProperty* Property, void* Value)
			{
				if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
				{
					FScriptArrayHelper Helper(Array, Value);
					for (int32 Index = 0; Index < Helper.Num(); ++Index) { Check(Array->Inner, Helper.GetRawPtr(Index)); }
					return;
				}
				const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Property);
				if (!Soft) { return; } // Nested structs are handled by Visit.
				const FSoftObjectPath Path = Soft->GetPropertyValue(Value).ToSoftObjectPath();
				if (Path.IsNull()) { return; }
				UObject* Asset = Path.TryLoad();
				if (!Asset || !Asset->IsA(Soft->PropertyClass))
				{
					bValid = false;
					Error = FText::Format(NSLOCTEXT("Mixtormat", "PresetAssetType", "Missing or wrong-class asset at {0}.{1}: {2}"),
						FText::FromString(Struct->GetName()), FText::FromName(Soft->GetFName()), FText::FromString(Path.ToString()));
				}
			};
			for (TFieldIterator<FProperty> It(Struct); It; ++It) { Check(*It, It->ContainerPtrToValuePtr<void>(StructData)); }
		}, [](const FSoftObjectPath&) {});
		return bValid;
	}

	void Visit(const UScriptStruct* Type, void* Data, TFunctionRef<void(const UScriptStruct*, void*)> OnStruct,
		TFunctionRef<void(const FSoftObjectPath&)> OnAsset)
	{
		OnStruct(Type, Data);
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			if (Private::Included(*It, false)) { Private::VisitValue(*It, It->ContainerPtrToValuePtr<void>(Data), OnStruct, OnAsset); }
		}
	}
}
