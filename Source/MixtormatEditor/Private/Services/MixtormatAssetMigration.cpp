// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Services/MixtormatAssetMigration.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "MixtormatSurface.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Services/MixtormatPaths.h"

DEFINE_LOG_CATEGORY_STATIC(LogMixtormatAssetMigration, Log, All);

namespace MixtormatAssetMigration
{
	struct FRenameSpec
	{
		FSoftObjectPath Source;
		FSoftObjectPath Target;
		bool bSurface = false;
	};

	FSoftObjectPath MakeObjectPath(
		const FString& PackagePath,
		const FString& AssetName)
	{
		return FSoftObjectPath(FString::Printf(
			TEXT("%s/%s.%s"),
			*PackagePath,
			*AssetName,
			*AssetName));
	}

	void AddFixedRename(
		TArray<FRenameSpec>& Specs,
		const FString& PackagePath,
		const TCHAR* SourceName,
		const TCHAR* TargetName)
	{
		Specs.Add({
			MakeObjectPath(PackagePath, SourceName),
			MakeObjectPath(PackagePath, TargetName),
			false});
	}

	TArray<FRenameSpec> BuildFixedRenameSpecs()
	{
		TArray<FRenameSpec> Specs;
		const FString MaterialsRoot = FMixtormatPaths::MaterialsRoot();
		const FString FunctionsRoot = MaterialsRoot / TEXT("Functions");
		const FString MaterialTexturesRoot = MaterialsRoot / TEXT("Textures");
		const FString MeshesRoot = FMixtormatPaths::MeshesRoot();
		const FString EffectsRoot = FMixtormatPaths::EffectsRoot();

		AddFixedRename(Specs, MaterialsRoot,
			TEXT("M_MaterialLab_Substrate"), TEXT("M_Mixtormat_Substrate"));
		AddFixedRename(Specs, MaterialsRoot,
			TEXT("MI_ML_Studio_Floor"), TEXT("MI_Mixtormat_StudioFloor"));

		AddFixedRename(Specs, FunctionsRoot,
			TEXT("M_MaterialLab_Normal_Intensity"), TEXT("M_Mixtormat_NormalIntensity"));
		AddFixedRename(Specs, FunctionsRoot,
			TEXT("M_MaterialLab_Roughness_Bias"), TEXT("M_Mixtormat_RoughnessBias"));
		AddFixedRename(Specs, FunctionsRoot,
			TEXT("MF_MaterialLab_LayerBlend"), TEXT("MF_Mixtormat_LayerBlend"));
		AddFixedRename(Specs, FunctionsRoot,
			TEXT("MF_MaterialLab_Surface"), TEXT("MF_Mixtormat_Surface"));

		AddFixedRename(Specs, MaterialTexturesRoot,
			TEXT("T_ML_Studio_Floor_BC"), TEXT("T_Mixtormat_StudioFloor_BC"));
		AddFixedRename(Specs, MaterialTexturesRoot,
			TEXT("T_ML_Studio_Floor_H"), TEXT("T_Mixtormat_StudioFloor_H"));
		AddFixedRename(Specs, MaterialTexturesRoot,
			TEXT("T_ML_Studio_Floor_N"), TEXT("T_Mixtormat_StudioFloor_N"));
		AddFixedRename(Specs, MaterialTexturesRoot,
			TEXT("T_ML_Studio_Floor_RAM"), TEXT("T_Mixtormat_StudioFloor_RAM"));

		AddFixedRename(Specs, MeshesRoot,
			TEXT("SM_MaterialLab_Cube"), TEXT("SM_Mixtormat_Cube"));
		AddFixedRename(Specs, MeshesRoot,
			TEXT("SM_MaterialLab_Plane"), TEXT("SM_Mixtormat_Plane"));
		AddFixedRename(Specs, MeshesRoot,
			TEXT("SM_MaterialLab_Sphere"), TEXT("SM_Mixtormat_Sphere"));

		AddFixedRename(Specs, EffectsRoot / TEXT("Peeling"),
			TEXT("MLFX_Peeling_Standard_01"), TEXT("DA_Peeling_Standard_01"));
		AddFixedRename(Specs, EffectsRoot / TEXT("Stain"),
			TEXT("MLFX_Stain"), TEXT("DA_Stain"));

		return Specs;
	}

