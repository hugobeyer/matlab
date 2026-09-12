# Copyright 2026 Hugo Beyer. All Rights Reserved.
#
# Clears AssetImportData from Mixtormat's shipping content.
#
# WHY
#   Every imported .uasset stores where it came from. Right now that is an absolute
#   path on the author's machine --
#       C:/Tools/MaterialLab/MatLab/Plugins/Mixtormat/Content/Textures/.../Source/TX_*.png
#   -- plus, on older assets, a dead "/MaterialLab/..." mount left over from the rename.
#   Customers see both in Asset Details -> Source File and on right-click -> Reimport.
#   It ships the author's directory layout and points Reimport at paths that cannot
#   exist on their machine. Nothing is functionally broken: these are editor-only
#   metadata, there are no ObjectRedirectors and no live references to the old mount.
#
# RUN IT ON A STAGED COPY, NOT ON Content/ IN PLACE.
#   The raw sources under Content/Textures/*/Source are real and still used for
#   reimport during authoring. Clearing import data in the working tree would sever
#   that link for the author too. The packaging filter already keeps those sources out
#   of the build (Config/FilterPlugin.ini), so the only thing left to fix is the
#   metadata baked into the .uasset files that DO ship.
#
# HOW
#   1. Copy the plugin to a staging dir, e.g.  D:/Staging/Mixtormat
#   2. Open that staged copy as the plugin of a throwaway project.
#   3. Run this from the editor:  Tools -> Execute Python Script
#      (or: UnrealEditor-Cmd.exe <Project>.uproject -run=pythonscript
#            -script="<path>/strip_import_metadata.py")
#   4. Package from the staged copy.
#
# Prints a summary and saves. Re-running it is harmless.

import unreal

CONTENT_ROOT = "/Mixtormat"

def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path(CONTENT_ROOT, recursive=True)

    cleared, skipped, failed = [], 0, []

    for data in assets:
        asset = data.get_asset()
        if asset is None:
            continue

        import_data = None
        try:
            import_data = asset.get_editor_property("asset_import_data")
        except Exception:
            skipped += 1
            continue

        if import_data is None:
            skipped += 1
            continue

        path = str(data.package_name)
        try:
            # Drops every stored source filename and its timestamp/hash.
            import_data.set_editor_property("source_data", unreal.AssetImportInfo())
            cleared.append(path)
        except Exception as error:
            failed.append((path, str(error)))

    if cleared:
        unreal.EditorAssetLibrary.save_directory(CONTENT_ROOT, only_if_is_dirty=True, recursive=True)

    unreal.log("=" * 70)
    unreal.log("Mixtormat import-metadata strip")
    unreal.log("  assets scanned      : %d" % len(assets))
    unreal.log("  import data cleared : %d" % len(cleared))
    unreal.log("  no import data      : %d" % skipped)
    unreal.log("  failed              : %d" % len(failed))
    for path, error in failed[:20]:
        unreal.log_warning("    %s -- %s" % (path, error))
    unreal.log("=" * 70)

main()
