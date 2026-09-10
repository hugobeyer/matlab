I audited the current main using repository file reads only. No files were edited; no Git commands, terminal, builds, or Unreal were run.

The target architecture is a fairly contained change. The real blockers are: startup rebuild, customer Refresh calling the developer importer, customer imports targeting /Mixtormat, plugin-only save validation, and single-root registry discovery.

1. Affected files and exact change map
File	Function / lines	Required change
Source/MixtormatEditor/Private/Widgets/SMixtormat.cpp	Construct() 13–25	Remove raw-library import at 17–20.
Source/MixtormatEditor/Private/Widgets/SMixtormat_Library.cpp	RefreshSurfaceList() 9–15	Already suitable as customer Refresh entry point.
same	RebuildSurfaceList() 179–220	Replace raw-source empty-library message at 208–219.
same	BuildLibraryPage() 261–328	Folder import button 294–307 remains; change current reimport button 310–325 into normal Refresh.
Source/MixtormatEditor/Private/Widgets/SMixtormat_Document.cpp	ImportSurfaces() 10–20	No structural change required if ImportFromDialog() becomes user-project import.
same	ReimportShippedLibrary() 22–37	Must no longer be reachable from customer UI. May remain unused/internal or be removed.
Source/MixtormatEditor/Private/Widgets/SMixtormat.h	declarations 63–65	Only needs editing if ReimportShippedLibrary() wrapper is removed.
Source/MixtormatEditor/Private/Services/MixtormatPaths.h	path declarations 18–34, project paths 43–44	Add /Game/Mixtormat/Library/... helpers.
Source/MixtormatEditor/Private/Services/MixtormatPaths.cpp	built-in roots 49–173	Keep built-ins; add corresponding user-library roots.
Source/MixtormatEditor/Private/Services/MixtormatSurfaceImporter.cpp	157–309, 348–445, 490–514, 631–650, 695–748, 778–1069, 1071–1335	Main implementation work: destination ownership, saves, user paths, dev-only raw rebuild.
Source/MixtormatEditor/Private/Services/MixtormatSurfaceImporter.h	public API 25–34	Can remain unchanged with a cpp-internal import-target helper; otherwise update ImportDirectory signature.
Source/MixtormatEditor/Private/Services/MixtormatThumbnailRenderer.h	thumbnail APIs 29–33	Surface thumbnail creation needs destination-aware path input.
Source/MixtormatEditor/Private/Services/MixtormatThumbnailRenderer.cpp	generic creator 156–250; mask 582–603; surface 605–626	Generic helper is already destination-based; public wrappers hardcode plugin thumbnail roots.
Source/MixtormatEditor/Private/Services/MixtormatRegistry.cpp	thumbnail helper 13–22; GetSurfaces() 25–82; GetMasks() 84–153	Add built-in + user package roots; fix fallback family/root assumptions.

The actual plugin-window startup path goes through SpawnMixtormatTab() at MixtormatEditorModule.cpp:100–111, but that file itself does not require a change.

2. Package-path construction and /Mixtormat enforcement

Central construction is in:

Source/MixtormatEditor/Private/Services/MixtormatPaths.cpp

55 FString FMixtormatPaths::PluginContentRoot()
56 {
57     return TEXT("/") + CurrentPluginName.ToString();
58 }

Since CurrentPluginName == "Mixtormat", that is /Mixtormat.

Current built-in path constructors are:

Lines	Function	Result
11–14	PackageChild()	generic package child helper
16–19	ObjectPath()	generic object-path helper
49–53	SourceTexturesDir()	physical plugin Content/Textures
55–58	PluginContentRoot()	/Mixtormat
60–63	SurfacesRoot()	/Mixtormat/Surfaces
65–68	SurfaceFamilyRoot()	/Mixtormat/Surfaces/<Family>
70–73	TexturesRoot()	/Mixtormat/Textures
75–78	RawTextureFamilyRoot()	/Mixtormat/Textures/<Family>/Raw
80–98	thumbnail helpers	/Mixtormat/Thumbnails/...
100–103	MasksRoot()	/Mixtormat/Masks
105–113	Normal helpers	/Mixtormat/Normals/...
115–118	EffectsRoot()	/Mixtormat/Effects
120–123	LightingRoot()	/Mixtormat/Lighting
125–133	Material/instance helpers	/Mixtormat/Materials/...
135–138	MeshesRoot()	/Mixtormat/Meshes
140–168	object-path helpers	built-in materials/meshes
170–173	ProjectMaterialsRoot()	already /Game/Mixtormat/Materials

The built-in functions should remain. They are correct for shipped read-only data.