	void AddSurfaceRenameSpecs(
		IAssetRegistry& AssetRegistry,
		TArray<FRenameSpec>& Specs,
		FMixtormatAssetMigrationReport& Report)
	{
		FARFilter Filter;
		Filter.ClassPaths.Add(UMixtormatSurface::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName(*FMixtormatPaths::SurfacesRoot()));
		Filter.bRecursiveClasses = true;
		Filter.bRecursivePaths = true;

		TArray<FAssetData> SurfaceAssets;
		AssetRegistry.GetAssets(Filter, SurfaceAssets);
		Report.SurfaceAssetCount = SurfaceAssets.Num();
		if (SurfaceAssets.IsEmpty())
		{
			Report.Errors.Add(FString::Printf(
				TEXT("No surface assets were found under %s."),
				*FMixtormatPaths::SurfacesRoot()));
			return;
		}

		for (const FAssetData& Asset : SurfaceAssets)
		{
			const FString SourceName = Asset.AssetName.ToString();
			if (SourceName.StartsWith(TEXT("DA_")))
			{
				++Report.AlreadyCanonicalCount;
				continue;
			}
			if (!SourceName.StartsWith(TEXT("ML_")))
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("Surface has a non-canonical name and was not planned: %s"),
					*Asset.GetObjectPathString()));
				continue;
			}

			const FString TargetName = TEXT("DA_") + SourceName.RightChop(3);
			Specs.Add({
				Asset.GetSoftObjectPath(),
				MakeObjectPath(Asset.PackagePath.ToString(), TargetName),
				true});
		}
	}

	void ValidateSpecs(
		IAssetRegistry& AssetRegistry,
		const TArray<FRenameSpec>& Specs,
		TArray<FRenameSpec>& PendingSpecs,
		FMixtormatAssetMigrationReport& Report)
	{
		for (const FRenameSpec& Spec : Specs)
		{
			const FAssetData SourceAsset = AssetRegistry.GetAssetByObjectPath(Spec.Source);
			const FAssetData TargetAsset = AssetRegistry.GetAssetByObjectPath(Spec.Target);

			if (!SourceAsset.IsValid())
			{
				if (TargetAsset.IsValid())
				{
					++Report.AlreadyCanonicalCount;
				}
				else if (!Spec.bSurface)
				{
					Report.Warnings.Add(FString::Printf(
						TEXT("Known support asset was not found: %s"),
						*Spec.Source.ToString()));
				}
				continue;
			}

			if (TargetAsset.IsValid())
			{
				Report.Errors.Add(FString::Printf(
					TEXT("Rename target already exists: %s"),
					*Spec.Target.ToString()));
				continue;
			}

			PendingSpecs.Add(Spec);
			if (Spec.bSurface)
			{
				++Report.PlannedSurfaceRenameCount;
			}
			else
			{
				++Report.PlannedSupportRenameCount;
			}
			Report.RenameLines.Add(FString::Printf(
				TEXT("%s -> %s"),
				*Spec.Source.ToString(),
				*Spec.Target.ToString()));
		}
	}

	bool ApplySpecs(
		IAssetRegistry& AssetRegistry,
		const TArray<FRenameSpec>& PendingSpecs,
		FMixtormatAssetMigrationReport& Report)
	{
		TArray<FAssetRenameData> RenameData;
		RenameData.Reserve(PendingSpecs.Num());
		for (const FRenameSpec& Spec : PendingSpecs)
		{
			const FAssetData SourceAsset = AssetRegistry.GetAssetByObjectPath(Spec.Source);
			UObject* Asset = SourceAsset.GetAsset();
			if (!Asset)
			{
				Report.Errors.Add(FString::Printf(
					TEXT("Failed to load rename source: %s"),
					*Spec.Source.ToString()));
				continue;
			}

			const FString TargetPackageName = Spec.Target.GetLongPackageName();
			RenameData.Emplace(
				Asset,
				FPackageName::GetLongPackagePath(TargetPackageName),
				FPackageName::GetLongPackageAssetName(TargetPackageName));
		}

		if (RenameData.Num() != PendingSpecs.Num() || !Report.Errors.IsEmpty())
		{
			return false;
		}

		IAssetTools& AssetTools =
			FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		if (!AssetTools.RenameAssets(RenameData))
		{
			Report.Errors.Add(TEXT("AssetTools reported that the rename batch failed."));
			return false;
		}

		for (const FRenameSpec& Spec : PendingSpecs)
		{
			if (AssetRegistry.GetAssetByObjectPath(Spec.Target).IsValid())
			{
				++Report.VerifiedTargetCount;
			}
			else
			{
				Report.Errors.Add(FString::Printf(
					TEXT("Renamed target was not found after the batch: %s"),
					*Spec.Target.ToString()));
			}
		}

		return Report.Errors.IsEmpty()
			&& Report.VerifiedTargetCount == PendingSpecs.Num();
	}
}

