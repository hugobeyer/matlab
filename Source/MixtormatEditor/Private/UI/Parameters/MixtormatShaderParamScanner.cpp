// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Parameters/MixtormatShaderParamScanner.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Services/MixtormatPaths.h"

namespace
{
	bool ParseFloat(const FString& Text, float& OutValue)
	{
		if (Text.IsEmpty())
		{
			return false;
		}
		bool bDigit = false;
		bool bDot = false;
		bool bExponent = false;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const TCHAR Character = Text[Index];
			if (FChar::IsDigit(Character))
			{
				bDigit = true;
				continue;
			}
			if (Character == TEXT('.') && !bDot && !bExponent)
			{
				bDot = true;
				continue;
			}
			if ((Character == TEXT('e') || Character == TEXT('E')) && !bExponent && bDigit)
			{
				bExponent = true;
				bDigit = false;
				continue;
			}
			if ((Character == TEXT('+') || Character == TEXT('-'))
				&& (Index == 0 || Text[Index - 1] == TEXT('e') || Text[Index - 1] == TEXT('E')))
			{
				continue;
			}
			return false;
		}
		if (!bDigit)
		{
			return false;
		}
		OutValue = FCString::Atof(*Text);
		return FMath::IsFinite(OutValue);
	}

	FString UniformFromDeclaration(const FString& Line)
	{
		FString Code = Line;
		Code.RemoveFromEnd(TEXT(";"));
		TArray<FString> Parts;
		Code.ParseIntoArrayWS(Parts);
		if (Parts.Num() < 2)
		{
			return FString();
		}
		FString Uniform = Parts.Last();
		Uniform.RemoveFromStart(TEXT("*") );
		const int32 ArrayStart = Uniform.Find(TEXT("["));
		if (ArrayStart != INDEX_NONE)
		{
			Uniform.LeftInline(ArrayStart);
		}
		return Uniform;
	}

	bool IsDeclaration(const FString& Line)
	{
		return Line.EndsWith(TEXT(";"))
			&& !Line.StartsWith(TEXT("//"))
			&& !Line.Contains(TEXT("("))
			&& !Line.Contains(TEXT("="));
	}

	bool ParseTag(
		const FString& Text,
		FMixtormatShaderParamTag& OutTag,
		FString& OutError)
	{
		TArray<FString> Tokens;
		Text.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() < 2 || Tokens[0] != TEXT("@param"))
		{
			OutError = TEXT("expected '@param <name> [facts...]'");
			return false;
		}

		OutTag.Parameter = FName(Tokens[1]);
		for (int32 Index = 2; Index < Tokens.Num(); ++Index)
		{
			const FString& Token = Tokens[Index];
			if (Token == TEXT("saturates"))
			{
				OutTag.bSaturates = true;
				continue;
			}
			if (Token == TEXT("divisor"))
			{
				OutTag.bDivisor = true;
				continue;
			}

			FString Key;
			FString Value;
			if (!Token.Split(TEXT("="), &Key, &Value))
			{
				OutError = FString::Printf(TEXT("unknown fact '%s'"), *Token);
				return false;
			}

			float Number = 0.0f;
			if (!ParseFloat(Value, Number))
			{
				OutError = FString::Printf(TEXT("invalid numeric fact '%s'"), *Token);
				return false;
			}
			if (Key == TEXT("hardmin"))
			{
				OutTag.HardMin = Number;
			}
			else if (Key == TEXT("hardmax"))
			{
				OutTag.HardMax = Number;
			}
			else if (Key == TEXT("normalize"))
			{
				OutTag.Normalize = Number;
			}
			else
			{
				OutError = FString::Printf(TEXT("unknown fact '%s'"), *Key);
				return false;
			}
		}
		return true;
	}
}

namespace MixtormatShaderParamScanner
{
	bool Scan(TArray<FMixtormatShaderParamTag>& OutTags, TArray<FString>& OutErrors)
	{
		OutTags.Reset();
		OutErrors.Reset();

		const FString PluginRoot = FMixtormatPaths::PluginBaseDir();
		const FString Root = FPaths::Combine(PluginRoot, TEXT("Shaders/Private"));
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.usf"), true, false);
		for (const FString& File : Files)
		{
			FString Source;
			if (!FFileHelper::LoadFileToString(Source, *File))
			{
				OutErrors.Add(FString::Printf(TEXT("Could not read shader '%s'"), *File));
				continue;
			}

			TArray<FString> Lines;
			Source.ParseIntoArrayLines(Lines, false);
			for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
			{
				const FString Comment = Lines[LineIndex].TrimStartAndEnd();
				if (!Comment.StartsWith(TEXT("// @param")))
				{
					continue;
				}

				FMixtormatShaderParamTag Tag;
				const FString RelativeFile = File.RightChop(PluginRoot.Len() + 1);
				Tag.ShaderFile = RelativeFile;
				Tag.Owner = RelativeFile.EndsWith(TEXT("MixtormatComposite.usf"))
					? EMixtormatParameterOwnerType::Layer
					: EMixtormatParameterOwnerType::Effect;

				FString Error;
				if (!ParseTag(Comment.RightChop(3).TrimStart(), Tag, Error))
				{
					OutErrors.Add(FString::Printf(TEXT("%s:%d: %s"), *RelativeFile, LineIndex + 1, *Error));
					continue;
				}

				FString UniformName;
				for (int32 Next = LineIndex + 1; Next < Lines.Num() && UniformName.IsEmpty(); ++Next)
				{
					const FString Candidate = Lines[Next].TrimStartAndEnd();
					if (Candidate.IsEmpty() || Candidate.StartsWith(TEXT("//")))
					{
						continue;
					}
					if (IsDeclaration(Candidate))
					{
						UniformName = UniformFromDeclaration(Candidate);
					}
					break;
				}
				for (int32 Previous = LineIndex - 1; Previous >= 0 && UniformName.IsEmpty(); --Previous)
				{
					const FString Candidate = Lines[Previous].TrimStartAndEnd();
					if (!Candidate.IsEmpty() && !Candidate.StartsWith(TEXT("//")))
					{
						if (IsDeclaration(Candidate))
						{
							UniformName = UniformFromDeclaration(Candidate);
						}
						break;
					}
				}
				if (UniformName.IsEmpty())
				{
					OutErrors.Add(FString::Printf(
						TEXT("%s:%d: tag has no adjacent uniform declaration"), *RelativeFile, LineIndex + 1));
					continue;
				}

				Tag.UniformName = UniformName;
				OutTags.Add(MoveTemp(Tag));
			}
		}
		return OutErrors.IsEmpty();
	}
}
