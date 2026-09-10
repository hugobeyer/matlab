# Mixtormat — Fab Shipping Checklist

> Short version: keep GitHub code-focused, build a separate Fab package locally, ship the built-in library as `.uasset` content, keep raw source PNGs private, and import customer content into `/Game/Mixtormat/...`.

---

## 1. The release model

Use **two different things**:

### GitHub repository

This can stay lightweight:

```text
Mixtormat/
├─ Source/
├─ Shaders/
├─ Resources/
├─ Docs/
├─ Mixtormat.uplugin
└─ no giant shipping Content required
```

The 1.4 GB material library does **not** need to live in the public GitHub repository.

### Fab submission package

Build this separately from your local plugin installation:

```text
Mixtormat/
├─ Config/
│  └─ FilterPlugin.ini
├─ Content/
│  ├─ Surfaces/
│  ├─ Textures/
│  ├─ Masks/
│  ├─ Materials/
│  ├─ Meshes/
│  ├─ Lighting/
│  └─ ...
├─ Resources/
│  ├─ Icon128.png
│  ├─ Icons/
│  └─ ...
├─ Shaders/
│  └─ Private/
├─ Source/
│  ├─ MixtormatRuntime/
│  ├─ MixtormatEditor/
│  └─ MixtormatShaders/
├─ Docs/                  optional
└─ Mixtormat.uplugin
```

Fab currently requires Code Plugins to contain:

- `.uplugin`
- `Source/`
- `Content/`
- `Config/`

Mixtormat should therefore be submitted as a **Code Plugin**, not as a normal content pack.

---

# 2. What is the `Config` folder?

For Mixtormat, think of `Config/` primarily as part of the **plugin packaging contract**.

The important file is:

```text
Config/FilterPlugin.ini
```

`FilterPlugin.ini` tells Unreal/Fab's plugin packaging process which **extra top-level folders** must survive packaging.

`Source/`, `Content/`, and `Resources/` are standard plugin folders.

Mixtormat also has:

```text
Shaders/
```

That is important because your compute shaders must be shipped with the plugin.

So use:

```ini
[FilterPlugin]
/Shaders/...
```

If you ship the HTML documentation inside a top-level `Docs/` folder, use:

```ini
[FilterPlugin]
/Shaders/...
/Docs/...
```

If you later add another non-standard top-level distribution folder, add it here too.

### What `FilterPlugin.ini` is NOT

It is not where normal Mixtormat preferences need to live.

It is mainly a **packaging inclusion filter**.

Runtime/editor settings, if you later need plugin `.ini` settings, follow normal Unreal plugin config conventions separately.

### Recommended Mixtormat file

```text
Mixtormat/Config/FilterPlugin.ini
```

```ini
[FilterPlugin]
/Shaders/...
/Docs/...
```

If `Docs/` is not part of the Fab package:

```ini
[FilterPlugin]
/Shaders/...
```

---

# 3. Built-in Mixtormat content

The built-in library should be treated as **installed product content**.

Recommended layout:

```text
/Mixtormat/Surfaces/...
/Mixtormat/Textures/...
/Mixtormat/Masks/...
/Mixtormat/Materials/...
```

## Ship `.uasset`, not duplicate PNG source art

For the built-in library:

**Ship:**

```text
.uasset textures
.uasset surfaces
.uasset masks
master materials
preview materials
preview meshes
lighting assets
other required Unreal assets
```

**Do not ship just for safety:**

```text
original PNG source textures
working PSDs
Houdini files
Substance files
source renders
development exports
```

Your 1.4 GB library is acceptable as long as those files are actually part of the product.

There is little value in shipping both:

```text
Stone_BC.png
Stone_BC.uasset
```

when the customer only needs the Unreal asset.

That duplicates the material library and makes the download larger.

---

# 4. What should "Refresh Library" do?

For a customer installation:

```text
Refresh Library
      ↓
scan existing Mixtormat .uassets
      ↓
rebuild the library UI / registry
```

It should **not** need the original built-in PNGs.

It should not regenerate the whole installed library from raw source textures.

The shipped `.uasset` library should already be ready to use.

---

# 5. Built-in content should be considered read-only

Customer-facing Mixtormat should not normally modify:

```text
/Mixtormat/...
```

Fab/Launcher-installed plugin content may live under an Engine plugin installation and should be treated as product files.

So:

```text
/Mixtormat/...       built-in, installed, read-only conceptually
/Game/Mixtormat/...  customer-created, writable
```

This protects customers from:

- plugin updates overwriting their work
- permissions problems
- engine/plugin folders being read-only
- customer materials becoming mixed with shipped assets

---