bool FMixtormatAssetMigrationReport::CanApply() const
{
	return Errors.IsEmpty()
		&& PlannedSurfaceRenameCount + PlannedSupportRenameCount > 0;
}

FString FMixtormatAssetMigrationReport::ToString() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(
		TEXT("Mixtormat asset migration %s"),
		bApplyRequested ? TEXT("APPLY") : TEXT("DRY RUN")));
	Lines.Add(FString::Printf(
		TEXT("Surfaces discovered: %d"),
		SurfaceAssetCount));
	Lines.Add(FString::Printf(
		TEXT("Planned renames: %d surfaces, %d support assets"),
		PlannedSurfaceRenameCount,
		PlannedSupportRenameCount));
	Lines.Add(FString::Printf(
		TEXT("Already canonical: %d"),
		AlreadyCanonicalCount));

	if (bApplyRequested)
	{
		Lines.Add(FString::Printf(
			TEXT("Verified renamed targets: %d"),
			VerifiedTargetCount));
		Lines.Add(bRenameSucceeded
			? TEXT("Rename batch succeeded. Redirectors were retained for a separate approved cleanup.")
			: TEXT("Rename batch did not complete successfully."));
	}

	for (const FString& RenameLine : RenameLines)
	{
		Lines.Add(TEXT("RENAME: ") + RenameLine);
	}
	for (const FString& Warning : Warnings)
	{
		Lines.Add(TEXT("WARNING: ") + Warning);
	}
	for (const FString& Error : Errors)
	{
		Lines.Add(TEXT("ERROR: ") + Error);
	}

	if (!bApplyRequested && CanApply())
	{
		Lines.Add(TEXT("Dry run is valid. Use: Mixtormat.MigrateAssets Apply"));
	}
	return FString::Join(Lines, TEXT("\n"));
}

FMixtormatAssetMigrationReport FMixtormatAssetMigration::Run(const bool bApply)
{
	using namespace MixtormatAssetMigration;
	FMixtormatAssetMigrationReport Report;
	Report.bApplyRequested = bApply;

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FRenameSpec> Specs = BuildFixedRenameSpecs();
	AddSurfaceRenameSpecs(AssetRegistry, Specs, Report);

	TArray<FRenameSpec> PendingSpecs;
	ValidateSpecs(AssetRegistry, Specs, PendingSpecs, Report);
	if (bApply)
	{
		if (!Report.Errors.IsEmpty())
		{
			Report.Errors.Add(TEXT("Apply was blocked because the dry-run plan is not valid."));
		}
		else if (PendingSpecs.IsEmpty())
		{
			Report.bRenameSucceeded = true;
		}
		else
		{
			Report.bRenameSucceeded = ApplySpecs(AssetRegistry, PendingSpecs, Report);
		}
	}

	const FString ReportText = Report.ToString();
	UE_LOG(LogMixtormatAssetMigration, Display, TEXT("%s"), *ReportText);
	return Report;
}
