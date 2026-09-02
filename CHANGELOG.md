# AI Reader changelog

## v1.3.28 — 2026-09-02

### The comparison is a task, and says what it is doing
- Comparing papers is one model call that can take a minute or two, and
  the only sign of it was a spinner the size of a letter beside the button.
  It is a task now, like every other model call: it queues in the one
  manager, shows in the tasks pane as 论文对比 with how long it has been
  running, and can be stopped from there. The line beside the Compare
  button says the same thing while it runs — waiting for a slot, waiting
  for the model's first words, or how much of the answer has arrived so
  far — because the answer is a single structured call and nothing of it
  is visible until it is complete.
- A comparison started in one project is cancelled when the reader
  switches to another, rather than filing its answer under the wrong one.
- A dead Compare button now says why. It was disabled without a word when
  one of the ticked papers had no interpretation, or the quick read of one
  had given up for lack of text, or no model was configured; the line
  beside it names the reason. And ticking papers in a project the reader
  may only view no longer leaves the button dead: it used to learn that
  the basket had changed only from a store write that never happened.

### Tasks beside Settings; the prompts inside it
- The tasks button moved from the pane group to the app's own group at
  the right end of the toolbar, left of Settings, where the running count
  cannot be scrolled out of sight while something is running.
- The Prompts button and its dialog are gone. The four system prompts are
  a page of the Settings dialog now, with the same editors, and a
  "Restore built-in" button per prompt in place of the old dialog's Reset.

## v1.3.27 — 2026-09-01

### The comparison picks its papers where it compares them
- The compare window said "nothing selected" and pointed elsewhere — the
  library pane's menu, a statement's ⋯ menu — so a reader who opened it was
  told to choose and could not see where. The project's papers are now
  listed in the window itself with a box beside each; tick two or more and
  compare. A ticked paper that has not been interpreted yet is not a dead
  end either: the button that interprets the missing ones sits beside
  Compare. Papers put in the basket from an open paper that was never added
  to the project still show, as a chip with its own ✕.

### Every toolbar button has a name
- The tooltips were sentences — "Interpretation pane: relevance to this
  project, what to read first, and every statement traced back to the
  paper" — and a toolbar of twenty sentences is a toolbar nobody reads.
  Each button now has a name of one or two words on the first line and, where
  it earns one, a short clause under it. The pane buttons take their names
  from the same list the layout menu uses, so a pane is called one thing on
  the button and the same thing in the menu; the research pane is 课题汇总
  in both.

## v1.3.26 — 2026-09-01

### One list of papers, and the star finally means something
- The library pane and the "Interpret the library" window were two lists of
  the same papers, and neither could act on what the other was showing. A set
  filtered down to "everything the model called high-relevance" could be
  starred in the window but not opened from it; a paper opened from the pane
  could not be interpreted there. They are one list now, in the pane: the
  filters, the progress, the failures and their reasons, and every per-paper
  action — open it, interpret it, close-read it, star it, compare it, set it
  aside — on the row itself. The separate window is gone, and the running
  count moved onto the button that opens the pane.
- The star was a note to self with nothing downstream of it. Marking thirty
  papers for a close read was possible; reading them was not, because the
  batch could only ever run the quick interpretation. It runs the nine-part
  close reading now — "Close-read the starred" — a paper at a time, its nine
  parts paced against the concurrency limit, resuming at the part it stopped
  on and skipping papers a collaborator has already read. A paper reached
  without a quick interpretation gets one first rather than being failed for
  not having one: the nine parts are written against it.
- Papers can be put into the comparison from the library. Until now the only
  way in was the ⋯ menu beside a statement inside an open paper's
  interpretation, so comparing three papers meant opening three PDFs one
  after another. Right-click any row, or take everything the filters are
  showing in one go.

### The two panes that were filed as dialogs
- The research summary and the task list are panes — they dock, they resize,
  they are saved in layouts — but their buttons sat in the project group
  beside the dialogs, which is where a reader went looking for a window and
  found a pane instead. Both moved into the pane group with the other six.

### Fixed
- A batch stopped counting itself as busy while it was fetching and
  segmenting a paper, so on the last paper of a run the progress bar and the
  Stop button vanished for the whole of that and the buttons that start a run
  came back to life on top of one.

## v1.3.25 — 2026-09-01

### The freeze instrumentation costs nothing to leave on
- Three things were being paid for on every operation rather than only when
  something was slow, and none of them earned it. The receiver's object name
  was read for every event the thread delivered — a string built hundreds of
  thousands of times a session, and not once did it identify anything the
  class name and its parent's had not. A phase marker allocated a name to
  file its time under even when it had taken no time at all, which is almost
  always. And the watchdog woke twenty times a second where ten is plenty to
  notice a freeze worth reporting.
- What stays, stays on: the startup phases, the freeze watchdog, the marked
  steps, the event timer, the per-frame timing that arms itself after a long
  freeze, and the lines a sync and a download write about themselves. Between
  them a single launch now names anything that holds the window for more than
  a third of a second, and says where its time went. That is the difference
  between "it hangs sometimes" and a fix.

## v1.3.24 — 2026-09-01

### Switching papers stops rebuilding the same things twice
- The interpretation pane was built three times for one click. Switching
  papers empties the interpretation and then fills it from the project, and
  each half announced itself separately, so the pane tore its tree down and
  built it again — empty, then full — before settling. Those are one change
  as far as anyone watching is concerned, and it is announced once now.
- Putting a paper's translations back was three notifications per paragraph.
  A fully translated paper is hundreds of paragraphs, and it happened every
  time the paper was opened. They go back in one.
- The step where the viewer reloads the document and re-lays out its page
  table is now named in the log too, so what is left of a switch has nowhere
  to hide.

### A freeze's breakdown belongs to that freeze
- The per-phase breakdown was cleared when it was reported rather than when
  an event began, so the marked work that ran between two slow events was
  charged to whichever one was reported next. The parts added up to more
  than the whole, which is how the error showed itself. Each event now
  starts from zero.

## v1.3.23 — 2026-09-01

### A maximized window opens maximized, without the small one first
- The window was shown the moment QML finished building it, at the size
  written in the QML, and last session's geometry arrived a moment later.
  That gap is the small window that appears and jumps.
- It now stays hidden until the geometry and the maximized/full-screen
  state have both been put back, and is shown into that state. The first
  frame is already the right shape.

### A pane nobody has opened is not built
- All nine panes were built at launch whether or not they were showing, and
  every one of them was paid for in the seconds before the window appeared.
  A reader with four panes open was still paying for the folder browser,
  the outline, the research pane, the task list and the chat.
- They are built the first time they are opened, and kept after that, so
  toggling one costs nothing the second time. With five put away, the
  window carries 655 items instead of 1162.
- The drag-to-reorder grip, the saved layouts and the pane widths all key
  off the same names as before, so nothing about arranging panes changes.

