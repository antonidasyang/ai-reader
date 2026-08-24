# qt-harness

Behavioural checks that need the real app objects — services wired together and
driven for real, against local sockets standing in for the backend and the
model. One directory per driver.

```sh
cmake --build build                     # the drivers link against these objects
tools/qt-harness/papersync/run.sh       # ~1 min, 34 checks
tools/qt-harness/translation/run.sh     # ~30 s, 12 checks
tools/qt-harness/analysis/run.sh        # ~2 min, 101 checks
tools/qt-harness/dialogs/run.sh         # ~20 s, opens all 13 dialogs
tools/qt-harness/panes/run.sh           # ~30 s, times a splitter drag
```

Exit status is non-zero if anything failed.

| driver | covers | stand-in |
|---|---|---|
| `papersync` | segmentation and translations syncing into the project — who wins, what gets published, what gets attributed | `common/FakeSync.h`, a `QTcpServer` speaking the sync API |
| `translation` | cancelling a translation run mid-stream | `common/FakeLlm.h`, a `QTcpServer` streaming OpenAI SSE that never finishes on its own |
| `panes` | what a splitter drag costs — sweeps a real pane's width the way a drag does, with and without the `resizing` flag that holds its layout | a fixture PDF and the real panes in a `QQuickView` |
| `dialogs` | every dialog actually opening — loading Main.qml only proves the window's own tree is sound, and a dialog's delegates and bindings are not exercised until it is shown | the real services, wired the way `main.cpp` wires them, in an offscreen window |
| `analysis` | the interpretation layer — citations checked against the paper, unsupported claims demoted, the research profile reaching the prompt, staleness, storage and attribution, the batch that interprets papers nobody has opened, the nine-module close reading, the cross-paper comparison, the category system surviving the reader's edits, and the Markdown export | `common/FakeAnalysisLlm.h` (builds its answer out of the very paragraph markers it was sent) + `common/FakeSync.h` |

## What it actually runs

Not mocks. `build.sh <driver>` reuses **every object file the app build
produced**, swapping out exactly three:

| dropped | replaced by | why |
|---|---|---|
| `src/main.cpp.o` | `<driver>/main.cpp` | the test driver instead of the GUI |
| `src/AuthController.cpp.o` | `common/AuthStub.cpp` | the real one signs in through CAS in a system browser, which a headless run can't drive |
| `src/Settings.cpp.o` | a copy generated at build time | `Settings::setApiKey` writes to the login keychain under service `ai-reader` — **the user's real API key**. The generated copy is the shipping file with those two keychain functions emptied; everything else in it is the real code. |

So `PaperController`, `TranslationService`, `SyncEngine`, `LibraryDb` and
`PaperSyncService` are the shipping code, wired together the way `main.cpp`
wires them.

Compile and link flags are read out of `build.ninja`, so there is nothing to
keep in sync with CMakeLists.

## What papersync covers

- **Rule 1 — same account, another machine.** A paper segmented elsewhere comes
  back; it counts as ours, not as a loan.
- **Rule 2 — another account.** A paper nobody here has segmented takes the
  project's copy, attributed to whoever segmented it. Our own segmentation is
  never replaced. Translations go paragraph by paragraph: ours is never
  overwritten, theirs only fills a gap.
- **Attribution.** The header chip and the per-paragraph badge appear and clear
  at the right moments.
- **Publication.** Deterministic per-member ids, a payload that inflates back
  to the same paragraphs, adopted work never re-published under our name,
  unchanged payloads not re-pushed, work done while sharing was off offered on
  the next open, nothing at all with sharing off, and nothing at all to a
  server that never advertised a push limit.

## What translation covers

Cancel has to actually abort. Before the fix behind these checks, `cancel()`
cleared the queue but let in-flight requests "finish naturally" — so with two
of them running, paragraphs kept streaming in, the button stayed on Cancel,
and the model kept billing. The driver catches a run mid-stream and asserts
that `busy()` drops, no row is left queued or translating, half-streamed text
is cleared rather than left mid-sentence, the cancel is not counted as a
failure, **the sockets close**, no further chunks arrive, and Translate works
again afterwards. Reverting the fix turns 6 of the 12 red, including
`2 → 106` chunks after Cancel.

## Notes

- macOS only: `build.sh` assumes the Ninja generator and `/usr/bin/c++`, and
  each `run.sh` makes its fixture PDFs with `cupsfilter`.
- Everything it writes — the SQLite mirror, both caches, QSettings — goes under
  a throwaway `~/Library/Application Support/ai-reader-harness/PaperSyncHarness`
  that the run deletes at both ends. It never touches the real profile, and it
  never talks to a real server.
- Rebuild the app before re-running after a source change; the harness links
  the objects, it doesn't compile them.
- Any helper class added here must not use `Q_OBJECT` — there is no moc pass
  for this directory, only the app's aggregated one.
- To add a driver: `mkdir tools/qt-harness/<name>`, write `main.cpp` and a
  `run.sh` modelled on an existing one. `build.sh <name>` picks it up with no
  other changes.