# 6. What if the customer deletes a built-in `.uasset`?

Do not ship hundreds of MB of source PNGs just to solve this case.

If a built-in asset under `/Mixtormat/...` disappears:

```text
Refresh Library
      ↓
detect missing built-in asset
      ↓
show "Mixtormat installation is missing content"
      ↓
repair / reinstall plugin
```

Fab/Epic installation is the authoritative copy of built-in content.

Mixtormat should not be responsible for reconstructing a manually deleted shipping asset.

---

# 7. User-imported content

User content should **never** be imported into the plugin.

Recommended location:

```text
/Game/Mixtormat/Library/
```

For example:

```text
/Game/Mixtormat/Library/Surfaces/
/Game/Mixtormat/Library/Textures/
/Game/Mixtormat/Library/Masks/
```

The workflow should be:

```text
User selects external files
    ↓
Stone_BC.png
Stone_N.png
Stone_RAMH.png
    ↓
Mixtormat imports them
    ↓
/Game/Mixtormat/Library/...
    ↓
Refresh Library
    ↓
new user material appears
```

The customer's original PNG/TGA/etc. stays wherever **they** keep their source art.

---

# 8. User reimport

For imported user content, keep enough information to locate the original external sources.

For example:

```text
Material: Stone_04

Base Color:
D:/Materials/Stone_04_BC.png

Normal:
D:/Materials/Stone_04_N.png

RAMH:
D:/Materials/Stone_04_RAMH.png
```

Then:

```text
Reimport
   ↓
read original external source
   ↓
update /Game/Mixtormat/... .uassets
```

Unreal's `AssetImportData` can already retain import-source information while the asset exists.

For extra robustness, Mixtormat can later keep its own lightweight import manifest.

---

# 9. What if a user deletes their imported `.uasset`?

This is different from built-in content.

If you keep a lightweight Mixtormat import record, Refresh Library can detect:

```text
Stone_04
source files still exist
project .uasset missing
```

and offer:

```text
[ Reimport ]
```

This is optional for v1, but it is a good long-term feature.

Without a separate import record, deleting the `.uasset` also deletes its normal Unreal `AssetImportData`, so Mixtormat no longer knows where the original file came from.

---

# 10. Current importer architecture that should change before release

The current importer has development-oriented behavior that is useful for building the built-in library:

```text
Content/Textures
      ↓
hash source files
      ↓
detect changed PNG
      ↓
reimport plugin texture
      ↓
save into /Mixtormat/...
```

It also currently has protection that refuses certain import/save operations outside the plugin mount.

That is useful for **your internal library-building pipeline**, but user import needs a different path.

Recommended split:

```text
A. Built-in Library Refresh
   scan /Mixtormat .uassets only

B. User Import
   external PNG/TGA/etc.
       ↓
   /Game/Mixtormat/Library/...

C. User Reimport
   external source
       ↓
   update /Game asset

D. Developer Library Rebuild
   private source PNG library
       ↓
   rebuild shipping /Mixtormat content
```

`D` can remain a developer-only workflow and does not need to ship to customers.

---

# 11. Release `.uplugin`

Before Fab submission, release-clean the descriptor.

Things to verify:

```text
Version
VersionName
FriendlyName
Description
CreatedBy
CreatedByURL
DocsURL
SupportURL
EngineVersion
FabURL / listing identifier when available
supported platforms
module target types
CanContainContent = true
IsBetaVersion = false for 1.0
```

Recommended first launch strategy:

```text
Unreal Engine 5.8
Win64
```

Only advertise platforms and engine versions you have actually built and tested.

Fab requires a new/updated Code Plugin package for supported new Unreal Engine versions.

---

# 12. Plugin icon

Create:

```text
Resources/Icon128.png
```

Exactly:

```text
128 × 128 PNG
```

Use the existing Mixtormat icon artwork.

Keep your SVG logo/icons too if the Slate UI needs them.

---

# 13. Copyright headers

Before submission, add a consistent copyright header to shipped source.

For example:

```cpp
// Copyright 2026 <Publisher Name>. All Rights Reserved.
```

Apply consistently to shipped:

```text
.cpp
.h
.Build.cs
.usf
.ush
```

Keep the publisher name exactly consistent with the Fab publisher/company identity you intend to use.

---

# 14. Third-party licenses

You already use Lucide-derived icons and have the Lucide/Feather license notice.

Keep that notice in the shipping package.

Recommended:

```text
Docs/ThirdPartyNotices.txt
```

Include:

```text
Lucide Icons — ISC
Feather-derived icons — MIT
```

Also do one final provenance pass over:

- icons
- shaders
- copied/adapted algorithms
- code snippets
- fonts
- HDRIs
- material textures
- masks
- preview assets

For every third-party item, make sure you have redistribution rights.

Research papers and algorithm ideas are not automatically a problem; copied code/art/assets are where licensing matters.

---

# 15. Do not ship development junk

The Fab ZIP should not be the GitHub repository ZIP.

Exclude development material such as:

```text
Prototypes/
AUDIT_REPORT.md
implementation plans
old task documents
Houdini experiments
training scripts
prototype ZIP files
temporary exports
Binaries/
Intermediate/
Saved/
DerivedDataCache/
.git/
.gitignore
```

Ship customer-facing documentation, not your development history.

---

# 16. Recommended customer documentation

Ship or host:

```text
Quick Start
Workspace
Material Library
Importing Materials
Layers
Masks
Generated Masks
Pattern / Region IDs
Effects
Drivers & References
Instances
Preview
Saving
Baking
Shortcuts
Troubleshooting
Requirements / Limitations
```

Your HTML documentation can serve this role.

If `Docs/` is inside the Fab plugin package, remember:

```ini
[FilterPlugin]
/Shaders/...
/Docs/...
```

---

# 17. Important product wording

Describe Mixtormat accurately as:

> An editor material-authoring and compositing tool that produces baked Unreal material assets.

The live compositor runs as an editor authoring workflow.

The production/runtime result is the baked output:

```text
Base Color
Normal
RAM
Height
Material Instance
```

Do not advertise live runtime procedural compositing unless you actually add and support it.

---

# 18. Built-in normal-to-height limitation

The current normal-derived Height pipeline supports square textures at:

```text
1024 × 1024
2048 × 2048
4096 × 4096
```

Document this wherever users import materials or generate Height from Normal.

Do not make customers discover this only from an error dialog.

---

# 19. Fab package build

Do not manually copy random files into a ZIP and assume it is correct.

Run Unreal's plugin packaging flow.

Example:

```bat
Engine\Build\BatchFiles\RunUAT.bat BuildPlugin ^
  -Plugin="D:\Mixtormat\Mixtormat.uplugin" ^
  -Package="D:\Mixtormat_Fab"
```

Then inspect the generated package.

Confirm that it still contains:

```text
Mixtormat.uplugin
Config/
Content/
Resources/
Shaders/
Source/
Docs/          if shipping docs
```

Most importantly, verify that:

```text
Shaders/Private/*.usf
Shaders/Private/*.ush
```

survived packaging.

That is why `FilterPlugin.ini` matters.

---

# 20. Fresh-install test

Do this before Fab submission.

Use a clean Unreal 5.8 project that has never seen Mixtormat.

Test in this order:

### Installation

- Copy/install only the final packaged plugin.
- Do not rely on files from the development checkout.
- Start Unreal.
- Enable Mixtormat.
- Restart.

### Cold shader test

- Test with a fresh Derived Data Cache if practical.
- Open Mixtormat.
- Confirm all global shaders compile.
- Confirm no missing `/Plugin/Mixtormat/...` shader paths.

### Built-in library

- Open Material Library.
- Confirm all shipped surfaces appear.
- Confirm all masks appear.
- Refresh Library.
- Confirm Refresh does not need source PNGs.

### Authoring

- Create a recipe.
- Add several layers.
- Add masks.
- Add generated masks.
- Add Region/Pattern IDs.
- Add effects.
- Test Drivers.
- Test References.
- Test child Instances.
- Save.
- Close.
- Reopen.
- Confirm result is identical.

### User import

- Import external BC/N/RAM or RAMH textures.
- Confirm assets are created under:

```text
/Game/Mixtormat/...
```

- Confirm nothing is written into:

```text
/Mixtormat/...
```

- Restart Unreal.
- Confirm user material is still found.

### Bake

Test:

```text
1K
2K
4K
```

Confirm:

```text
Base Color
Normal
RAM
Height
Material Instance
```

Test updating an existing bake.

Test applying the baked material to an actor.

### Packaged game

Package a minimal game/project using a baked Mixtormat result.

Confirm the baked material works without the editor compositor.

---

# 21. Read-only plugin test

This one is important.

Install Mixtormat in a location that the editor should not modify.

Then use it normally.

Expected:

```text
Built-in plugin content      read only
User materials              /Game/Mixtormat/...
Recipes                     /Game/...
Bakes                       /Game/...
Theme/settings              Saved/... where appropriate
```

Normal customer workflows should succeed without writing to the installed plugin folder.

---

# 22. Missing-content behavior

Recommended behavior:

### Built-in asset missing

```text
Built-in Mixtormat content is missing.

Repair or reinstall Mixtormat.
```