## v1.3.22 — 2026-09-01

### Interpretations are counted without being read
- The breakdown in the field log left nothing to guess at. Every freeze was
  the same two lines, and the residual was zero: 2881 ms "reading every
  object of one kind out of the database", 1178 ms "decoding every
  interpretation in the project", then 1899 + 772, then 955 + 394. Nothing
  in QML, nothing in the scene graph, nothing in Qt.
- A stored interpretation carries the reading itself plus the last two
  versions of it — several hundred kilobytes of JSON. Ten papers is
  megabytes. And almost everything that asks about interpretations wants to
  know which papers have one, whose it is and when it was written, not what
  it says: how many digests exist (the research pane binds that, so it is
  re-asked on every change), whether a collaborator has one for this paper,
  what the set of them hashes to. Each of those parsed the lot.
- There is now a side index of exactly that metadata, kept in step as
  objects arrive — the same thing paper_data has had all along, for the
  same reason and in the same shape. Counting, indexing and hashing read it
  and never touch a payload.
- What genuinely needs the readings decodes them through a cache stamped
  with each one's updatedAt, so a sync that changed one interpretation
  costs one decode rather than a project's worth.
- Measured against a 200-paper project holding 28 MB of interpretations:
  one change went from 416 ms to 13–23 ms, and counting the digests from
  47 ms to nothing.

## v1.3.21 — 2026-09-01

### A sync that moved nothing stops costing a second and a half
- With the probe reporting honestly, the field log named it: every freeze
  was "a sync landing", 1.4 seconds of it, over and over. And the syncs
  themselves were arriving every two seconds instead of every thirty —
  including this one: `sync: 0 objects in, 0 out, 1338 ms`. A sync that
  moved not one object still told everything that listens to reload, and
  everything that listens reloads the library, re-reads the project's
  interpretations and re-runs the panes bound to them.
- Nothing came in and nothing went out means there is nothing to react to,
  so nobody is told any more.
- Every local write used to ask for a full sync of its own, immediately.
  Publishing a paper's paragraphs and its translations, storing an
  interpretation and its notes — each was a round trip, and each round trip
  ended in that work. Writes now wait a second and a half for their
  neighbours and go together.
- The receivers of a sync name themselves now too, so what is left of that
  1.4 seconds has somewhere to show up.

### A slow event is reported as a breakdown, not a single name
- The probe used to name the longest marked step inside a slow event, which
  is the right answer only when one step dominates. It now lists where the
  time went, biggest first — and, last, how much of it no marker covers at
  all. That remainder is the number that says to stop marking and go look
  at QML, at the scene graph, or at Qt.
- The times are exclusive: a marked path that calls another marked path is
  not charged for the inner one, so the lines add up instead of nesting.

## v1.3.20 — 2026-09-01

### The window stops freezing every time the app talks to the server
- The event probe finally named it: every freeze was one MetaCall delivered
  to QNetworkReplyHttpImpl — 4075 ms for the first, then about 1400 ms
  each. No marked step was inside any of them, the replies were all under
  half a megabyte, and one of those syncs moved a single object. The time
  was not in parsing, applying, drawing or laying out. It was in starting
  the request.
- Qt asks the operating system which proxy to use for every request, and it
  asks on the thread the reply belongs to, which is the one drawing the
  window. On Windows with "automatically detect settings" on, that question
  is WPAD: a DHCP/DNS round trip, and on some networks a PAC fetch. It
  blocks whoever asked, for as long as it takes.
- The answer cannot change often enough to be worth asking twice. It is now
  asked once, off the GUI thread, at launch — several seconds before the
  first request — and cached for the session. A request that somehow beats
  the lookup waits for it, once, with a ceiling; everything after is
  instant.
- The lookup logs how long the system took, so a machine where this was the
  whole problem says so in one line.

### The freeze log stopped lying about what it caught
- The event probe timed events on every thread, so a background page build
  minding its own business looked like a frozen window. It only counts this
  thread now.
- It also read the phase after the event, by which time every marker inside
  had been unwound and the answer was always "idle". It now reports the
  longest marked step that ran while the event did, and says plainly when
  nothing marked was inside — which is what pointed at Qt's own code here.

## v1.3.19 — 2026-09-01

### Everything that can hold the GUI thread now says so
- The event timer's threshold drops to 150 ms and its line carries more:
  the kind of event, the receiver's class, its object name, its parent's
  class, and the phase the app thought it was in.
- A freeze now records how many events the thread delivered while it lasted.
  One means a single slow slot — and the line above it names that slot.
  Thousands mean a storm of small work. None at all means the thread was
  blocked outside event delivery entirely, waiting on a lock, a thread or a
  file, which is a different bug and could not be told apart before.
- Twenty more paths name themselves while they run: reading every object of
  one kind out of the database, decoding the project's interpretations,
  counting them, rebuilding and filtering the paper list, reloading the
  library, the comparison basket, switching the outline and the chat to a
  paper and putting them back, reading and writing the paragraph and
  translation caches, hashing a PDF, saving a downloaded one, packing an
  interpretation to store, and writing the settings file.
- A sync logs what it moved and how long it took, and a downloaded PDF logs
  its size and transfer time. Work that loops is invisible when each turn of
  the loop is under the freeze threshold; a line per turn makes the loop
  itself obvious.

## v1.3.18 — 2026-09-01

### The log names whatever froze the window, marked or not
- Hand-placed markers only cover the paths someone thought to mark, and
  three rounds of field logs have now come back saying the freeze was in
  none of them — while the frame timing ruled out layout (`polish` never
  over 7 ms) and drawing (`blockedForSync` never over 32 ms) just as
  firmly. The thread was busy with something nobody had a name for.
- Every event the GUI thread delivers now passes through a timer. One that
  takes a quarter of a second or more is logged with the kind of event it
  was and the class of the object it was delivered to — and a queued signal
  arrives as a MetaCall on the object about to run the slot, so a slow slot
  names its own class whether or not anyone marked it.
- It costs two clock reads per top-level event and builds nothing unless
  the event was actually slow.
- The push side of a sync is marked too: reading the dirty rows out and
  serialising them is up to six megabytes of JSON built on this thread,
  once per batch, and it was the last unnamed step in a sync.

## v1.3.17 — 2026-08-31

### A sync no longer freezes the window while it lands
- Per-frame timing from the field settled what this was: `polish` never
  went over 1 ms and `blockedForSync` never over 32 ms, so neither laying
  the scene out nor drawing it was slow. The GUI thread simply did not run
  Qt Quick at all for 4.2 seconds — it was somewhere else. And the freezes
  that came later were 30 seconds apart, which is the sync poll interval.
- A pulled page asked for two hundred objects. That is the right number for
  a library entry of a few hundred bytes and the wrong one for a
  `paper_data` artifact, which is a whole paper's paragraphs or a whole
  paper's translations — so a page could be tens of megabytes of JSON,
  parsed and written to the database in a single turn of the event loop. A
  page is twenty-five objects now.
