# AI Reader changelog

## v1.1.19 — 2026-08-22

### Segmentation and translations now belong to the project
Splitting a paper into paragraphs costs seconds of CPU; translating it costs
model tokens. Until now both stayed in a local JSON cache and were paid for
again on every machine and by every collaborator. They are now objects in the
research project, synced like everything else.

- **Your own other machines get them back.** Open a paper you segmented or
  translated on the desktop and the laptop has it, without re-running the
  splitter or paying for the tokens twice. The key is the file's content hash,
  so it works even if the PDF was renamed or moved.
- **Collaborators who haven't done the work get yours.** Open a paper someone
  in the project already segmented and its paragraphs are there, attributed to
  them in a line above the list.
- **What you did yourself always wins.** Paragraph lists are all or nothing —
  once you have segmented or edited a paper, nobody else's segmentation
  replaces it. Translations are per paragraph: one you translated yourself is
  never overwritten, and a collaborator's only ever fills a paragraph you have
  no translation for. Adopted work is marked as adopted and is not re-published
  under your name, so a project of five members doesn't end up storing five
  copies of the same thing.
- **Off with one checkbox.** Settings → Updates & privacy → "Share with
  project". Turn it off and a paper's text never leaves the machine; the local
  caches keep working exactly as before.
- Payloads are deflated before they go up — a 790 KB segmentation travels as
  about 200 KB — and a paper too large to share (over 4 MB compressed) is left
  alone rather than wedging the queue.

### Sync holds up under the bigger objects
- **The pull is paged.** A first sync of a busy project used to be one
  response with everything in it; it now walks pages of 200 objects, parking
  the cursor on what it has applied so an interrupted sync resumes instead of
  starting over.
- **The push is batched** by object count and by size, so one request stays
  small and the outbox drains steadily.
- **The server accepts them.** Its JSON body limit was the Express default of
  100 KB, which is smaller than a single segmented paper.
- **Paper data is indexed, not scanned.** Opening a paper looks its
  segmentation up by key instead of parsing every synced blob in the project.

## v1.1.18 — 2026-08-21

### Translate a selection where you selected it
- **Right-click text in the PDF and choose "Translate Selection".** A
  card opens next to it with the translation, streaming in as the model
  writes. Copy takes the translation with you.
- **The cards are pinned.** They stay put when the card loses focus,
  when you select something else, and while you scroll — only the ×
  closes one. Drag a card by its header to move it, and open as many as
  you want: each new one lands on top, clear of the ones already there.
- **When the selection sits inside a paragraph the app knows, that
  paragraph is what gets translated.** It is the same row the right pane
  shows and the same on-disk cache, so the pane fills in at the same
  time and a paragraph you have already paid for opens instantly, with
  no second API call.
- **Anything else is translated on its own** — a selection running
  across two paragraphs, a phrase, or a paper that has not been split
  into paragraphs yet. Those results are cached per paper too.

### Dragging a splitter is smooth again
- **The splitter handle has a proper grab area and cursor.** It was a
  4 px strip with no cursor change, so there was nothing to tell you it
  could be dragged and it was easy to miss. The grab area is now 10 px
  (the visible line is still 4 px) and the pointer turns into the
  resize cursor over it.
- **The panes keep reflowing as you drag, but no longer once per mouse
  move.** Every move re-wrapped every paragraph, chat bubble and the
  whole interpretation document — 3.1 ms, 3.1 ms and 7.8 ms a move,
  measured, which is what ate the frame budget. They now re-wrap at
  about 30 times a second while the handle moves and land exactly right
  when it is released. Same measurement after: 0.10, 0.16 and 0.71 ms a
  move.
- **A paragraph list re-flowing no longer jumps the PDF.** Changing row
  heights shift the list's scroll position, which the pane reported as
  "the user scrolled to page N" — so the PDF pane could be dragged into
  page jumps mid-resize. Position reports are ignored while a handle is
  moving.
