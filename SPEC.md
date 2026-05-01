# AI Paper Reader — Development Specification

## 1. Overview

An LLM-powered desktop/web application for reading and understanding academic papers. The tool ingests PDFs, produces summaries, presents bilingual side-by-side translation (English ↔ Chinese), generates a navigable chapter index, and exposes a chat interface where the model can call tools to extract specific paper fragments.

## 2. Goals & Non-Goals

**Goals**
- Reduce time-to-comprehension for English-language academic papers.
- Keep the original text available at all times alongside translations.
- Let users tailor the model, prompts, and translation style.
- Enable grounded conversation: the model answers from paper content, not memory.
- Native desktop application (macOS / Windows / Linux).

**Non-Goals**
- No collaborative/multi-user editing.
- No citation graph or cross-paper knowledge base.
- No mobile clients.
- No web-hosted multi-tenant deployment (single-user desktop app).

## 3. Core Functional Requirements

### 3.1 Paper Import (PDF) — Hybrid Text + VL Parsing via QtPdf
- Accept PDF upload via drag-drop or file picker.
- **Fast path (text)**: parse with **QtPdf** (`QPdfDocument`, backed by PDFium):
  - `QPdfDocument::pageCount()`, `getAllText(QPdfSelection)` for full-page text with rect-aligned word boxes.
  - `QPdfBookmarkModel` for any embedded outline (rare on academic papers, used as a hint).
  - `QPdfLinkModel` for cross-references and external links.
  - We synthesize structural blocks (paragraph / heading / figure-caption / table / equation) by clustering word boxes geometrically: vertical gap thresholds for paragraph breaks, font-size deltas for heading detection, "Figure N." / "Table N." regex for captions.
- **VL path (vision-language model)**: render each page to a high-DPI image via `QPdfPageRenderer::requestPage()` (PDFium-native, no external rasterizer). Used in three cases:
  1. Scanned/image-only PDFs where text extraction returns empty or garbage.
  2. Pages flagged as low-confidence by the parser (heuristics below).
  3. On-demand when the chat model calls `read_page_visual` (§3.6).
- **Auto-promotion heuristics** (page → VL path when **any** is true; all thresholds user-configurable in Settings → Parsing):
  - Extracted character count `< 50` (likely a scan or figure-only page).
  - "Broken word" ratio `> 20%` — a token is broken if it has no vowel, has > 2 non-ASCII letters in a Latin context, or has > 3 consecutive consonants outside a small whitelist.
  - Block-clusterer reports unclustered word boxes covering `> 50%` of page area.
- VL model is the same one configured for chat (must be vision-capable). The parser stores both extracted text and rendered page images, so VL calls are cheap to replay.
- Each imported paper gets a stable `paper_id` used by every downstream feature.
- Handle failure modes explicitly: encrypted PDF (`QPdfDocument::Error::IncorrectPassword`) prompts for password; malformed file surfaces a clear error.
- Persist raw text, page-aligned word boxes, structural blocks, and rendered page images.

### 3.2 Summary & Interpretation
- One-click "Interpret" produces:
  - Short abstract-style summary (≈150 words).
  - Structured deep-read: problem / method / contributions / limitations / open questions.
- **Prompt customization**: users can edit the prompt template per-paper or save reusable templates (e.g., "ML reviewer", "Beginner explainer"). Prompts support variables: `{{title}}`, `{{abstract}}`, `{{full_text}}`, `{{section}}`.
- **Model customization**: per-user settings for provider, model ID, base URL, API key, temperature, max tokens. Settings stored encrypted at rest. Built-in profiles for common providers (Anthropic, OpenAI, DeepSeek, local OpenAI-compatible endpoints).
- Summaries are cached per `(paper_id, prompt_hash, model_id)` so re-opening is instant; users can force regeneration.