- What a page carries is applied a slice at a time, about a frame's worth
  per turn, handing the event loop back in between. The sync takes the same
  time overall; the window stays usable throughout it.
- Parsing a reply is the one part that cannot be sliced, so it says so in
  the log while it happens, and any reply over half a megabyte is logged
  with its size.

### The tour's spotlight follows what it is pointing at
- The coach mark maps its target's position through every ancestor, but a
  binding depends only on what it reads — the target's own geometry. An
  ancestor moving (the splitter settling, the toolbar scrolling, a pane
  appearing) moved the target on screen without touching any of that, so
  the spotlight and its card stayed where the layout used to be.
- On a slow start that meant the whole tour: it opened against a layout
  that had not finished, nothing it depended on changed afterwards, and the
  card sat off-screen until the user resized the window and forced the
  binding to run again. It now re-checks four times a second while it is
  open, which costs nothing and cannot go stale.

## v1.3.16 — 2026-08-31

### A freeze is logged with when it began, and how big the scene was
- The line now carries the moment the freeze started as well as the moment
  it ended. "Frozen for 8374 ms" reported at t+22028 began at t+13654, and
  matching a freeze to the frames inside it needs both ends.
- When per-frame timing arms, the window's size and the number of items it
  is carrying go in beside it. Laying a scene out and handing it to the
  renderer are both walks over every item, so when the question is why a
  frame took seconds, the size of the thing being walked is the first
  number worth having — and it is the one that tells a slow machine apart
  from a scene that got too big.

## v1.3.15 — 2026-08-31

### A long freeze now turns on per-frame timing by itself
- The field logs said the window froze for four seconds and that none of
  the expensive C++ paths were on the thread at the time — so the time was
  going into QML or into drawing, and nothing in the log could say which.
- After a freeze of a second or more, the app now switches Qt's own
  per-frame timing on for fifteen seconds. Each frame then records
  `polish` — this thread laying the scene out — and `blockedForSync`,
  which is it waiting on the renderer. Those two lead to opposite fixes,
  and there was no way to tell them apart from outside.
- It stays off until then, because it costs a line per frame, and it turns
  itself off afterwards. Freezes come in runs, so arming on the first one
  still catches the rest. An explicit QT_LOGGING_RULES is left alone —
  that belongs to whoever set it.

## v1.3.14 — 2026-08-31

### A freeze in the log now says what the app was doing
- v1.3.13 could tell you the window froze for four seconds. It could not
  tell you what for: everything after startup was reported as "idle",
  which is the one answer that is no use. The phases now cover the work
  that happens while the app is running, not just while it is starting.
- The paths that can hold the GUI thread name themselves while they are on
  it: opening a paper, loading the PDF and its paragraphs, putting a
  paper's translations back, loading the interpretation from the project,
  indexing the project's interpretations, packing a paper's work to share,
  a sync landing, and taking in what a sync brought.
- A freeze that still reports "idle" is now information rather than a gap:
  it means the time went into QML or into drawing, not into any of the
  work above.

## v1.3.13 — 2026-08-31

### The log now says where a freeze went
- "It hangs on startup" is the report we could not act on: the log recorded
  that the app started and nothing about how long any part of it took. It
  now stamps every phase of startup — settings and the keychain read, the
  folder pane, the sync and library services, building the QML scene,
  reopening the papers that were open — against a clock started at the top
  of main().
- A watchdog on the GUI thread runs for the whole session. It is a timer
  that expects to be woken twenty times a second; when it is late, the
  thread was busy and the window was frozen for exactly that long. Anything
  over 300 ms is written to the log with how long it lasted and what the app
  was doing at the time. That covers freezes after startup too — switching
  papers, a sync landing — which until now left no trace at all.
- Both cost nothing to leave on, and they are on: a user who hits a freeze
  once has already collected the evidence, without having to reproduce it
  under a special build.

## v1.3.12 — 2026-08-30

### Switching papers no longer freezes the window for a second at a time
- QtPdf funnels every call into PDFium through one global lock. Opening a
  paper used to start a background sweep that built the selection index —
  text, line boxes and link rectangles — for every page of the document,
  and that sweep held the lock. Switching to another paper has to load the
  new document and lay its pages out, which needs the same lock, so the
  switch queued behind the sweep. Measured on a real paper: up to 1.1 s of
  frozen window per switch, and worse the faster you switched, because the
  sweep for a paper you had already left kept running. It is now 80 ms.
- The sweep is gone. Selection structures are built for the pages the
  reader is actually on — the current page and a couple either side — one
  page at a time, on a thread that keeps the document open instead of
  re-parsing the file for every batch. A paper you flip past is never
  parsed at all, and the request is held for a fraction of a second so
  scrolling fast or flipping through tabs does not build a trail of pages
  nobody looked at.
- Leaving a paper now stops the work that belongs to it. Both the
  selection builder and the paragraph clusterer check between pages
  whether anyone still wants the answer, so a switch waits at most one
  page rather than a whole document.
- Clicking somewhere the index has not reached yet still works exactly as
  before: that one page is built on the spot.
- The paper's id — a hash over the first four megabytes of the file — is
  remembered per file, size and timestamp. Switching between two open
  papers was re-reading four megabytes off disk each time to answer a
  question whose answer had not changed.

### The interpretation pane stops rebuilding all nine parts of a close reading
- A close reading is nine parts and only the first is expanded to begin
  with, but all nine were built anyway — and each one carried all nine
  part layouts when it needed one, so the pane held 81 subtrees where 9
  would do. Every time a part landed from the model, all of it was thrown
  away and built again.
- Now a part is built when it is first expanded, and each part carries
  only its own layout. The pane holds a fifth of the items it used to.
  Showing an interpretation went from 360 ms to 100 ms, and the rebuild
  that happens every time a part lands — nine times while a close reading
  is being written, and again on every sync — from 57 ms to 11 ms.
- Expanding a part for the first time now costs about 60 ms, which is the
  point: the work happens where the reader asked for it, once.

## v1.3.11 — 2026-08-30

### The interpretation's output length can be set as high as the model's
- Settings → Interpretation → Max output tokens stopped at 64000, while the
  same field on the model page went to 131072. 64000 was a round number
  picked the day the feature landed, not any model's limit, and it was the
  wrong field to cap lower than the rest: a close reading is nine separate
  requests and the longest thing the app asks a model for. Both fields now
  end at 131072, and the ceiling is one named constant rather than the same
  literal written in three places — the spin box, the setter, and the load.
- A value above the ceiling is still refused rather than stored, and a
  setting synced down from another machine goes through the same check.

## v1.3.10 — 2026-08-29