Add a parallel project library family, minimally along these lines:

ProjectLibraryRoot()
    /Game/Mixtormat/Library

ProjectLibrarySurfacesRoot()
ProjectLibrarySurfaceFamilyRoot(Family)

ProjectLibraryTexturesRoot()
ProjectLibraryRawTextureFamilyRoot(Family)

ProjectLibraryMasksRoot()

ProjectLibraryThumbnailsRoot()
ProjectLibrarySurfaceThumbnailFamilyRoot(Family)
ProjectLibraryMaskThumbnailsRoot()

ProjectLibraryMaterialInstanceFamilyRoot(Family)

That covers every asset the current surface import actually creates.

3. Save/import helpers assuming plugin ownership

The strongest hardcoded constraint is MixtormatSurfaceImporter.cpp:157–193:

bool IsPluginAssetPath(const FString& PackagePath)
{
    return PackagePath.StartsWith(
        FMixtormatPaths::PluginContentRoot() + TEXT("/"),
        ESearchCase::CaseSensitive);
}

bool SavePluginAsset(...)
{
    ...
    if (!IsPluginAssetPath(PackageName))
        return false;

    ...
    AssetSubsystem->SaveLoadedAsset(&Asset, false);
}

This explicitly prevents /Game/Mixtormat/Library/... from being saved.

Affected helpers:

Function	Lines	Problem
IsPluginAssetPath()	157–163	Only accepts /Mixtormat/....
SavePluginAsset()	165–193	Rejects all project-owned assets.
IsPluginSourceFile()	201–207	Explicitly recognizes only developer plugin source PNGs. Keep for dev rebuild.
HasPluginSourceChanged()	209–230	Generic enough; currently only used behind plugin-source check.
ImportTexture()	232–309	Rejects non-plugin destination at 239–245, and saves through SavePluginAsset.
CreateOrUpdateGeneratedRAMH()	348–445	Rejects non-plugin destination and saves through SavePluginAsset.
LoadReusableDerivedRAMH()	490–514	Destination-parametric already; no root assumption itself.
BuildDerivedRAMH()	516–592	Destination-parametric; downstream generated-RAMH helper is the blocker.
CreateOrLoadSurface()	631–650	Destination-parametric already.
CreateOrUpdatePreviewMaterial()	695–748	Hardcodes plugin material-instance path at 708.

ImportTexture() currently contains:

if (!IsPluginAssetPath(DestinationPath))
{
    Result.Errors.Add(...);
    return nullptr;
}

and then uses SavePluginAsset() for both imported and updated textures.

The generated RAMH path has the same restriction.

The smallest fix is not to remove validation. Replace the concept of “must belong to plugin” with “must belong to the selected import ownership root”:

Developer shipped rebuild:
    allowed root = /Mixtormat/

Customer import:
    allowed root = /Game/Mixtormat/Library/

That preserves the existing safety check while allowing user-owned assets.

4. Startup importer calls

There is one confirmed normal widget-startup import path.

SMixtormat.cpp:13–25:

if (FMixtormatRegistry::GetSurfaces().IsEmpty()
    || FMixtormatRegistry::GetMasks().IsEmpty())
{
    FMixtormatSurfaceImporter::ImportDefaultLibrary();
}

Remove lines 17–20 completely. Startup should simply build the UI from whatever Unreal assets exist.

The module itself does not import. The actual graph is:

FMixtormatEditorModule::SpawnMixtormatTab()       100–111
    ↓
SNew(SMixtormat)
    ↓
SMixtormat::Construct()                            13–25
    ↓
GetSurfaces() / GetMasks()
    ↓
CURRENT: ImportDefaultLibrary() if either empty    17–20

So MixtormatEditorModule.cpp is call-graph context only; no required edit.

5. Customer Refresh / reimport UI

There are actually two refresh concepts in the code.

Correct existing refresh function

SMixtormat_Library.cpp:9–15:

FReply SMixtormat::RefreshSurfaceList()
{
    RebuildCategoryList();
    RebuildSurfaceList();
    RebuildMaskList();
    return FReply::Handled();
}

This does not import PNGs. It is the correct customer callback.

But the visible refresh icon does not use it

BuildLibraryPage():294–325 currently contains two icon buttons.

Folder/customer import:

294 SNew(SButton)
...
297 .ToolTipText(... "Choose Texture Folder...")
298 .OnClicked(this, &SMixtormat::ImportSurfaces)

Current refresh icon:

