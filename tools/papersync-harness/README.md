# papersync-harness

Checks for the paper-data sync path — the bridge that puts a paper's paragraph
segmentation and its translations into the research project
(`PaperSyncService`, `BlockCache`, `TranslationCache`, `SyncEngine`).

```sh
cmake --build build          # the harness links against these objects
tools/papersync-harness/run.sh
```

About a minute; several checks wait out the 15-second publish throttle. Exit
status is non-zero if anything failed.

## What it actually runs

Not mocks. `build.sh` reuses **every object file the app build produced**,
swapping out exactly two:

| dropped | replaced by | why |
|---|---|---|
| `src/main.cpp.o` | `main.cpp` | the test driver instead of the GUI |
| `src/AuthController.cpp.o` | `AuthStub.cpp` | the real one signs in through CAS in a system browser, which a headless run can't drive |

So `PaperController`, `TranslationService`, `SyncEngine`, `LibraryDb` and
`PaperSyncService` are the shipping code, wired together the way `main.cpp`
wires them. `FakeSync.h` is a `QTcpServer` speaking just enough of the backend
(`GET /projects`, `GET /projects/:id/sync`, `POST /projects/:id/push`) for the
real `ApiClient` to talk to, with an object store the test seeds and inspects.

Compile and link flags are read out of `build.ninja`, so there is nothing to
keep in sync with CMakeLists.

## What it covers

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

## Notes

- macOS only: `build.sh` assumes the Ninja generator and `/usr/bin/c++`, and
  `run.sh` makes its fixture PDFs with `cupsfilter`.
- Everything it writes — the SQLite mirror, both caches, QSettings — goes under
  a throwaway `~/Library/Application Support/ai-reader-harness/PaperSyncHarness`
  that the run deletes at both ends. It never touches the real profile, and it
  never talks to a real server.
- Rebuild the app before re-running after a source change; the harness links
  the objects, it doesn't compile them.
- Any helper class added here must not use `Q_OBJECT` — there is no moc pass
  for this directory, only the app's aggregated one.