### The toolbar no longer runs off the edge of a narrow window
- The toolbar is one long row, and a window narrower than that row used to
  lose the right-hand end of it — settings, help, the account, half the
  project group — drawn past the edge of the window where it could be
  neither seen nor clicked. It now gives ground in three steps instead.
- The readouts go first, because they are the only things in the toolbar
  that cannot be clicked. The model name steps out, the account keeps its
  icon and drops its name, the project list narrows, and the page counter
  loses its words and then leaves. The "LLM not configured" warning is not
  a readout and stays at every width: it is the reason half the buttons
  above it are dead.
- What is still too wide scrolls sideways, with an arrow at each end of the
  row that shows how much is off in that direction, dimming when there is
  nothing left that way. The wheel scrolls it too — a plain vertical wheel,
  since that is the only wheel most mice have.
- The app's own group never scrolls: settings, prompts and the tour stay
  pinned against the right edge at every window size, one click away.
- Where the toolbar decides to shed a readout is measured, not guessed. It
  remembers what the row wants with everything spelled out and what it
  wants with the words gone, and compares those to the room it has, so the
  same window is roomy with no paper open and tight with a paper open, an
  account signed in and a project chosen — and a step is undone against the
  very number that called for it, which is what keeps it from flickering
  between two answers.
- The getting-started tour spotlights toolbar buttons, and a button
  scrolled off the end cannot be pointed at, so each step now brings its
  own targets back into view before the spotlight looks for them.

## v1.3.9 — 2026-08-28

### Emptying the API key no longer bends the settings page
- The red note under an empty API key sat in the model page's two-column
  grid as a cell of its own, so the moment it appeared every row below it
  shifted by one: temperature, max output tokens and context window each
  ended up with the label on the right and the field on the left. The note
  now lives inside the key field's own cell, and the rows under it stay put
  whether it is showing or not. The translation page has no such note,
  which is why only the model page bent.

### The app is in one language at a time
- The right-click menu inside every text box — undo, redo, cut, copy,
  paste, delete, select all — belongs to Qt, and Qt ships no Chinese for
  it, so a Chinese interface had one stubbornly English menu in it. Those
  seven words are now translated along with the rest of the app.
- The app itself speaks English and Chinese; Qt speaks dozens. A German or
  Japanese Windows used to get our English interface with German or
  Japanese dialog buttons and menus, because Qt has a catalog for those
  locales and we don't. The language is now resolved to one of the two the
  app actually has, and Qt's own strings follow it: a Chinese system is
  Chinese throughout, every other system is English throughout. Settings →
  Appearance → UI language still overrides both.

## v1.3.8 — 2026-08-26

### Formulas in translations are drawn, not spelled
- The PDF hands the app its formulas as flattened text, and the
  translation model faithfully rebuilds them as $…$ LaTeX — which the
  paragraph view then showed as raw dollar signs. Those spans are now
  rendered as real formulas, inline with the translated sentence, in the
  same ink and size as the words around them; dark mode gets its own ink
  instead of near-black on near-black. Display math ($$…$$) centers on
  its own line.
- A span the renderer cannot parse stays as its literal text, a price in
  dollars is never mistaken for math, and a translation with no formulas
  renders exactly as before.

### The settings dialog says when the key is missing
- An empty API key field now carries a red note explaining that saving
  like this leaves the toolbar showing "LLM not configured" — and that
  keys live only on this machine, never in the account sync. Configuring
  a new machine lands exactly there, with everything else synced and
  only the key missing.

## v1.3.7 — 2026-08-26

### Every way of translating now resolves its settings the same way
- There were four ways to start a translation — the whole paper, retry
  the failed paragraphs, right-click a single paragraph, translate a
  selection — and one of them was special: retrying kept whatever
  endpoint and key the failed run had used, so fixing the settings and
  clicking retry sent the retries to the old, broken endpoint. Every
  entry point now resolves the current settings before it sends anything.
- Changing the model settings now takes effect immediately, even while
  paragraphs are in flight: new requests go out on the new endpoint, and
  the ones already in the air finish quietly on the old one instead of
  being cut off.
- The settings dialog's translation section used to re-implement the
  fallback rules ("blank means: same as the main configuration") in its
  own way, so what its Fetch button probed could differ from what a
  translation actually used. Both now go through the same single
  resolver, and the "Translation will run on" line previews exactly what
  a paragraph will be sent to — as you type, before saving.
- The key now follows the endpoint it belongs to: if the translation
  section points anywhere other than the exact server the main
  configuration uses, the main key stays home instead of being sent to a
  third party. A URL differing only by a trailing slash no longer counts
  as "somewhere else", and stray whitespace around a pasted key is no
  longer part of the key.
- Errors from the model now say which server answered — "HTTP 401 from
  api.deepseek.com: …" — so a request that went to the wrong place is
  visible at a glance instead of looking like a bad key on the right one.

## v1.3.6 — 2026-08-26

### The zoom readout
- The percentage in the toolbar used to hide two gestures: one click
  fitted the page to the window width, a double click went back to 100%.
  Telling those apart meant every single click waited a quarter of a
  second before doing anything. The fit-to-width button next door already
  does the first job, so the readout now does only the second: one click,
  straight back to 100%, no pause.

## v1.3.5 — 2026-08-25

### The saved-layouts menu
- The menu that lists the saved layouts drew the tick mark on top of the
  layout's name, so the one arrangement you could not read was the one you
  were using. The tick and the name no longer share the same space.
- Every row also carried a small ✕. Everywhere else in the app an ✕ closes
  something; here it deleted the layout for good. A row now does one thing
  — clicking it puts that arrangement back — and renaming and deleting have
  moved into a **Manage layouts…** window, where the buttons say in words
  what they do and deleting asks first, on the row, naming the layout it is
  about to remove.
- That window also shows which panes each saved layout actually opens, so a
  layout can be told apart by what it does rather than only by the name
  somebody gave it months ago.

## v1.3.4 — 2026-08-25

### Pane layouts can be saved and switched
- Arrange the panes however you like, save that arrangement under a name,
  and switch between saved layouts from the toolbar.
- A layout remembers which panes are showing, how wide each is, and the
  order they sit in — widths are saved as a share of the window, so a
  layout saved on a big monitor still works on a laptop.
- Layouts travel with your account, so the same arrangements are on your
  other computer. Window size and position are deliberately not part of a
  layout.

### The library now belongs to the account
- Signing out empties the library, the search and the interpretation views
  instead of leaving the previous person's papers on screen. Nothing is
  deleted, and anything not yet synced is still there and still pushes
  when that user signs back in.
- A second account on the same computer sees its own library, not the
  first one's.
- Being merely offline is not signing out: the person who owns the
  library still sees it with no network.

### The browser is left on a proper page after signing in
- It is now a card with the app's own mark, a green tick, the user's
  name, and a button that closes the page — with a line telling them to
  close the tab themselves when the browser refuses to.
- Failures use the same page with a red mark and say what to do.