### 3.3 Bilingual Side-by-Side Reading
- Layout: two panes — `QPdfView` showing the original PDF on the left, a QML `Flickable` + `Repeater` of translated blocks on the right.
- Translation is generated in a **single batched pass per logical block** (paragraph / heading / list-item), not per-sentence, to preserve discourse coherence.
- Each translated block carries the same `block_id` as its source on the left, plus the page index and bounding rect, so positions can be mapped both directions.
- **Synchronized scrolling**:
  - PDF → translation: bind to `QPdfView::pageNavigator().currentPage()` and the visible scroll offset; resolve to the topmost block whose bounding rect intersects the viewport, then `positionViewAtItem` on the translation `Repeater`.
  - Translation → PDF: on `Flickable.contentY` change, find the active translation block and call `pageNavigator().jump(page, location)`.
  - A re-entrancy flag suppresses the mirror handler during programmatic scrolls to break feedback loops.
- Translation of long papers is streamed block-by-block; the right pane renders blocks as they arrive with a per-block loading state.
- Untranslatable spans (formulas, code, citations like `[12]`) pass through unchanged. Detected via regex + structural-block kind before sending to the model.
- Cache translations per `(paper_id, block_id, model_id, prompt_hash)`.

### 3.4 Chapter Indexing & TOC
- Use the LLM to identify section/subsection boundaries from the parsed text (PDF TOC metadata is often missing or wrong for papers).
- Output schema:
  ```json
  {
    "sections": [
      {"id": "s1", "level": 1, "title": "Introduction", "start_block": 12, "end_block": 47, "children": [...]}
    ]
  }
  ```
- Render as a collapsible sidebar; clicking a section scrolls both columns to `start_block`.
- TOC generation runs once per paper and is cached; users can edit titles or merge/split sections manually.

### 3.5 Chat Platform
- Right-pane (or toggleable overlay) chat that shares context with the active paper.
- System prompt always includes paper metadata + TOC; full text is **not** auto-injected — the model retrieves it via tools (§3.6).
- Messages render Markdown + LaTeX math: cmark-gfm parses the message into an AST; a C++ walker emits typed QML items (`TextBlock`, `CodeBlock`, `MathBlock`, `ListBlock`, `TableBlock`, `ImageBlock`, `QuoteBlock`). `MathBlock` renders via MicroTeX to a `QQuickPaintedItem`; `CodeBlock` uses KSyntaxHighlighting.
- Streaming responses with cancel button (`QNetworkReply::abort()`).
- Conversation persisted per `(paper_id, conversation_id)`; users can branch / rename / delete.
- **Quote-selection → ask flow**: user selects text in either pane.
  - Left pane: `QPdfView` exposes the selection via `QPdfSelection` (bounding rects + text + page).
  - Right pane: a custom selection-tracking `MouseArea` over the translation `Repeater`.
  - A floating action button calls `chatService.attachQuote(...)`, attaching the selection (text + source side + block ids) to the next user message.

### 3.6 Tools Exposed to the Model

The chat model is invoked with tool-use enabled. Tools operate on the active paper.

| Tool | Purpose | Input schema | Returns |
|---|---|---|---|
| `get_section` | Fetch a named/indexed section | `{section_id: string}` or `{title_query: string}` | `{title, text, page_range}` |
| `get_block_range` | Fetch raw blocks by id | `{start_block: int, end_block: int}` | `{blocks: [{id, text}]}` |
| `get_user_selection` | Read the user's currently highlighted text | `{}` | `{text, block_ids, source: "en"|"zh"}` |
| `search_paper` | Keyword/semantic search over the paper | `{query: string, top_k?: int}` | `{hits: [{block_id, snippet, score}]}` |
| `get_figure_caption` | Retrieve a figure/table caption by label | `{label: string}` (e.g. "Figure 3") | `{caption, page}` |
| `read_page_visual` | Send a rendered page image to the VL model for figure/equation/table interpretation | `{page: int, question?: string}` | `{description, extracted_data?}` |
| `list_sections` | Return the TOC | `{}` | TOC JSON |

