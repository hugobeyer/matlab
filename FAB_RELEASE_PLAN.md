# Mixtormat — FAB Release Plan (glance audit)

This is a plan, not a special mode; I haven't started implementation. Reply `go` to execute this plan, or tell me what to change.

## Goal
- Ship clean Mixtormat 1.0 on FAB for UE 5.8, Win64.
- No dev trash in zip. No docs/prototypes/audits in plugin.

## Success Criteria
- Packaged zip = `.uplugin + Config + Resources + Shaders + Source` (+ curated `Content/` samples only).
- Opens in clean UE 5.8 project, no warnings blocking FAB review.
- FAB listing ready: desc, images, video, support URL, license.

## Verdict (3-file glance)
- Sources: `Mixtormat.uplugin`, `AgentDocs/ARCHITECTURE.md`, `AgentDocs/FILE_MAP.md`, `AGENTS.md`.
- Arch is FAB-safe: `Runtime ← Shaders ← Editor`, 3 modules correct in `.uplugin`.
- `Shaders` + `Editor` are `Editor`-only — good, keeps runtime lean.
- Main risk is not code, it's payload: ~100+ md/html/py/svg/txt helpers ship today if zipped as-is.

## Approach
- Delete nothing yet. Exclude via pack + move dev stuff out of plugin root.
- Keep one source of truth, no migrations/redirects (unshipped per `AGENTS.md`).

## Steps

### 0. Freeze
- Bump `VersionName` to `1.0.0`, changelog started.
- Lock scope: no new generators/effects before FAB.

### 1. Strip trash (exclude from zip, move or gitignore)
- `AgentDocs/` — ALL of it (incl `archive_old/`, `old_docs/`, `audits/`, `Prototypes/`, `NEW_TASKS/`, `new iconography/`, `code_docs/`, `*.html`).
- `auditdocs/`, `Concepts/`, `Docs/` (keep only linkage to `https://hugobeyer.github.io/mixtormat/`).
- Root trash if present: `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `.vs/`, `.claude/worktrees/`, `Docs/docs-assets/`.
- Keep in repo but out of FAB zip: move above to `../Mixtormat-Dev/` or separate private repo.
- Decide: `Content/` — ship only 2–3 demo materials + meshes. Rest stays gitignored.

### 2. Fix `.uplugin` for FAB
- `Description`: 1-line buyer pitch, not dev pitch. Mention bake + runtime control.
- `Category`: keep `Rendering`.
- `SupportedTargetPlatforms`: confirm Win64-only is intentional; FAB buyers expect Win64+ at least.
- `DocsURL`/`SupportURL`: keep site + mailto, verify both live.
- Add `MarketplaceURL` on submit; no code change needed.

### 3. Config + Resources check
- Keep: `Config/DefaultMixtormat.ini`, `Config/FilterPlugin.ini`, `Config/UIStyleTheme.json`, `Config/MixtormatParameterAuthoring.json`.
- Keep: `Resources/Icons/` only used icons (see `AgentDocs/ICONS.md` flow).
- Delete: unused themes, old `UIStyleTheme` backups, prototype JSON.

### 4. Code glance gates (no full review)
- Runtime: no `Editor` includes, no Slate, no GPU.
- Shaders: `Shaders/Private/*.usf|*.ush` match `IMPLEMENT_GLOBAL_SHADER` sites.
- Editor: style via `MixtormatDesignTokens.h` + `ThemeStore`, help via `SMixtormatHelp` only.
- Enums: numeric-stable, no CoreRedirects needed (unshipped).

### 5. Package + smoke
- Package via `RunUAT BuildPlugin`, test zip in clean UE 5.8 project.
- Check: open asset, compose, preview viewport, bake, packaged-game runtime (baked only).
- Clear `Output Log` errors/warnings before submit.

### 6. FAB listing pack (outside plugin)
- Title, 30-sec video, 5–8 screenshots, feature list, limits (Win64, UE 5.8, bake workflow).
- Demo project link, docs link, support mail, license + EULA note.
- Version + changelog file for updates.

## Validation Plan
- Zip content list shows no `AgentDocs|auditdocs|Concepts|Docs|Prototypes|Intermediate|Saved|Binaries`.
- Clean-project open + bake passes without user testing your dev machine.
- FAB validator / review returns zero file-hygiene flags.

## Risks / Open Questions
- Win64-only: limit sales vs cut QA? Default keep Win64-only for v1.
- `Content/` samples: which 2–3 materials best demo? Pick before pack.
- External site docs: must be live before submit — verify URL.

Reply `go` to execute this plan, or tell me what to change.