### Fixes
- The window title and the tab used to show a 64-character checksum
  whenever the paper's title could not be looked up — when signed out, or
  before the project's papers had come down. They say "Untitled paper"
  now.
- Switching projects with the library pane closed left it listing the
  previous project's papers when it was reopened; creating a project
  could leave every view pointing at the old one.
- Selected text in the paragraph pane was painted in a heavy opaque blue;
  it now uses the same translucent highlight as the PDF page, so the same
  sentence looks the same in both.
- The translation half of a paragraph could not be selected or copied at
  all. It can now.
- In the task list, the button that stops a task sat beside the task's
  name, where it read as "delete this row". It is at the end of the
  progress bar now, next to the work it stops.

## v1.3.3 — 2026-08-25

### Pane buttons show whether the pane is open
- Every toolbar button that shows or hides a pane now sits pressed while its
  pane is open and raised while it is closed — including the two newest ones,
  the project analyses and the task list, which had no pressed state at all.
- They also stay right when something else opens a pane: asking the AI about
  a selection opens the chat pane, and the chat button now knows it.

### The selected tab sits in the middle of its bar
- The blue pill behind the current tab was 3 pixels from the top of its
  frame and 7 from the bottom, which read as slightly wrong without being
  obviously wrong. The bar is now sized from the tab it frames, so the two
  gaps are equal.

## v1.3.2 — 2026-08-25

### Project buttons stop disappearing
- Members, the research profile, batch interpretation, comparison and the
  project analyses used to vanish from the toolbar when you were not signed
  in or had no project chosen — which reads as "that feature is gone", not as
  "that feature needs something first". They stay put now, greyed, and say
  what they want when you hover them.
- Clicking one does the missing step rather than nothing: it signs you in, or
  opens the project list.

## v1.3.1 — 2026-08-25

### The folder pane could freeze the app, and did
- Browsing a folder of PDFs used to walk that folder's whole subtree, on the
  spot, for every row on screen. On a big folder — or one on a network drive
  or a synced cloud folder — that froze the window, and because the pane's
  state was remembered it froze again on the next launch.
- Worse, the walk could never finish at all: a shortcut inside a folder that
  points back at one of its own parents (common in Windows user profiles and
  cloud-drive mounts) sent it round in circles for ever.
- The counting now happens in the background: rows appear at once and their
  numbers fill in behind them. A loop is detected and stepped over, a folder
  with an absurd number of PDFs is counted up to a limit and then says so
  rather than offering a "select everything" that would quietly act on part
  of it, and ticking a folder full of papers is instant instead of taking
  seconds.

### Settings are one file you can read
- Everything the app remembers used to live in the Windows registry, in a
  macOS preferences database, or in a Linux config file — three different
  things, and on Windows not a file at all, so somebody whose app would not
  start could not be talked through fixing it.
- It is now a single JSON file, in the same place on every platform, that can
  be opened, read, backed up, edited, sent to us, or deleted to put a stuck
  installation back on its feet. Its location is printed in the startup log.
- If that file is ever damaged, the app starts from defaults and keeps a copy
  of the damaged one beside it instead of overwriting it.

### Your settings can follow your account
- The settings that belong to you rather than to a machine — model and
  provider, prompts, target language, interface language, font sizes, token
  and concurrency limits, how Enter behaves in chat — now travel with your
  account, so a second computer picks up where you left off.
- The settings that belong to the machine stay on it: window size and
  position, which panes are open and how wide, where you were in each paper,
  which files are open, the folder you last browsed. A window position from a
  4K desktop would put the window off-screen on a laptop, so those never
  travel.
- API keys and login tokens never leave the machine — they stay in the system
  keychain and are not part of what syncs.
- A machine you have already configured is not overwritten by what the
  account holds: only settings you have not touched here are adopted, and
  what you changed here is pushed. It all keeps working with no account and
  with no network — what is on this machine is always what the app obeys.

## v1.3.0 — 2026-08-25

### One queue for everything that talks to a model
- Translating a paper, splitting it into paragraphs, extracting its contents,
  reading a page as an image, the quick interpretation and the nine-module
  close reading, interpreting a whole library in batch, and each of the seven
  project-wide analyses are now all submitted to one task queue instead of
  starting wherever they were clicked.
- Two runs of the same work on the same paper can no longer overlap — the
  second is refused rather than started, which is what stops one paper's
  answers from landing on another. Different papers still run side by side.
- A budget limits how many model calls run at once; the rest wait in the
  order they were submitted and are admitted as earlier ones finish.

### A Tasks pane
- A new pane — toolbar button, dockable like the other panes, showing a
  count while anything is running — lists what is running, waiting and
  finished, with a progress bar, how long each has been going and an
  estimate of how much longer.
- Tasks can be cancelled individually or all at once, failed ones retried,
  and finished ones cleared.
- A task that is merely queued is shown as such in the pane that started it,
  so a click never looks ignored.

### Closing the window no longer loses work quietly
- Closing while work is in flight now asks first, and lists what is
  unfinished.
- Close anyway and the unfinished work is written down; the next launch
  offers to pick it up where it left off, or to throw it away. Work that
  cannot honestly be restarted is never offered.
- A library analysis that produced nothing now reports a failure instead of
  silently stalling the queue.

## v1.2.19 — 2026-08-25

### Project analysis is a pane, not a window
- Categories, the research map, consensus, the timeline, coverage, openings
  and next steps now dock alongside the paper instead of covering it, so a
  category can be read with the paper it names open next to it. It drags to
  any slot in the layout like the other panes, and the toolbar button toggles
  it.
- One **Generate all** button at the bottom now runs all seven, queued, and
  turns into the way to stop the run. Each tab keeps its own Generate for
  rewriting a single thin answer.

### The app's folders carry the brand
- Settings, the library database and the caches used to live under
  `ai-reader/AI Reader` — a repository name where the brand belongs, and a
  space that every script downstream had to quote. They are `D2S/AIReader`
  now.
- Updating moves what is already there: the library, the paragraph caches,
  the downloaded PDFs and every setting come across on first launch, once,
  and the API key in the system keychain is untouched. A second launch
  changes nothing, and work done after the move is never overwritten.

## v1.2.18 — 2026-08-25

### A toolbar you can read
- Twenty-five labelled buttons in one row had stopped being a toolbar. They
  are now icons, grouped by what they act on — the file, the view, this paper,
  the panes, the project, the app — with a separator between groups and the
  words moved into the tooltips.
- The icons are one drawn family, tinted to the theme, so they read the same
  in light and dark.
- Where a button carries a number it keeps it: the zoom level, how many papers
  a batch has done, how many are lined up to compare, the paragraphs that
  failed to translate.

### Splitting a paper again asks first
- Pressing Segment on a paper that is already split now says how many
  paragraphs it has, how many are translated, and that splitting again
  replaces the division — translations are tied to the old paragraphs and
  mostly will not match afterwards. First-time segmentation just runs.

