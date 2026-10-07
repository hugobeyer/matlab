# Conventions

## Naming

| Prefix | Meaning |
|---|---|
| `FMixtormat*` | plain struct |
| `UMixtormat*` | UObject / data asset |
| `EMixtormat*` | enum |
| `SMixtormat*` | Slate widget |
| `Mixtormat*` | namespace / free helpers |
| `Add*Passes` | RDG pass group |
| `Gather*Child` | gather entry |
| `Build*Panel` | inspector builder |

Files are `Mixtormat<Thing>.{h,cpp}`. Every source file starts with
`// Copyright 2026 Hugo Beyer. All Rights Reserved.` and uses tabs.

## Parameter / default ownership

One number, one owner. Resolution order:

```
compiled default (struct initializer / CDO)
  → shipped authoring DB (Config/MixtormatParameterAuthoring.json)
  → unsaved developer edit
```

- **Compiled default** — the UPROPERTY initializer in the struct's owning runtime header (`MixtormatLayerTypes.h`, `MixtormatGeneratorTypes.h`, `MixtormatMaskTypes.h`, `MixtormatIdTypes.h`, `MixtormatEffect.h`, …).
- **UI range** — `meta = (UIMin/UIMax/Delta)` on the UPROPERTY (editor-only).
- **Authoring override** — `MixtormatParameterAuthoring.*` + the JSON.
- **Hard bound / sanitize** — `MixtormatParameterDefinition.h` (runtime, ships).

Never duplicate a default. Never widen a hard bound from the editor.

## UI labels vs shader symbols

- `LOCTEXT` labels are display-only; they never touch the parameter `FName`.
- The parameter `FName` is the binding address and the authoring key.
- Shader uniform names may differ from the parameter name; the `// @param` tag
  is the link. Keep it adjacent to the uniform.

## Serialization

- Enums are serialized **by value**: append new entries, never reorder.
- Renames go in `Config/DefaultMixtormat.ini` `[CoreRedirects]`
  (`+EnumRedirects=…`).
- Deprecated fields stay (with `DeprecatedProperty`) so old assets load.
- `TSoftObjectPtr` for asset references.

## IDs / GUIDs

- `MixtormatParameterBinding::EnsureStableIds` — assign persistent identity.
- `RegenerateLayerIdentity` / `RegenerateLayerIdentities` — duplicates get new
  IDs; internal references follow the copy.
- `MixtormatLayerGroups::MakeEffectiveChildId` — deterministic per-member child
  ID for group children (never random).
- Group IDs share the layer-ID namespace; group children share the child-ID
  namespace.

## Enum extension rules

1. Append at the end; keep existing numeric values.
2. Add a `UMETA(DisplayName = "…")`.
3. If renaming, add a `CoreRedirects` entry.
4. Update `MixtormatChildCapabilities.cpp` if the type publishes outputs.

## Shader modification checklist

1. Declare the parameter on the struct (its owning runtime header, e.g. `MixtormatLayerTypes.h` / `MixtormatGeneratorTypes.h` / `MixtormatMaskTypes.h`).
2. Gather it (`Compositing/Mixtormat*Gather.cpp`).
3. Bind it in the pass (`MixtormatGpu*Passes.cpp`).
4. Add the uniform + `// @param` tag in the `.usf`/`.ush`.
5. Add UI meta / authoring entry if it is user-facing.
6. Add a hard bound in `MixtormatParameterDefinition.h` only if the math needs it.

## Unreal patterns used here

- `MIXTORMATRUNTIME_API` / `MIXTORMATSHADERS_API` / `MIXTORMATEDITOR_API` on
  public symbols.
- `UPROPERTY(Category = "…")` drives inspector grouping.
- `LOCTEXT_NAMESPACE "SMixtormat"` in editor `.cpp`.
- Log categories: `LogMixtormat`, `LogMixtormatComposition`,
  `LogMixtormatAssetMigration` — never `LogTemp`.
- Editor-only code is guarded by module type, not `#if WITH_EDITOR`.