- Tools are pure read functions and idempotent.
- **Tool-call budget per conversation turn is user-configurable** (Settings → Chat → "Max tool calls per turn"). Default **30**, range 1–100. The budget bounds runaway loops, not cost.
- When the budget is hit, the model receives a system message ("tool budget exhausted, answer with what you have") rather than silently truncating.
- Tool results are appended to the conversation as standard tool-result messages so the model can chain reasoning.
- `get_user_selection` reads from a shared frontend state (the latest highlight); empty result if nothing is selected.

## 4. Architecture — Qt 6 + QML + QtPdf + MicroTeX (C++, single process)

```
┌──────────────────────────────────────────────────────────────┐
│  Single C++ Process (Qt 6)                                   │
│                                                              │
│  ┌────────────────────────────┐  ┌────────────────────────┐  │
│  │ QML UI (Qt Quick)          │  │ C++ App Core           │  │
│  │ ─ Reader: QPdfView (left)  │◄─│ ─ Paper service        │  │
│  │   + Translation column     │  │ ─ TOC service          │  │
│  │   (Flickable+Repeater)     │  │ ─ Chat service         │  │
│  │ ─ TOC sidebar              │  │ ─ Settings (QSettings) │  │
│  │ ─ Chat panel               │  │ ─ LLM client           │  │
│  │   ─ MarkdownView (custom)  │  │   (QNetworkAccessMgr   │  │
│  │   ─ MathBlock (MicroTeX)   │  │    + JSON, streams via │  │
│  │   ─ CodeBlock              │  │    SSE chunks)         │  │
│  │     (KSyntaxHighlighting)  │  │ ─ Search (SQLite FTS5) │  │
│  │ ─ Settings dialogs         │  │ ─ Storage (QtSql)      │  │
│  └────────────────────────────┘  └────────┬───────────────┘  │
│           ▲                               │                   │
│           │ Q_INVOKABLE / signals         │                   │
│           └───────────────────────────────┘                   │
└───────────────────────────────────────────┼──────────────────┘
                                            ▼
                                 ┌────────────────────────────┐
                                 │ App Data Dir (QStandardPaths)│
                                 │ ─ papers/*.pdf + *.png       │
                                 │ ─ db.sqlite (FTS5 indexed)   │
                                 │ ─ keychain (QtKeychain)      │
                                 └────────────────────────────┘
```

**Rationale**
- **Single C++ process.** No language boundary, no IPC, no Python runtime. QML talks to C++ via `Q_INVOKABLE` methods and signals on registered context objects.
- **QtPdf is the reader engine.** `QPdfView` for the left (English) pane gives native rect-based text selection, search, and GPU-composited scrolling. The right (Chinese translation) pane is a `Flickable` with a `Repeater` of `Text` blocks, sync-scrolled to the PDF's `currentPage`/`scrollPosition`.
- **MicroTeX renders math** in chat replies. Implemented as a `QQuickPaintedItem` subclass `MathBlock` exposed to QML — `MathBlock { tex: "\\mathbb{E}[X]" }`. Rendered output is cached per `(tex_hash, font_size, dpi)`.
- **Markdown in chat**: parsed C++-side with **cmark-gfm** into an AST, walked to emit a typed list of QML items (`TextBlock`, `CodeBlock`, `MathBlock`, `ListBlock`, `TableBlock`, `ImageBlock`, `QuoteBlock`) via a model bound to a `Repeater`. Math fragments are extracted from text nodes (`$...$` and `$$...$$`) before rendering.
- **Code blocks** use **KSyntaxHighlighting** (KDE Frameworks, Qt-native, no other KF deps required).
- **LLM client** is a thin C++ wrapper over `QNetworkAccessManager`. Anthropic and OpenAI APIs are JSON over HTTPS with SSE streaming — one provider class per API shape, switched by profile config. ~150 LOC each.
- **Search**: SQLite FTS5 via `QtSql` for keyword search. Embeddings are deferred to v1.1 (see §13).
- **API keys**: `QtKeychain` (cross-platform wrapper over macOS Keychain / Windows Credential Manager / libsecret).