## v1.2.17 — 2026-08-25

### A paper reopens where you left it
- The reading position is remembered per paper and restored when you come
  back to it — including a paper opened from a project, whose file on disk is
  named by a checksum, since the position is keyed to the paper rather than
  the file. It stays on this machine: where you were on a laptop screen is
  not where you were on a monitor.

### Home, End, Page Up, Page Down
- They now scroll the PDF, the paragraph pane, the interpretation pane and
  the dialogs, and **Space / Shift+Space** page forward and back the way a
  reader expects. Typing in a text box keeps them for editing.
- Click a pane to give it the keys.

## v1.2.16 — 2026-08-25

### Switching papers is quick again
- Opening another paper asks the project for that paper's interpretation, and
  that lookup was searching every interpretation in the project — parsing the
  lot, including the close readings, which run to tens of kilobytes each. On a
  200-paper library it cost 19 ms every time you switched, and it grew with
  the library.
- An interpretation of your own is now found directly (its identity is derived
  from the project, the paper and you, so there is nothing to search for), and
  a collaborator's is found through an index rebuilt only when something
  changes. Same library: 0.8 ms.

## v1.2.15 — 2026-08-25

### The window title names the paper, not its hash
- A paper opened from a project plays out of the shared cache, where the file
  is named by a checksum. The tab bar already looked the real title up in the
  library; the window caption still read the file name off disk and showed the
  hash. It asks the same place now.

## v1.2.14 — 2026-08-25

### The comparison basket follows your account
- The papers you line up to compare were kept in this machine's settings
  file, so they never reached your other machines. They are stored in the
  project now, like everything else the interpretation layer produces — and
  an existing basket is carried over the first time this version runs.
- Everything else already synced: interpretations (yours and your
  collaborators', each attributed), the project-wide analyses, the research
  profile, your notes, and the *read closely* / *set aside* marks, which ride
  on the paper itself and so are visible to the whole project.

## v1.2.13 — 2026-08-24

### The old Summary pane is gone
- The structured **Interpret** pane replaces it completely — it answers the
  same questions and traces every statement back to the paper — so the older
  free-form summary, its prompt tab and its "Shared" window have been removed
  rather than left as a second, worse copy. Nothing is deleted from your
  projects; the old shared summaries simply no longer have a window of their
  own, and interpretations are shared with attribution in their place.

### Toolbar
- **A Paragraphs button** shows and hides the paragraph pane, like every other
  pane, and it remembers.
- The status line says **which page you are on**, not just how many there are.

### Settings
- **Interpretation and Chat are one page**, "Interpretation & chat" — between
  them they held three controls.

## v1.2.12 — 2026-08-24

### Changing the model takes effect now, not after a restart
- Switching provider used to keep talking to the previous one until the app
  was restarted. Every service cached its own connection and, on the next
  request, only patched its fields — but switching provider needs a different
  kind of connection entirely (Anthropic and OpenAI speak different
  protocols), and the address it re-applied was the stored one rather than the
  provider's own.
- There is now one place that decides when a connection has to be rebuilt, and
  it rebuilds on any change to provider, model, address or key — while
  refusing to do it under a request that is still in flight, which would leave
  that request waiting forever.

### A running batch is visible outside its window
- **Closing the "Interpret library" window has never stopped the run** — only
  Stop does. Now the toolbar button says so, counting *Interpreting 12/57*
  while it works, and the window says it too.

## v1.2.11 — 2026-08-24

### Usable over Remote Desktop
- **The app now notices it is running in a remote session** (Windows) and
  draws in software instead of through the graphics card. There is no card to
  draw with inside RDP: the normal path renders every frame as one full-window
  picture that then has to be encoded and sent, which is the opposite of what
  the protocol is good at. Software drawing repaints only the parts that
  changed.
- **The small hover and open/close animations are switched off** in that mode.
  A 120 ms colour fade is nothing locally and is a stream of full-window
  frames over a remote desktop.
- Settings → Appearance → **Remote desktop** to force it on or off; it applies
  at the next start, and says when a session is drawing in software.

### Dragging a splitter
- **The interpretation pane now holds its layout while a handle is moving**,
  the way the paragraph, summary and chat panes already did — re-wrapping a
  whole interpretation on every mouse move was the expensive part. Measured on
  the paragraph pane: 100 drag steps cost 294 ms live and 2 ms held.
- The PDF view no longer re-lays out its page table on every pixel of a drag,
  settling once when the handle is released.

## v1.2.10 — 2026-08-24

### The endpoint follows the provider
- **Anthropic, OpenAI and DeepSeek each have exactly one address**, and it is
  no longer yours to type: pick the provider and the app talks to it. The
  Base URL box only appears for **openai-compatible**, which is what that
  entry is for.
- This was a real bug, not just clutter. A Base URL left behind by an earlier
  provider kept being used after switching, so every request went to the
  wrong server — with a model name that server had never heard of, which is
  what produced an unexplained *400* no matter what else was changed.
- **openai-compatible with no Base URL now counts as unconfigured** instead of
  quietly falling back to OpenAI's endpoint.
- Fetching the model list goes to the same place the requests will.

## v1.2.9 — 2026-08-24

### When the model refuses, it now says why
- A failed request used to read *"Error transferring … — server replied with
  status code 400"*, which names no cause and no cure. The provider's own
  explanation was being thrown away: on a streaming request the error body
  arrives through the same channel as the content, so by the time the request
  finished there was nothing left to read.
- The message now carries what the server actually said — *"HTTP 400: This
  model's maximum context length is 65536 tokens"* — for both the OpenAI-style
  and Anthropic clients.
- And where there is an obvious fix, it is spelled out: a context overflow
  points at *Settings → Model → Context window*, an output-length refusal at
  *Settings → Interpretation → Max output tokens*.
- Failures also log the request's shape (model, output budget, prompt size,
  which attempt) to launch.log, so a report from the field can be diagnosed
  without reproducing it.

## v1.2.8 — 2026-08-24

### Settings is a list of subjects, not one long scroll
- **Categories down the left, one page each**: Model, Translation,
  Interpretation, Chat, Appearance, Documents, Updates & privacy.
- Things that were never about the model have left the model page. Fonts, the
  UI language, the chat input, segmentation, sharing and update settings each
  live where they belong; the Model page holds the model.
- **The translation model has its own Fetch button**, and its own model list —
  it may be a different gateway entirely, so the two lists no longer overwrite
  each other.

### Tabs look like the rest of the app
- The tabs in the research window, the Interpret pane and the prompts dialog
  are now a segmented control in the same language as the buttons: the
  selected one filled, the rest quiet until hovered.
- The close-reading tab's button no longer sits flush against the tabs above it.

### Smaller things
- **Edit project**: Save and Close swapped places, so the confirming button is
  where it is in every other dialog. The research profile dialog follows.