- The PDF pane itself was measured at 0.07 ms per pixel and left alone.
- Pane widths are now written once, 300 ms after you let go of the
  handle, instead of every 300 ms throughout the drag — which is what
  the code already claimed to do.

## v1.1.17 — 2026-08-20

### Fit-to-width really fits
- **Clicking the zoom percentage now zooms to exactly the width that
  fits.** It divided by the pane's outer width, but the page column is
  two pixels narrower than that, so the page landed just past the edge
  of overflow and the horizontal scrollbar appeared every single time.
  It now zooms as far as it can go without one.
- **A rotated document fits by its rotated width.** Fit-to-width always
  measured the upright page, so it was wrong by the page's aspect ratio
  whenever the view was turned 90°.
- **A sub-pixel overflow no longer counts as overflow**, so the bar
  cannot flicker back on over a rounding remainder.

### A stalled model no longer looks like a stalled app
- **Requests that go quiet now time out.** If a provider accepts the
  request, answers 200 and then never sends a token, the reply used to
  hang forever: no error, no progress, and — because translation runs
  two blocks at a time — the whole batch stopped dead with nothing on
  screen to explain it. Two minutes of complete silence now ends the
  request and reports it. The clock resets on every byte received, so a
  slow model that keeps producing output is never cut off.
- **Errors reported inside a streamed response are shown.** Gateways
  have to commit to "200 OK" before they know whether the model will
  answer, so they report the failure as an event in the stream. Those
  events were being dropped and the request just ended empty.
- **An empty translation counts as a failure, not a result.** A block
  that came back blank was marked Translated and the blank was written
  to the on-disk cache, so reopening the paper restored the blank and
  the block was never retried. It is now marked Failed, left out of the
  cache, and picked up by Retry failed.

## v1.1.16 — 2026-08-20

### Scrollbars appear only when there is something to scroll
- **The PDF pane no longer shows a horizontal scrollbar at every zoom.**
  The page column was sized a scrollbar's width wider than the viewport,
  so the content technically overflowed even when the page was a
  fraction of the window. It now overflows only when the page really is
  wider than the pane — zoom out and the bar goes away.
- **A one-page paper zoomed out shows no vertical bar either**, for the
  same reason: each bar now tracks its own axis honestly.
- **Zooming re-lays out immediately.** The relayout ran before the new
  page width had been computed, so the scrollable width stayed one zoom
  step behind until something else moved.

### Tick a whole folder in the folder pane
- **Folder rows have a tick box.** It selects every PDF underneath —
  subfolders included, and the parts of the tree you never expanded —
  and shows a partial mark while only some of them are selected. The
  right-click actions are still there.

### Moving a panel
- **The pane grip now uses the move cursor** (the four-headed arrow), and
  keeps it for the whole drag instead of losing it to whatever the
  pointer passes over. A drag interrupted by a popup or a window switch
  now also clears its insertion marker.

## v1.1.15 — 2026-08-20

### Segmentation happens when you ask for it
- **Opening a paper no longer splits it into paragraphs by itself.**
  That work costs seconds of CPU (and a GROBID round trip) on a long
  PDF, which is wasted when you only wanted to look at the pages. The
  toolbar's **Segment** button — and the button in the empty paragraph
  pane — runs exactly the same pipeline when you want it.
- **New setting** under *Paragraph segmentation*: "Segment a paper
  automatically the first time it is opened", off by default. Papers
  that were already segmented still come back instantly from cache.

### Add a whole folder to the library at once
- **Tick PDFs in the folder tree and add them in one go.** The strip
  above the tree adds the ticked files to the current project: each one
  is hashed, entered in the library, looked up by DOI/arXiv and
  uploaded, one after another, without opening any of them. Papers the
  project already has are skipped, not duplicated.
- **Right-click a folder** to select (or add) every PDF underneath it,
  subfolders included — the tree only lists what you've expanded, so
  this reads the folder itself.