**Trade-offs accepted**
- C++ learning curve if the developer is new to Qt/CMake.
- We hand-write the chat-pane markdown renderer rather than getting one off the shelf. Mitigated by cmark-gfm doing the parsing and a small QML component set doing the rendering.
- No semantic search in v1 (FTS5 keyword search only). Adding ONNX-Runtime + a small embedding model (e.g., `bge-small`) is the v1.1 path.

## 5. Data Model (sketch)

```
papers(id, title, authors, uploaded_at, file_path, status)
blocks(id, paper_id, ord, kind, page, text, bbox)
sections(id, paper_id, parent_id, level, title, start_block, end_block)
translations(paper_id, block_id, lang, model_id, prompt_hash, text, created_at)
summaries(paper_id, prompt_hash, model_id, body, created_at)
conversations(id, paper_id, title, created_at)
messages(id, conversation_id, role, content, tool_calls, created_at)
model_profiles(id, user_id, name, provider, model, base_url, api_key_keychain_ref, params_json)
  -- api_key_keychain_ref points to a QtKeychain entry; the secret is never stored in SQLite.
prompt_templates(id, user_id, name, kind, body)
```

## 6. UX Notes

- Reader is the default view; chat is a togglable right pane (≥30% width).
- Highlight + right-click → "Ask about this" prefills chat input with quoted selection.
- TOC sidebar on far left, collapsible.
- Settings: a "Models" tab and a "Prompts" tab; each prompt template has a kind (`summary` / `translate` / `chat-system`) that gates where it appears.
- Reading state (scroll position, last-viewed section, open conversation) is persisted per paper.

## 7. Configuration

Per-user settings file (`~/.ai-reader/config.json` or DB row):
```json
{
  "active_model_profile": "claude-opus",
  "model_profiles": [
    {"id": "claude-opus", "provider": "anthropic", "model": "claude-opus-4-7", "params": {"temperature": 0.2}}
  ],
  "prompts": {
    "summary": "summary_default",
    "translate": "translate_academic_zh",
    "chat_system": "chat_grounded"
  },
  "translation": {"target_lang": "zh-CN", "preserve_terms": ["LLM", "Transformer"]}
}
```

API keys are stored in the OS keychain via QtKeychain (macOS Keychain / Windows Credential Manager / libsecret). Settings JSON only holds the keychain reference key, never the secret itself.

## 8. Performance

- Translate and summarize **lazily**: trigger only when user opens a paper or requests it, not at import.
- Stream output and render incrementally — never block the UI on a full pass. LLM streaming runs on a worker thread; UI updates via `QMetaObject::invokeMethod(... Qt::QueuedConnection)`.
- Chunk long papers for translation by section to stay within model context windows; keep a small overlap for cross-block coherence.
- Cache aggressively (see §3); cache key includes `prompt_hash` so prompt edits invalidate cleanly.
- PDF rendering: rely on `QPdfPageRenderer`'s built-in lazy rendering with an LRU page cache; pre-render `current ± 2` pages.
- MicroTeX renders run on a worker thread, results posted back as `QImage` and cached per `(tex_hash, font_size, dpi)`.

## 9. Error Handling

- Every LLM call wrapped with retry (exponential backoff, max 3) on transient errors; surface the underlying provider error verbatim on final failure.
- Tool calls validated against schema before dispatch; invalid args return a structured error to the model so it can self-correct.
- Partial results are preserved: a failed block-translation does not invalidate completed blocks.

## 10. Testing