## v1.2.7 — 2026-08-24

### The main model reads; only translation gets its own
- **The model at the top of Settings now does the reading** — interpretation,
  close reading, the project-wide analyses, chat, summaries and vision all run
  on it.
- **Translation is the one job that can be pointed somewhere else.** It runs on
  every paragraph of every paper, so a fast, cheap model usually serves it
  better. Settings → *Translation model*; leave a field blank and it uses the
  main configuration.

### The paper you are reading is highlighted again
- Both the **library** and the **folder** panes highlight the open paper. A
  paper opened from a project plays out of the shared cache under a
  sha256 name, so matching on the file path alone quietly failed for exactly
  the papers a collaborator had added — the library now matches on the paper
  itself, and the folder pane learns the file it came from.

### One look for every dialog
- Every popup in the app now wears the Settings/Prompts chrome: the same
  surface, the same dimmed backdrop, the same titled header, the same footer.
- **Every button is the same button** — the filled confirm and the outlined
  cancel from those two dialogs, at one height, everywhere. Text fields,
  drop-downs and spin boxes match too.
- Under the hood these are shared components rather than a style copied into
  each dialog, so the next dialog inherits the look instead of re-implementing
  it.

## v1.2.6 — 2026-08-24

### Interpretation survives a model that cannot do tool calls
- The interpretation asks for its structured answer by calling a tool, which
  is the reliable way to get one — but a gateway whose model was deployed
  without a tool parser rejects the request outright. That used to end the
  interpretation. It now asks again in plain prose and reads the JSON out of
  the answer, so a weaker or older endpoint still works.
- Fixed a crash-shaped hang: changing the interpretation model while a close
  reading was running could destroy the connection under it, and the run
  would wait forever instead of finishing or failing.

## v1.2.5 — 2026-08-24

### The whole interpretation layer speaks Chinese
- All 984 interface strings are translated, including everything the new
  Interpret pane, close reading, batch, comparison and Research views added.

### Three smaller things the design was missing
- **A citation now names its section, not just its page** — the tooltip and
  the export both read "4.2 Experimental setup, page 6".
- **Regenerating an interpretation is recoverable.** The previous version is
  kept; Interpret pane → ⋯ → *Restore the previous version* brings it back.
- **A category can be split**, not only merged: tick the papers that belong
  apart and they move into a new category beside it — one that is yours, so
  regenerating the category system leaves it alone.

## v1.2.4 — 2026-08-24

### What a whole project of papers adds up to
**Toolbar → Research** opens seven views over the papers you have interpreted.
All of them are written from those interpretations, never from the PDFs, and
all of them are shared with the project.

- **Categories** sorts the library along whatever dimensions actually cut it —
  research problem, method route, task, data, metric, contribution, main
  limitation, relevance — and a paper sits in as many as it belongs to. The
  system is yours: rename a category, lock it, merge two, add your own, drag a
  paper out of one. **Locked and renamed categories survive a regeneration**,
  and papers added later are placed into the system you confirmed rather than
  the system being redrawn under you.
- **Map** — the questions this library circles, the routes taken at each, which
  papers take which, and where each route stops.
- **Consensus** — what several papers independently support, what rests on one
  source, what is repeated but never independently tested, and what actually
  conflicts, with real conflicts told apart from ones that are only different
  conditions.
- **Timeline** — how the questions, methods, data and standards of evidence
  moved, and what has stayed unsolved throughout.
- **Coverage** — what this collection covers well and what it barely covers,
  which conclusions rest on thin evidence, what has never been compared
  fairly or tested outside a benchmark. The warning that this describes *your
  library and not the field* is pinned to the top and travels with the export.
- **Openings** — candidate research questions read out of the limitations and
  conflicts, each with the smallest experiment that would test it, what to
  measure against, the risks, and an honest difficulty and confidence. Each
  says whether it is a question a paper left open, a gap in this library, or
  something that still needs a literature search.
- **Next steps** — what to read closely, what to search for, what to
  reproduce, what to compare, what small experiment to run, what to ask a
  supervisor.

Every paper named anywhere in these views opens with a click. Each view keeps
its last few versions, so regenerating one is never a silent loss for the rest
of the project, and the whole thing exports as one Markdown report.

## v1.2.3 — 2026-08-24

### Interpretations come back out as Markdown
- **Export a paper** (Interpret pane -> ... -> Export as Markdown) writes the
  quick interpretation, the close reading and your own notes to one file --
  with the provenance intact. A statement that was the authors' still says so,
  a statement the model reached for and could not support still says *no
  evidence found*, and a citation that did not check out is exported as
  unverified rather than quietly dropped.
- **Export a comparison** writes a real Markdown table, with the
  comparability warnings above it.
- A whole-project report is available too, and it carries the sentence that
  matters: what this library does not cover is not the same as work that does
  not exist.

### The library list says what has been read
- A small dot next to each paper: interpreted (green when it came out highly
  relevant), or red when interpreting it failed or the PDF had too little
  text. Hovering says which.

## v1.2.2 — 2026-08-24

### Reading a paper closely, one part at a time
- **The Interpret pane has a second tab: Close read.** Nine parts — what the
  paper is, the background and terminology you need, the method step by step,
  the experiments, the contributions, a critical reading, the limitations,
  whether it could be reproduced, and what to do next.
- **Each part is written on its own**, so a part that came out thin can be
  rewritten in one request without touching the other eight, and a run you
  stop halfway still leaves you the parts it finished.
- The critical reading gives a verdict per dimension rather than general
  praise; the reproducibility part answers "unclear" where the paper says
  nothing, instead of inventing a repository link.

### Every statement can be pushed on
- **The ⋯ next to any statement**: explain it more simply, give me an example,
  walk me through the equation or figure behind it, challenge it, or just ask
  about it — each opens the chat with the question already written, against
  the paper you are reading.
- **Save as a note** keeps it in a Notes tab. Notes are yours: regenerating an
  interpretation never touches them.
- **Add to the comparison** puts the paper in a basket that survives restarts.

### Comparing papers
- **Toolbar → Compare** puts the papers you collected side by side across
  eleven dimensions — problem, hypothesis, method, inputs and outputs, data,
  baselines, metrics, results, contributions, limitations, reproducibility.
- **The warnings come before the table.** Papers measured on different data,
  with different metrics or on a different task cannot be ranked by their
  numbers, and the comparison says so plainly rather than letting a tidy grid
  imply a winner. Where an interpretation does not say, the cell says "not
  stated" rather than guessing.
- Comparisons are built from each paper's own interpretation, not from the
  PDFs, so comparing a dozen papers is one request.

## v1.2.1 — 2026-08-24

### Interpret the whole library at once
- **Toolbar → Interpret library** reads every paper in the project, without
  opening any of them. Papers whose PDF this machine has never seen are
  fetched from the project first, segmented in the background, and interpreted
  — the paper you are reading is left alone throughout.