### Selection and tabs
- **Ctrl+C now copies the selected PDF text** (⌘C on macOS, and the
  physical Control key works there too). Clicking into the page puts it
  in the keyboard focus chain, which a drag-selection never used to do —
  so the shortcut went nowhere and only the right-click menu copied.
- **Right-click a tab** for Close / Close Others / Close All.

## v1.1.14 — 2026-08-20

### The library tells you what it's doing
- **Upload, download and check progress now appear** at the bottom of
  the library pane. They were reported internally but shown nowhere,
  so a failed upload looked exactly like a successful one.

### PDFs are recorded only once they're really stored
- **A paper is marked as having a PDF only after its bytes reach the
  cloud.** The record used to be written before the upload, so a
  failed transfer left an entry claiming a PDF that other machines
  then tried, and failed, to download.
- **New "Check PDFs" button** reconciles the project with what's
  actually stored: papers whose PDF is missing are re-uploaded when
  the file is still on this machine, and entries whose file exists
  nowhere are retired so they stop causing failed downloads. The
  library entries themselves are never touched.

- Buttons in the project settings dialog are all one size now.

## v1.1.13 — 2026-08-20

- **Papers opened from the library no longer come up blank.** The
  library handed the viewer a bare filesystem path where a file:// URL
  was required, so the PDF pane stayed empty while the paragraph list
  filled in — and, for the same reason, the paper was treated as
  remote, which skipped GROBID and left fragmented fallback
  paragraphs. Bare paths are now normalized wherever a paper is
  opened, so no caller can trigger this again.

## v1.1.12 — 2026-08-20

### Library PDFs sync outside the office network
- **PDF uploads and downloads now go through the API host** instead of
  straight to object storage. The old route handed the client a
  presigned URL pointing at an internal address, so adding a paper —
  or opening a colleague's paper on another machine — only worked on
  the office network and failed silently everywhere else. Bytes now
  travel over the same host as everything else, with the same login;
  storage is no longer exposed to clients at all. Uploads are still
  deduplicated by content hash, so a paper someone else already added
  uploads nothing.

## v1.1.11 — 2026-08-20

### Projects can be renamed and deleted
- **New "Edit project" button** next to the project picker: rename a
  project or change its description (needs editor or owner access),
  and — for owners — delete it. Deletion asks you to type the project
  name first and tells you how many unsynced local changes would be
  lost, because it removes the papers, notes, annotations and AI
  results for every member, permanently. Deleting also purges the
  project's local objects, sync cursor and search index, which used
  to linger forever as unreachable rows.

### PDF scrollbars are back
- **The PDF pane now shows real scrollbars** whenever there's
  something to scroll, and you can drag them. They existed before but
  were invisible in practice: the app pans by moving the view
  directly, which never triggered the auto-hide scrollbar's fade-in.

### The TOC says where it came from
- The table of contents is now labelled **"from paper structure"** or
  **"by AI"**, and the button reads **"Rebuild with AI"** instead of
  "Refresh" — it discards the structural outline and spends a model
  request, which the old wording hid.

## v1.1.10 — 2026-08-20

Fixes for the five issues from v1.1.9 field testing.

### Re-segment actually works now
- **Clicking Re-extract no longer looks (or is) dead.** Three stacked
  problems fixed: no feedback at all during the tens-of-seconds
  extraction (now: button disables, the status bar shows
  "Segmenting…", the paragraph pane spins); a late GROBID reply from
  the previous cycle could swallow the fresh result entirely; and on
  any paper you had ever translated, the GROBID upgrade was refused
  forever by the keep-your-translations guard. An explicit re-segment
  now always wins, verified by a headless regression test.

### Auto-update relaunch, take two
- **The app now exits immediately after starting the installer** —
  the officially documented Inno self-update pattern — instead of
  waiting to be force-closed mid-file-swap, which could silently
  break the relaunch step. The installer also writes a log to
  AppData\update-install.log so any future failure is diagnosable.
  (Takes effect updating FROM this version; the 1.1.9 → 1.1.10 hop
  may still need one manual reopen.)