- **Unit (`QtTest`)**: block-clusterer on a fixture set covering single-column, double-column, IEEE/ACM templates, a Chinese-text paper, and a deliberately broken PDF. Markdown AST → QML item walker. MicroTeX render coverage on a hand-curated TeX fixture set.
- **Integration**: end-to-end "import → summary → translate → chat with tool call" against a recorded LLM transcript (HTTP responses replayed via a local mock server). No live API in CI.
- **QML view tests** (`qmltestrunner`): scroll-sync invariants, selection→quote flow, math/code rendering smoke tests.
- **Manual checklist**: bilingual scroll sync across short paper, long paper, paper with many figures/equations, scanned paper (forces VL path), Chinese-original paper.

## 11. Milestones

1. **M1 — Skeleton (week 1–2)**: CMake project, Qt Quick app, `QPdfView` rendering a PDF, drag-drop import, encrypted-PDF prompt. (D1–D2 of §12a)
2. **M2 — Bilingual reader (week 3–4)**: block-clusterer, translation pane, batched block translation, synchronized bidirectional scroll, formula/citation pass-through. (D3)
3. **M3 — Summary + TOC + VL path (week 5–6)**: prompt-template editor, model-profile editor, LLM-generated TOC + navigation, VL auto-promotion + `read_page_visual`. (D4, D7)
4. **M4 — Chat pane (week 7–8) ✅**: cmark-gfm pipeline → typed QML items (`ChatContent` returns `{type, html|source|language|dataUrl}` segments to a `Repeater` of `TextBlock` / `CodeBlock` / `MathBlock` delegates), `MathBlock` (MicroTeX-rendered PNG with raw-LaTeX fallback + copy-source), `CodeBlock` (in-house syntax highlighter as a deliberate substitute for KSyntaxHighlighting — pulling in KDE Extra-CMake-Modules wasn't worth the FetchContent surgery; the in-house highlighter covers cpp/c, python, js/ts, json, sh), streaming (TextEdit MarkdownText during chunk arrival, swap to typed items on Done), selection-as-quote ("Ask AI about this" right-click), all 7 tools wired (`list_sections`, `read_page`, `read_page_visual`, `read_section`, `search_paper`, `get_figure_caption`, `get_user_selection`) with user-configurable tool budget (default 30), per-paper multi-session conversations with auto-naming + persistent storage. (D5–D6)
5. **M5 — Desktop polish (week 9–10)**: caching, error UX, settings persistence, signed installers, auto-update, opt-in crash reporting. (D8–D10)

## 12. Tech Stack

- **Language / runtime**: C++17 (Qt 6 minimum), single-process desktop binary.
- **UI framework**: Qt 6.6+ with Qt Quick / QML. Modules used: `QtCore`, `QtGui`, `QtQml`, `QtQuick`, `QtQuickControls2`, `QtPdf`, `QtPdfQuick`, `QtNetwork`, `QtSql`.
- **PDF engine**: **QtPdf** (PDFium under the hood) — `QPdfDocument`, `QPdfView` (QML), `QPdfPageRenderer`, `QPdfBookmarkModel`, `QPdfLinkModel`, `QPdfSearchModel`.
- **Math rendering**: **MicroTeX** (C++ LaTeX-math renderer) integrated as a CMake submodule. Wrapped in a `QQuickPaintedItem` exposed to QML as `MathBlock`.
- **Markdown parsing**: **cmark-gfm** (C library, CommonMark + GFM) as CMake submodule. AST walk in C++ produces a typed item list bound to a QML `Repeater`.
- **Code-block syntax highlighting**: **KSyntaxHighlighting** (KDE Frameworks, Qt-native, standalone — no other KF deps required).
- **HTTP / LLM**: `QNetworkAccessManager` for plain JSON requests; SSE parsing for streaming responses. One provider class per API shape (`AnthropicClient`, `OpenAICompatibleClient`).
- **Storage**: SQLite via `QtSql`, single DB file in `QStandardPaths::AppLocalDataLocation`. **FTS5** virtual tables for keyword search.
- **Secrets**: **QtKeychain** (cross-platform wrapper for macOS Keychain / Windows Credential Manager / libsecret).
- **Build system**: CMake (Qt 6 first-class). Dependency resolution via CMake `FetchContent` for MicroTeX, cmark-gfm, KSyntaxHighlighting, QtKeychain. No vcpkg/Conan required.
- **Packaging**: Qt's deployment tools — `macdeployqt` → `.dmg`, `windeployqt` → MSI/Inno Setup, `linuxdeploy-qt` → `.AppImage`. Auto-update via a small custom updater hitting a signed manifest.
- **Testing**: Qt Test (`QtTest`) for C++ units, Squish or `qmltestrunner` for QML view tests.

## 12a. Desktop Build & Distribution Roadmap

| Phase | Deliverable | Key tasks |
|---|---|---|
| **D1 — Project skeleton** | CMake project building a runnable Qt Quick app with a stub `QPdfView` | Qt 6.6 install (Qt Online Installer or aqt); CMake preset for each OS; CI matrix (GitHub Actions) |
| **D2 — Reader pane** | Working PDF open + render + native text selection in left pane | `QPdfView` + `QPdfPageNavigator`; password prompt for encrypted PDFs; drag-drop import |
| **D3 — Translation pane + sync scroll** | Right `Flickable` shows translated blocks; scroll mirrors `QPdfView::pageNavigator().currentPage()` | block-clusterer in C++; QML `Repeater` over a `QAbstractListModel`; debounced bidirectional sync |
| **D4 — LLM plumbing** | LLM client classes; settings UI for model profiles; secrets in QtKeychain | `QNetworkAccessManager` + SSE; profile editor in QML; keychain test on each OS |
| **D5 — Chat pane (markdown + math + code)** | Working chat with cmark-gfm → typed QML items, `MathBlock` via MicroTeX, code via KSyntaxHighlighting | font shipping for MicroTeX; KSyntaxHighlighting theme to match Qt palette |
| **D6 — TOC + tools + selection→quote** | LLM-generated TOC; all 7 tools in §3.6 wired; user-configurable tool budget (default 30) | `Q_INVOKABLE` tool dispatchers; selection model exposed from `QPdfView` |
| **D7 — VL path** | Auto-promotion + on-demand `read_page_visual` | `QPdfPageRenderer` to PNG; vision request format per provider |
| **D8 — First installables** | Unsigned `.dmg` / `.exe` / `.AppImage` | `macdeployqt` / `windeployqt` / `linuxdeploy-qt`; smoke test per OS |
| **D9 — Signing & notarization** | Signed builds for macOS + Windows | Apple Developer ID + notarytool; Windows EV cert or Azure Trusted Signing |
| **D10 — Auto-update + crash reporting** | In-app update check; opt-in crash reports | signed manifest on GitHub Releases; Sentry (opt-in, offline-first) |

## 13. Open Questions

- Selection-quote flow: when a user highlights in the Chinese column, do we send the Chinese span, the aligned English span, or both as the quoted context?
- Should prompt templates be shareable (export/import JSON), or kept private to the local install?
- **Search engine for `search_paper` tool**: ship v1 with SQLite FTS5 keyword search only, and add semantic embeddings (ONNX Runtime + `bge-small`) in v1.1? Or invest the bundle size now (~50–100 MB for ONNX runtime + model)?
- **MicroTeX coverage**: validate against a fixture set of 20–30 representative papers from the user's domain before committing. If `\begin{align}` / custom-package math fails for >5% of equations, fallback plan is to render math via a tiny embedded `QtWebEngineView` with KaTeX *only* for chat replies (the reader pane stays native QtPdf).
- **Translation cache invalidation**: include the source PDF's content hash in the cache key, so re-importing a corrected version of the same paper doesn't serve stale translations.