- **Progress, and failures you can act on.** Each paper shows queued /
  working / interpreted / failed, a failure says what went wrong on the row
  itself, and *Retry the N that failed* runs just those again.
- **Then filter.** Once a library has been interpreted, filter it by how
  relevant the papers came out and by what the reading advice was — and act on
  what is left: mark everything shown for a close read, or set it all aside.
- **Nothing is interpreted twice.** A paper that already carries an
  interpretation — yours or a collaborator's — is skipped, so a project pays
  for each paper once.
- Segmentation done for a batch is kept, so opening one of those papers later
  is instant.

## v1.2.0 — 2026-08-24

### Papers can now be interpreted, not just summarised
- **A new Interpret pane** reads the open paper and answers the questions you
  actually have about it: what it is in one line, how relevant it is to *your*
  project, what to read first, what it claims, what it showed, and where it is
  weak.
- **Every statement says where it comes from** — the authors, an experiment in
  the paper, or the model's own reading. Nothing borrows the authors'
  authority by accident.
- **The little page chips are checked citations.** The model has to quote the
  paragraph it is relying on, and the app re-reads that paragraph before
  showing the chip. Click one and the PDF and the paragraph list both jump to
  it. A quote that is not in the paper is shown greyed as unverified, and the
  statement resting on it is knocked down to "AI reading" — so a confident
  sentence with nothing behind it cannot pass for a finding.

### A project can say what it is about
- **Toolbar → Profile** describes the research project: the goal, the
  questions, what is in and out of scope, where you are in the work. Every
  interpretation is written against it, so relevance and reading advice are
  about your work rather than about the field in general.
- **Change the profile and existing interpretations say they may be out of
  date** — as does re-segmenting the paper or switching model.

### Interpretations are shared, not repeated
- Interpretations are stored in the project like everything else, attributed
  to whoever ran them. A collaborator's reading of a paper shows up on your
  machine instead of costing you the tokens again.

### The interpretation can run on a different model from everything else
- **Settings → Interpretation model.** Reading a paper critically asks more of
  a model than translating a paragraph does. Point it at a stronger model — or
  a different endpoint entirely — and leave translation where it is. Anything
  left blank falls back to the main configuration.

### Chat input
- **The input box is taller** (and its height is now a setting).
- **Enter sends by default**, with Shift+Enter for a new line. If you prefer
  the old behaviour, Settings → Chat input switches it back to Ctrl+Enter.

## v1.1.24 — 2026-08-23

Carries v1.1.20 through v1.1.23 too — none of them were published, so an
update from 1.1.19 brings the lot.

### The paragraph pane says how much is translated
- **The Paragraphs header now reads "Paragraphs (382 · 300 translated)".** How
  far through a paper you are was only visible while a translation was
  actually running; now it is there whenever any of it is done.

## v1.1.23 — 2026-08-23

### The translation counter can't overrun any more
- **"translating 419/382" is fixed.** The progress numbers were a running
  tally that two different things added to: every finished paragraph counted
  one, and rehydrating a paper from its cache raised the count to however many
  paragraphs were translated. Start over on a paper whose old translations are
  still cached, switch away and back while it runs, and the same paragraph was
  counted twice. The numbers are now read off the paragraphs themselves, so
  they cannot disagree with what is on screen.

## v1.1.22 — 2026-08-23



### Translate asks what you meant
- **On a paper that is part-translated, Translate now offers a choice**:
  translate the paragraphs that have no translation yet, or start over and
  re-ask the model for all of them. Until now the button silently did the
  first and there was no way to do the second short of going paragraph by
  paragraph. A paper with nothing translated still just goes.

### Knowing which paper you are looking at
- **The library highlights the paper that is open**, the way the folder tree
  already did. Both panes now decide it the same way, by comparing canonical
  paths, so a symlinked folder or a differently spelled path doesn't stop the
  row lighting up.
- **A paper opened from the library is named, not hashed.** Those are served
  out of a content-addressed cache, so the file on disk is called
  `<sha256>.pdf` — which is what the tab and the Interpret pane were showing.
  They now show the library's title for it, falling back to the filename for
  anything the library doesn't know.

## v1.1.21 — 2026-08-23

### Two papers translate at once, and you choose how many lanes
- **Starting a second paper no longer means waiting out the first.** Per-paper
  runs landed in 1.1.20, but the queue behind them was still one line, so every
  paragraph of the first paper sat ahead of the second paper's first one. The
  requests in flight are now shared out across papers.
- **How many paragraphs go at once is a setting** — Settings → Model &
  language → "Paragraphs at once", 1 to 16, still 2 by default. It is one
  budget shared by every paper being translated, not a limit per paper, and
  changing it takes effect on a run already going.

### Settings
- The first block of settings — provider, model, keys, generation limits,
  languages — had no heading, unlike every block below it. It is now
  "Model & language".

## v1.1.20 — 2026-08-23

### Cancel stops the translation, and switching papers no longer does
- **Cancel actually cancels.** It used to clear the queue and then let the
  requests already in flight "finish naturally" — with two of them running
  that is indistinguishable from a dead button: paragraphs kept arriving, the
  button stayed on Cancel, and the model kept billing. It now aborts them.
  A half-streamed paragraph is cleared rather than left mid-sentence under a
  "translated" badge, and cancelling is not counted as a failure.
- **A translation run belongs to its paper.** Switching tabs used to kill it.
  Each paper now translates on its own: leave one running, read another, come
  back and it is done. Results reached while you were elsewhere go into that
  paper's own cache, so they are there when you return.
- **The pane counts only the paper you are looking at**, and says how many
  other papers are still translating so a run in the background isn't
  invisible. Closing a paper's tab stops it — otherwise a paper nobody has
  open would go on spending tokens.
- **Editing paragraphs still cancels that paper's run**, because a split or a
  merge renumbers the paragraphs the queued work was built from. It is
  recognised by the paragraphs actually changing now, not by "the block list
  emitted a signal", which is what made a plain tab switch look like an edit.

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
  in the project already segmented and its paragraphs are there.
- **Anything borrowed says whose it is.** A "split by name@example.com" chip
  sits in the Paragraphs header while the paragraphs are somebody else's, and
  every paragraph whose translation came from the project carries a "from
  name@example.com" badge of its own — translations are adopted one paragraph
  at a time, so one page can mix your own with a collaborator's. Both go away
  the moment you redo that work yourself.
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
  about 200 KB — and a paper too large to share (over 4 MB compressed, or half
  of what the server will take) is left alone rather than wedging the queue.
- **Nothing is shared until the server can take it.** The server now says on
  every pull how large a push body it accepts; against one that doesn't say,
  the app keeps segmentation and translations local. A rejected batch would
  have stalled the outbox for ordinary library edits too.

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