### Standard buttons finally speak Chinese
- **Packaged Windows builds now ship Qt's own translation catalogs**
  (OK/Cancel/Close and friends). The deploy script had been passing
  --no-translations all along, so the catalogs the 1.1.5 loader fix
  was looking for never existed on user machines.

### Server side (no update needed)
- **The sync WebSocket reconnect storm is gone**: the product
  homepage rule on the server was swallowing the WebSocket handshake
  at the domain root, so every client retried every 5 seconds
  forever (sync silently ran on polling only). Real-time sync is
  back for all versions.

## v1.1.9 — 2026-08-20

- **Segmentation and the TOC are one operation now**: every applied
  GROBID segmentation (first open or an explicit re-segment) also
  refreshes the table of contents from the paper's structure — the
  old TOC referenced paragraph ids the fresh segmentation just
  replaced. An LLM generation you start yourself still wins; the rule
  is simply "latest result wins", on screen and in the cache.
- **Auto-update reopens the app when it finishes** (Windows): the
  installer's launch step was skipped in silent mode, so a one-click
  update ended with… nothing. The silent path now relaunches the new
  version explicitly, de-elevated.

## v1.1.8 — 2026-08-19

### Table of contents, straight from the paper
- **The TOC now comes from GROBID's structural analysis** — the same
  service that segments paragraphs also yields the section hierarchy
  (numbering-aware levels, per-section pages), so a proper outline
  appears automatically with no LLM call. The Generate button still
  runs the LLM path, and an LLM-generated TOC always wins over the
  structural one.

### Hardened GROBID endpoint
- **The public segmentation service now requires a rotating
  verification code** (RFC 6238 one-time password) sent automatically
  by the app — anonymous scanners hitting the endpoint get 401.
  Older releases fall back to the built-in splitter until updated.
- **Clearing the GROBID URL in Settings falls back to the default
  public endpoint** (matching how the update-manifest URL behaves) —
  the checkbox is the only on/off switch.

### Fixes
- Tour step 6's spotlight no longer drifts to screen center when the
  CAS session finishes restoring — it now tracks whichever of the
  Sign in / account buttons is visible.

## v1.1.7 — 2026-08-19

### One-click updates
- **"Update now" downloads and installs by itself** — progress in the
  button, silent install, automatic app restart. No more browser
  round-trips (Windows; other platforms still open the download).

### Global polish sweep
- **Every color-contrast bug from a full audit fixed**: the unreadable
  dark-banner buttons, chat-bubble text in dark mode, paragraph status
  badges, splitter handles, drag-grips, code-block controls, and more —
  all verified against both themes.
- **Full i18n audit**: file-dialog filters, member roles, metadata
  types, paragraph kind/status labels and other raw codes now display
  translated; two hard-coded English strings in the chat/RPC layer
  wrapped; catalog 100% translated.

## v1.1.6 — 2026-08-19

- **The update check answers where you clicked**: the result row in
  Settings now carries a Download button — the window-bottom banner
  was hidden behind the modal dialog, so a successful check looked
  like nothing happened.
- **Stale update URLs migrate automatically**: old installs that
  saved the retired raw.githubusercontent manifest address (silently
  unreachable for most users) are reset to the server default on
  launch.

## v1.1.5 — 2026-08-19

- **Standard buttons now follow the system language in packaged
  builds.** OK / Cancel / Close and friends come from Qt's own
  translation catalogs, which the app only looked for in the build
  machine's Qt install; it now also checks the deployed
  translations folder beside the executable (and the mac bundle's
  Resources), so Chinese systems get Chinese buttons.
- The Settings dialog's update-manifest field no longer hints at the
  retired GitHub URL; the live update feed
  (aireader.d2ssoft.com/update/manifest) now serves v1.1.4+ with a
  working download route.

## v1.1.4 — 2026-08-19