### User asset missing, source known

```text
User material source is available,
but the imported project asset is missing.

[ Reimport ]
```

### User asset missing, source also missing

```text
Material source could not be found.

[ Locate Source ]
```

This can be improved after v1; only the first case is essential for launch.

---

# 23. Final Fab folder

Recommended final shipping package:

```text
Mixtormat/
├─ Config/
│  └─ FilterPlugin.ini
│
├─ Content/
│  ├─ Surfaces/
│  ├─ Textures/
│  ├─ Masks/
│  ├─ Effects/
│  ├─ Materials/
│  ├─ Meshes/
│  ├─ Lighting/
│  └─ Thumbnails/
│
├─ Resources/
│  ├─ Icon128.png
│  ├─ Icons/
│  └─ ...
│
├─ Shaders/
│  └─ Private/
│
├─ Source/
│  ├─ MixtormatRuntime/
│  ├─ MixtormatEditor/
│  └─ MixtormatShaders/
│
├─ Docs/
│  ├─ Documentation.html
│  └─ ThirdPartyNotices.txt
│
└─ Mixtormat.uplugin
```

And:

```ini
[FilterPlugin]
/Shaders/...
/Docs/...
```

---

# 24. The actual order I recommend

Do these in this order.

## P0 — before Fab submission

- [ ] Separate customer `Refresh Library` from developer PNG reimport/rebuild.
- [ ] Make user imports write to `/Game/Mixtormat/...`.
- [ ] Ensure normal workflows never need to write to `/Mixtormat/...`.
- [ ] Build the final local `Content/` library using `.uasset` files.
- [ ] Do **not** include duplicate built-in PNG source art.
- [ ] Add `Config/FilterPlugin.ini`.
- [ ] Add `/Shaders/...` to `FilterPlugin.ini`.
- [ ] Add `/Docs/...` if Docs ships inside the plugin.
- [ ] Add `Resources/Icon128.png`.
- [ ] Release-clean `Mixtormat.uplugin`.
- [ ] Declare only tested engine/platform support.
- [ ] Add source/shader copyright headers.
- [ ] Finish `ThirdPartyNotices`.
- [ ] Verify rights to every shipped texture/mask/HDRI/icon/source.
- [ ] Create the package with `BuildPlugin`.
- [ ] Confirm Shaders survive packaging.
- [ ] Test in a completely fresh UE 5.8 project.
- [ ] Test plugin installation from a read-only location.
- [ ] Test 1K / 2K / 4K bake.
- [ ] Test a packaged game using the baked material.

## P1 — launch quality

- [ ] Finish artist documentation.
- [ ] Add import limitations and normal-to-height supported sizes.
- [ ] Add useful missing-file errors.
- [ ] Audit `Content/` for redirectors and broken references.
- [ ] Remove old `MaterialLab` names from shipped/customer-facing data.
- [ ] Verify all library thumbnails.
- [ ] Verify all included materials are actually worth shipping.
- [ ] Produce Fab screenshots and trailer.
- [ ] Create a clean Fab listing feature/requirements section.

## P2 — later improvements

- [ ] User import manifest.
- [ ] Rebuild accidentally deleted user assets from their external source.
- [ ] Relocate missing source files.
- [ ] Optional separate downloadable material packs.
- [ ] Additional Unreal/platform support after actual testing.

---

# 25. Do not overcomplicate v1

For the first Fab release, the clean architecture is:

```text
BUILT-IN CONTENT
Fab installs .uassets
        ↓
Mixtormat scans them
        ↓
read only


USER CONTENT
User selects PNG/TGA/etc.
        ↓
Mixtormat imports
        ↓
/Game/Mixtormat/...
        ↓
editable / reimportable


DEVELOPER SOURCE ART
Your private PNG/Houdini/etc. sources
        ↓
internal library builder
        ↓
shipping .uassets
```

That is the path I would ship.

---

# Sources / current requirements checked

Epic Fab documentation:

- Asset File Format and Structure Requirements  
  https://dev.epicgames.com/documentation/fab/asset-file-format-and-structure-requirements-in-fab

- Publishing Assets on Fab  
  https://dev.epicgames.com/documentation/fab/publishing-assets-for-sale-or-free-download-in-fab

Unreal Engine plugin documentation:

- Plugins in Unreal Engine  
  https://dev.epicgames.com/documentation/unreal-engine/plugins-in-unreal-engine

Legacy Epic Marketplace plugin packaging guidance is still useful for the purpose of `FilterPlugin.ini`: non-standard top-level distribution folders are listed under `[FilterPlugin]`. For Mixtormat, the important folder is `Shaders/`.
