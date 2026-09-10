#include "Misc/AutomationTest.h"

#include "Services/MixtormatPaths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLibraryPathContractTest,
	"Mixtormat.Import.LibraryPathContract",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FMixtormatLibraryPathContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString UserRoot = FMixtormatPaths::ProjectLibraryRoot();
	TestEqual(TEXT("Built-in content uses the plugin mount"), FMixtormatPaths::PluginContentRoot(), FString(TEXT("/Mixtormat")));
	TestEqual(TEXT("User content uses the project library"), UserRoot, FString(TEXT("/Game/Mixtormat/Library")));
	TestTrue(TEXT("Built-in and user roots are distinct"), FMixtormatPaths::PluginContentRoot() != UserRoot);

	const auto TestUserPath = [this, &UserRoot](const TCHAR* Label, const FString& Path)
	{
		TestTrue(Label, Path.StartsWith(UserRoot + TEXT("/"), ESearchCase::CaseSensitive));
	};
	TestUserPath(TEXT("User surfaces stay under the project library"), FMixtormatPaths::ProjectLibrarySurfaceFamilyRoot(TEXT("Stone")));
	TestUserPath(TEXT("User textures stay under the project library"), FMixtormatPaths::ProjectLibraryRawTextureFamilyRoot(TEXT("Stone")));
	TestUserPath(TEXT("User thumbnails stay under the project library"), FMixtormatPaths::ProjectLibrarySurfaceThumbnailFamilyRoot(TEXT("Stone")));
	TestUserPath(TEXT("User preview materials stay under the project library"), FMixtormatPaths::ProjectLibraryMaterialInstanceFamilyRoot(TEXT("Stone")));
	TestUserPath(TEXT("User masks stay under the project library"), FMixtormatPaths::ProjectLibraryMasksRoot());

	return true;
}

#endif