- **The zoom readout button changes behavior**: a single click fits
  the page to the window width; a double-click returns to 100%.

## v1.1.3 — 2026-08-18

### Onboarding
- **The welcome tour now replays on the first launch of every new
  version**, so each release's changes get a guided walk-through.
- **Two new tour steps**: signing in through CAS to sync your library
  across devices, and sharing a project library with teammates
  (owner / editor / viewer roles, shared AI interpretations).

### Interface
- **Primary buttons now use the mainstream blue-fill + white-text
  look in both themes** — the pale-blue/dark-text dark-mode variant
  is gone.

### Packaging (Windows)
- Installer hardening against missing MSVC runtimes: refuses to
  package without the CRT DLLs, bundles the official vc_redist for a
  silent belt-and-braces install, and rejects Windows older than
  10 (1809) at install time instead of crashing at launch.

## v1.1.2 — 2026-08-18

The smoothness release: profiling on a 30-page paper.

### Performance
- **Open instantly, scroll smoothly.** Everything that touched
  QtPdf's global PDFium lock on the UI path is gone: page sizes are
  cached for layout, paragraph extraction and selection structures
  build on paced worker threads after the first renders, and clicks
  locate text by binary search instead of per-character scans.

### Panning
- **No more blank side margins.** Pages narrower than the window pan
  vertically only; wider pages stop panning with the page edge flush
  against the window edge. Wheel scrolling clamps the same way.

### Fixes
- Hand cursors get a true filled silhouette — page text no longer
  shows through at the finger roots.
- New installs default the GROBID service to the public
  `https://aireader.d2ssoft.com/grobid` endpoint.
- Product homepage now lives at https://aireader.d2ssoft.com (with
  the Windows installer mirrored on our own server).

## v1.1.1 — 2026-08-18

Fixes from the first round of user testing of v1.1.0's selection
and paragraph features.

### Selection & paragraphs
- **Selection works everywhere**: on every page (not just the
  first) and across pages; double-click no longer picks up words
  from the neighboring line; triple-click selects the full
  paragraph as shown in the reading pane.
- **Rendered-line extraction rewritten** and shared between the
  paragraph splitter and selection — PDFium's line breaks
  under-report visual lines, and styled words (e.g. italics) no
  longer fragment paragraphs. Papers opened earlier keep their
  cached segmentation until re-extracted.

### Cursors
- I-beam / link / hand cursors now show correctly; hand cursors
  redrawn with a solid white fill.

### GROBID
- **GROBID service deployed and on by default** — new installs use
  `https://aireader.d2ssoft.com/grobid` automatically; existing
  installs can set it under Settings → Paragraph segmentation.

## v1.1.0 — 2026-08-18

The reading-experience release.

### PDF text selection, rebuilt
- **Browser-grade selection**: drag across page boundaries, double-click
  to select a word, triple-click a paragraph, keep dragging to extend
  by that unit; auto-scroll when dragging past the viewport edge.
- **Cursors that talk**: I-beam over text, pointing hand over links
  (click to follow), arrow elsewhere.
- **Right-click menu** (Copy / Select All on Page) and Ctrl/Cmd+C,
  Ctrl/Cmd+A in the PDF view.
- **Clean copied text**: hyphenated line breaks merge back into whole
  words and paragraphs reassemble — no more one-line fragments.

### Smarter paragraph detection
- **GROBID integration** (default-on, needs a reachable service):
  freshly opened papers get real paragraphs — merged across columns
  and pages — from GROBID's document model, with silent fallback to
  the built-in splitter. Configure under Settings → Paragraph
  segmentation; self-host with
  `docker run -d -p 8070:8070 grobid/grobid:0.9.1-crf`.

### Interface
- **Every dialog redesigned** with one consistent visual language:
  cleaner chrome, clear primary/secondary buttons, grouped sections
  in Settings; dark-mode fixes in the changelog and welcome wizard.
- New hand-tool icon.

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
