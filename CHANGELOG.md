# AI Reader changelog

## v1.0.0 — 2026-06-29

The cloud release. v0.2.0 made AI Reader survive real users on a
single machine; v1.0.0 lets a team share one library across machines
— AI Reader is now a collaborative literature workspace, not just a
local reader.

### Cloud literature library
- **Research projects as shared libraries.** Organise papers by
  research topic; each project is one cloud-synced library with its
  own members.
- **Add papers with auto-filled metadata.** Open a PDF and click
  **+ Add**: title / authors / year / venue auto-fill from the DOI
  or arXiv id, and the PDF uploads (content-addressed, so the same
  file is stored once for the whole project).
- **Full-text search** across the project library — title, abstract,
  authors, tags.
- **Offline-first sync.** Browse and edit offline; changes reconcile
  automatically on reconnect (instant via WebSocket, polling as a
  fallback), per-field last-write-wins.

### Collaboration
- **Multi-user projects** with owner / editor / viewer roles; invite
  members by email.
- **Shared AI interpretations.** Share an AI summary / interpretation
  with the project so teammates see it under **Shared**.

### Accounts & updates
- **CAS single sign-on.** One-click sign-in through your browser; no
  passwords stored in the app.
- **Self-hosted auto-update.** The app checks a server manifest on
  launch and offers a one-click download of the new build.

### Interface
- **The library bar folds into the main toolbar** — project picker,
  members, and account no longer take a separate row.
- **Chinese localization** for all of the above.

## v0.2.0 — 2026-05-02

The polish and packaging release. v0.1.0 was the first thing that
launched; v0.2.0 is the first thing that survives reaching real
users on machines other than the developer's.

### Chat
- **Multi-session chat per paper.** Each paper now owns a list of
  named chat sessions in a tab strip on top of the chat pane:
  **+** to add, **×** to close, double-click to rename.
  Session titles auto-derive from the first user message after
  three turns. Persisted per paper at
  `<AppData>/cache/chat/<paperId>.json`.
- **Typed message rendering.** Finished assistant replies render
  as a list of typed QML items (text, code, math) instead of one
  monolithic `TextEdit`. Code blocks get language captions and a
  Copy button; math (`$$ … $$`) renders via MicroTeX with a
  raw-LaTeX yellow fallback when the renderer fails.
- **Per-row Translate** in the Paragraphs right-click menu —
  translate one paragraph without re-running the global pass.

### Paragraphs
- **Source/translation visibility chevrons** per paragraph.
  Hide / show either half independently, persisted in the block
  cache.
- **Settings → Font sizes (px)** lets you scale the body text in
  Chapter menu / Interpretation / Paragraphs / Chat panes.
  Headings and labels in each pane scale relative to the value
  so the visual hierarchy stays intact.
- **TOC + Interpretation no longer wipe** when you split, merge,
  or delete a paragraph — the cancel-and-rehydrate path now
  fires only on actual paper switches.

### UI / onboarding
- **First-run welcome wizard** — a coach-mark tour that dims the
  rest of the UI and spotlights the toolbar buttons it explains.
  Re-launchable from the **?** button on the toolbar at any time.
- **Panel layout + sizes are remembered** between launches.
  Drag the **⋮⋮** grip in any pane's top-left corner to reorder
  the splitter; resize handles and ordering both persist.
- **Auto-scroll to bottom** in the chat and interpretation panes
  while a stream is in flight. Scrolling up pauses the auto-pin
  so you can read older content without being yanked back down.

### Updates & privacy
- **In-app update check** against a small `manifest.json` on
  GitHub Releases. New version → blue banner at the bottom of
  the window with a Download button. Auto-check toggle in
  Settings → Updates & privacy.
- **Crash report opt-in** for Sentry-Native (when the build was
  configured with `-DAIREADER_ENABLE_SENTRY=ON`). Off by default;
  no PII is collected.
- **App version visible** in the Settings dialog footer and in
  the installer's PE-resource metadata (Properties → Details).

### Packaging
- **Windows installer pipeline.** Build → `windeploy.bat` → Inno
  Setup `AiReader.iss` → optionally `sign-windows.ps1` →
  `publish-release.ps1`. The bat auto-locates `windeployqt`
  via `build\CMakeCache.txt`; the deploy bundles the MSVC
  runtime DLLs (vcruntime140.dll & friends) so end-users without
  the Visual C++ Redistributable installed can launch.
- **Persistent launch log** at
  `<AppData>\AI Reader\launch.log` for diagnosing GUI-build
  failures that swallow stderr.
- **MicroTeX font path is relocatable** — math now renders in
  packaged installs, not just dev builds from `build/`.

## v0.1.0 — 2026-04-26

Initial milestone build covering M1–M4: PDF rendering, paragraph
extraction with manual edit ops, side-by-side translation,
auto-generated TOC, summary pane, vision tool, and a chat pane
with cmark-gfm Markdown + 7 paper-aware tools.