310 SNew(SButton)
...
313 .ToolTipText(FText::Format(
314     LOCTEXT("ReimportShippedHint",
        "Reimport Shipped Library from Plugins/{0}/Content/Textures."),
...
316 .OnClicked(this, &SMixtormat::ReimportShippedLibrary)
...
322 SNew(SImage).Image(... "Mixtormat.Icon.Refresh")

Minimal customer UI patch:

.ToolTipText(LOCTEXT("RefreshLibraryHint", "Refresh Library"))
.OnClicked(this, &SMixtormat::RefreshSurfaceList)

No developer-rebuild button remains in customer UI.

SMixtormat_Document.cpp:22–37 contains the wrapper that currently runs developer reimport and produces customer-facing status strings:

"Reimported shipped library (...)"
"Reimport reported issues"

Once nothing binds to that method, it is no longer customer-facing. You can leave it as dead/internal developer code or remove the wrapper and its declaration.

There is one more customer-visible raw-source reference at SMixtormat_Library.cpp:208–219:

"Starter library is empty. Export the metal maps to:\n{0}"
FMixtormatSurfaceImporter::GetDefaultSourceDirectory()

That must go. Startup/customer UI should never tell users to populate plugin source folders.

6. Registry currently scans only /Mixtormat
Surfaces

MixtormatRegistry.cpp:25–82:

FARFilter Filter;
Filter.ClassPaths.Add(UMixtormatSurface::StaticClass()->GetClassPathName());
Filter.PackagePaths.Add(FName(*FMixtormatPaths::SurfacesRoot()));
Filter.bRecursivePaths = true;

Only /Mixtormat/Surfaces is queried.

Change to two roots:

Filter.PackagePaths.Add(FName(*FMixtormatPaths::SurfacesRoot()));
Filter.PackagePaths.Add(FName(*FMixtormatPaths::ProjectLibrarySurfacesRoot()));
Masks

GetMasks():84–153 currently does:

Filter.PackagePaths.Add(FName(*FMixtormatPaths::MasksRoot()));
Filter.bRecursivePaths = false;

Again, built-in only. Add /Game/Mixtormat/Library/Masks.

One uncertainty: bRecursivePaths = false is okay only if user masks are guaranteed to be direct children of the user Masks root. If user masks may have category folders:

/Game/Mixtormat/Library/Masks/Grunge/...

then this must become true.

Refresh nuance

RefreshSurfaceList() currently re-queries Asset Registry; it does not explicitly call ScanPathsSynchronous().

For assets created normally through Unreal/AssetTools, that is generally enough because they are registered immediately.

If “Refresh” must also discover .uasset files manually copied onto disk while Unreal is already open, an explicit Asset Registry scan of only these roots would be required:

/Mixtormat/Surfaces
/Mixtormat/Masks
/Game/Mixtormat/Library/Surfaces
/Game/Mixtormat/Library/Masks

That is the only point here where the exact intended meaning of “scan existing .uasset assets” is uncertain.

7. Family/category assumptions

There are three relevant cases.

A. Surface Registry fallback — must change

MixtormatRegistry.cpp:59–67:

Entry.Family = Surface->Family;
if (Entry.Family.IsNone())
{
    FString Family = Asset.PackagePath.ToString();
    Family.RemoveFromStart(FMixtormatPaths::SurfacesRoot() + TEXT("/"));
    ...
}

The fallback assumes every surface is below /Mixtormat/Surfaces. A user asset with empty Surface->Family under /Game/Mixtormat/Library/Surfaces/Stone/... would derive the wrong string.

Minimal fix: detect which of the two supported surface roots prefixes the package path, strip that root, then extract the first relative directory.

B. Import family naming — not root-dependent

MixtormatSurfaceImporter.cpp:1148–1155 derives Family from the source filename:

FString Identity = Set.BaseName;
Identity.RemoveFromStart(TEXT("TX_"));
...
const FString Family =
    Parts.IsEmpty() ? TEXT("Uncategorized") : Parts[0];

That naming behavior itself does not need to change for the requested architecture.

Immediately afterward, however, the destination is hardcoded:

const FString TexturePath =
    FMixtormatPaths::RawTextureFamilyRoot(Family);
const FString SurfacePath =
    FMixtormatPaths::SurfaceFamilyRoot(Family);

Those lines must select user-library roots for customer imports.

C. Normal category extraction — adjacent, not required by your target

FMixtormatRegistry::GetNormals() also strips only FMixtormatPaths::NormalsRoot() when deriving categories.

That matters only if standalone user Normals are also going into /Game/Mixtormat/Library/.... Your requested architecture specifically says dual-root surfaces/masks, so I would not expand this patch to normals yet.

Likewise ImportShippedNormals() derives categories relative to the physical shipped Content/Textures/Normals/Source hierarchy; that remains appropriate for developer rebuilding only.

8. Exact call graphs
Startup — current
MixtormatEditorModule.cpp
FMixtormatEditorModule::SpawnMixtormatTab()       100–111
    ↓
SMixtormat.cpp
SMixtormat::Construct()                            13–25
    ↓
Registry::GetSurfaces()
Registry::GetMasks()
    ↓ if either empty
SurfaceImporter::ImportDefaultLibrary()          1001–1039
    ├─ EnumerateShippedSourceDirectories()        786–828
    │    └─ ImportDirectory()                    1071–1335
    ├─ ImportShippedMasks()                       830–899
    ├─ ImportShippedNormals()                     901–960
    └─ ImportShippedEffects()                     962–993

Target:

Spawn tab
  → Construct
  → BuildWorkspaceUI
  → registry reads only
Customer Refresh — current
BuildLibraryPage()                                310–325
    ↓ Reimport icon
SMixtormat::ReimportShippedLibrary()               22–37
    ↓
SurfaceImporter::ReimportShippedLibrary()        1041–1044
    ↓
ImportDefaultLibrary()                           1001–1039
    ↓
raw PNG scanning/import

Target:

Refresh icon
    ↓
SMixtormat::RefreshSurfaceList()                    9–15
    ├─ RebuildCategoryList()
    ├─ RebuildSurfaceList()
    └─ RebuildMaskList()
          ↓
      Asset Registry only
Folder/customer import — current
BuildLibraryPage folder icon                      294–307
    ↓
SMixtormat::ImportSurfaces()                       10–20
    ↓
SurfaceImporter::ImportFromDialog()              1046–1069
    ↓
SurfaceImporter::ImportDirectory()               1071–1335
    ↓
/Mixtormat/Textures/<Family>/Raw
/Mixtormat/Surfaces/<Family>
/Mixtormat/Materials/Instances/<Family>
/Mixtormat/Thumbnails/Surfaces/<Family>

ImportFromDialog() also currently defaults the file dialog to the developer shipped-source location through GetDefaultSourceDirectory().

Target:

external PNG folder
   ↓
ImportFromDialog
   ↓
USER import target
   ↓
/Game/Mixtormat/Library/...
Texture / surface save — current
ImportDirectory()
 │
 ├─ ImportTexture()                         232–309
 │    └─ SavePluginAsset()                  165–193
 │
 ├─ BuildDerivedRAMH()                      516–592
 │    └─ CreateOrUpdateGeneratedRAMH()      348–445
 │         └─ SavePluginAsset()
 │
 ├─ CreateOrLoadSurface()                   631–650
 │    └─ SavePluginAsset()                 1324–1327
 │
 ├─ CreateOrUpdatePreviewMaterial()         695–748
 │    └─ plugin path currently at line 708
 │    └─ SavePluginAsset()                 1320–1323
 │
 └─ CreateOrUpdateSurfaceThumbnail()
      └─ CreateOrUpdateThumbnailTexture()
      └─ SavePluginAsset() in importer

The final preview/surface save pair is visible at 1320–1327.

Registry scan
RebuildCategoryList()
RebuildSurfaceList()
    ↓
FMixtormatRegistry::GetSurfaces()
    ↓
CURRENT: /Mixtormat/Surfaces only

RebuildMaskList()
    ↓
FMixtormatRegistry::GetMasks()
    ↓
CURRENT: /Mixtormat/Masks only
9. Thumbnail path issue

This is easy to miss.

The generic creator is already good:

MixtormatThumbnailRenderer.cpp:156–250

CreateOrUpdateThumbnailTexture(
    const FString& DestinationPath,
    ...)

It can create in /Game without architectural changes.

But its public surface wrapper hardcodes:

FMixtormatPaths::SurfaceThumbnailFamilyRoot(Family)

at 605–626. The mask wrapper similarly hardcodes MaskThumbnailsRoot() at 582–603.

For user surface imports, the surface method should receive the already-selected thumbnail destination instead of choosing /Mixtormat itself.

There is a related registry helper at MixtormatRegistry.cpp:13–22:

LoadGeneratedMaskThumbnail(...)
    → FMixtormatPaths::MaskThumbnailsRoot()

So user masks without an explicit UMixtormatMask::Thumbnail may fail to find their generated thumbnail.

This does not prevent the asset itself from being discovered, but it can affect the gallery thumbnail.

10. Existing tests

There is an existing Unreal Automation Test setup under:

Source/MixtormatEditor/Private/Tests/
    MixtormatCompositorTests.cpp
    MixtormatLayerPreviewTests.cpp
    MixtormatNormalHeightTests.cpp

They use WITH_DEV_AUTOMATION_TESTS and IMPLEMENT_SIMPLE_AUTOMATION_TEST.

MixtormatNormalHeightTests.cpp even has:

"Mixtormat.Import.NormalDerivedHeight"

but it tests FMixtormatNormalHeightGenerator::Generate(), not package destinations or FMixtormatSurfaceImporter.

I found no existing automation coverage for:

startup not importing;
ImportDirectory destination ownership;
/Game/Mixtormat/Library;
plugin-vs-user save validation;
dual-root surface registry;
dual-root mask registry;
customer Refresh versus shipped rebuild.

The compositor and layer-preview tests are unrelated to these paths.

Minimal patch sequence

I would implement it in this order.

1. Add user library paths

MixtormatPaths.h/.cpp

Add only the /Game/Mixtormat/Library/... roots required by surface/mask import.

Risk: very low.

Do not change existing /Mixtormat/... helpers.

2. Make ImportDirectory aware of ownership

Inside MixtormatSurfaceImporter.cpp, introduce one small internal distinction:

ShippedDeveloper
UserProject

Then:

ImportDefaultLibrary()
    → ShippedDeveloper

ImportFromDialog()
    → UserProject

Do not duplicate the importer.

For UserProject, select:

/Game/Mixtormat/Library/Textures/<Family>/Raw
/Game/Mixtormat/Library/Surfaces/<Family>
/Game/Mixtormat/Library/Materials/Instances/<Family>
/Game/Mixtormat/Library/Thumbnails/Surfaces/<Family>

Risk: medium, because the destination propagates through texture, derived RAMH, preview MIC, thumbnail, and surface saves.

3. Generalize save validation

Replace the plugin-only assumption:

IsPluginAssetPath()
SavePluginAsset()

with a helper that validates against the expected root for the current operation.

Conceptually:

developer rebuild → /Mixtormat/
user import       → /Game/Mixtormat/Library/

Do not simply remove path validation.

Risk: medium because every current SavePluginAsset call must receive the correct ownership context.

4. Pass user destination into preview/thumbnail creation

CreateOrUpdatePreviewMaterial() currently hardcodes plugin materials.

CreateOrUpdateSurfaceThumbnail() currently hardcodes plugin thumbnails.

Make both destination-aware.

Risk: medium/low. Thumbnail header/cpp signature must agree with importer call sites.

5. Remove startup import

Delete only SMixtormat.cpp:17–20.

Risk: very low.

// remove
if (GetSurfaces().IsEmpty() || GetMasks().IsEmpty())
{
    ImportDefaultLibrary();
}
6. Rewire customer Refresh

In BuildLibraryPage():

old:
refresh icon → ReimportShippedLibrary

new:
refresh icon → RefreshSurfaceList

Change tooltip to Refresh Library.

The developer raw rebuild service can remain compiled but has no customer UI entry.

Risk: very low.

7. Fix empty-library text

Remove:

Export the metal maps to:
<plugin source directory>

Do not call GetDefaultSourceDirectory() from customer UI.

Risk: none meaningful.

8. Make registry dual-root

GetSurfaces():

PackagePaths:
    /Mixtormat/Surfaces
    /Game/Mixtormat/Library/Surfaces

GetMasks():

PackagePaths:
    /Mixtormat/Masks
    /Game/Mixtormat/Library/Masks

Fix the surface Family fallback so it strips whichever root matched.

Risk: low. Main logic risk is accidentally applying /Mixtormat/Surfaces stripping to a /Game/... path.

9. Add focused automation coverage

Use the existing Private/Tests infrastructure. Keep tests narrowly around:

built-in path != writable user path
user roots resolve under /Game/Mixtormat/Library
registry accepts both roots

No compositor changes are required for this architecture.

Final implementation shape

After the patch, there should be exactly three distinct behaviors:

BUILT-IN
/Mixtormat/...
read only
registry scans it
never customer-written


USER IMPORT
external PNGs
    ↓
/Game/Mixtormat/Library/...
writable
registry scans it


DEVELOPER REBUILD
raw shipped PNG sources
    ↓
/Mixtormat/...
developer operation only
never startup
never customer Refresh
never customer UI

One uncertainty: GitHub's code-search index returned incomplete results during the audit. I therefore verified the relevant importer, path, registry, widget, thumbnail, module, and complete test-directory files directly. The change map above is high-confidence for those paths, but I would not claim an absolute repository-wide guarantee that no obscure uninspected file references ImportDefaultLibrary() solely from the failed index search.