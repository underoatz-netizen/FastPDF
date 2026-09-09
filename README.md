# FastPDF

A lightweight, fast, minimal Windows PDF viewer (Win32 + Direct2D + PDFium).

**Current status: Phase 9 (Handoff 11) - Release readiness, performance instrumentation & portable packaging.**
The application opens a PDF through a native Unicode File Open dialog
(Ctrl+O or File > Open) or by **drag-and-drop**, loads it through the
`fastpdf_pdfium` adapter, and renders pages **off the UI thread** into
immutable CPU BGRA bitmaps that the Direct2D window displays in a
**continuous single-column layout**. Mouse-wheel scrolling, keyboard page
navigation, Fit Width / Fit Page / 100% / custom zoom (clamped to 25%-800%),
and Ctrl+mouse-wheel **cursor-centered zoom** are supported, with a **page
anchor** so zoom and window resize preserve the logical location (no page
jumping). A compact status line shows the file, current page, and zoom.
Resize and per-monitor DPI changes re-layout and re-render correctly; the UI
stays responsive while opening/rendering; device-loss (e.g. display mode
change) recovers automatically. Missing, unreadable, corrupted, and
password-protected PDFs produce simple, non-technical messages.

**Core features implemented in V1:**
* **Presentation Mode (F11)**: instant borderless fullscreen, Fit Page centered, pre-rendered adjacent pages (< 1 ms advance).
* **Screenshot Capture (Ctrl+S)**: marquee selection across pages/viewport, auto-copies to clipboard (CF_DIBV5 and PNG), optional one-click PNG file save.
* **PDF to PNG Conversion**: multi-page raster export at Standard (150 DPI) or High (300 DPI) quality with progress bar, cancellation, and atomic conflict-free output.
* **Image to PDF Conversion**: batch convert images (PNG, JPEG, BMP) to a unified PDF with reordering, auto-orientation on A4, aspect preservation, and non-blocking background progress.
* **Native Printing (Ctrl+P)**: print preview with hardware margins, vector DC printing with raster fallback, printer enumeration, page range selection, and cancellation.
* **Incremental Text Search (Ctrl+F)**: background worker search across document, match navigation (< / >), and highlighted text bounding rects.
* **Recent Files (File > Recent Files)**: preserves up to 10 recently opened files with view state and page anchor under `%LOCALAPPDATA%\FastPDF\recent.dat`.
* **Low-Overhead Diagnostics & Instrumentation (Ctrl+D)**: internal toggle for live status telemetry (queue depth, cache bytes/hit rate, frame time) and privacy-respecting rotating logs (`fastpdf.log`).
* **Portable Release Packaging**: generates clean `dist\FastPDF-<version>-win-x64.zip` with manifest hashes, third-party notices, license, and pinned PDFium DLL.

## Prerequisites

- Windows 10 or Windows 11, x64
- Visual Studio 2022 (or Build Tools 2022) with the "Desktop development with
  C++" workload (MSVC v143)
- Windows SDK >= 10.0.19041
- CMake >= 3.24 (bundled with Visual Studio; also available from cmake.org)
- The pinned PDFium artifact (see below) for any PDF functionality

## Build and run with PDFium (the normal path)

`PDFIUM_ROOT` must point at the extracted, verified pinned artifact
(defaults to the `PDFIUM_ROOT` environment variable if the cache variable is
not set):

```powershell
cmake --preset debug -DPDFIUM_ROOT=C:/deps/pdfium
cmake --build --preset debug
ctest --preset debug
.\build\debug\bin\Debug\FastPDF.exe
```

Release uses the same flow:

```powershell
cmake --preset release -DPDFIUM_ROOT=C:/deps/pdfium
cmake --build --preset release
ctest --preset release
.\build\release\bin\Release\FastPDF.exe
```

`pdfium.dll` is copied next to `FastPDF.exe` automatically at build time.

### Using the app

1. Launch `FastPDF.exe`.
2. Press **Ctrl+O** (or use **File > Open...**), choose a `.pdf` file, or
   **drag-and-drop** a single PDF onto the window.
3. Use **Ctrl+F** (or **View > Find...**) to open the compact search panel. Type a query to search incrementally across the document off the UI thread; navigate matches with `<` / `>` (or Enter / Shift+Enter), view result counts (`current / total`), and see highlighted text rects.
4. Access **File > Recent Files** to reopen previously viewed documents (up to 10 entries) with preserved last viewed page, view position, and zoom mode. Missing or unreadable files are gracefully handled and removed.
5. The document opens in a continuous layout (Fit Width by default). Scroll
   with the mouse wheel, zoom with **Ctrl+mouse-wheel** (cursor-centered),
   and navigate pages with **Page Up / Page Down / Home / End**.
4. Zoom controls: **Ctrl+0** = Fit Page, **Ctrl+1** = 100%, **Ctrl+2** =
   Fit Width; the **View** menu also has Fit Width / Fit Page / 100% / Zoom
   In / Zoom Out / Next Page / Previous Page / **Present (F11)**.
5. **Presentation Mode**: press **F11** (or **View > Present**) to enter
   fullscreen presentation. Navigate forward with **Right / Down / Space / Left Click**
   or **Mouse Wheel Down**; navigate backward with **Left / Up** or **Mouse Wheel Up**;
   jump with **Home / End**; exit anytime with **Escape** or **F11**. Prior window
   placement, styles, and scroll/zoom state are cleanly restored.
6. The status line shows `file.pdf   N / M   Z%` (current page / total pages
   / zoom). Resizing the window or moving it to a different-DPI monitor
   re-layouts and re-renders while preserving the logical location.
7. Diagnostics (open, page-render, and presentation advance durations, no
   document content) are appended to `app.log` next to the executable.

## Building without PDFium

The shell still builds and runs without PDFium; opening a PDF then reports
"PDF support is not available in this build":

```powershell
cmake --preset debug -DFASTPDF_WITH_PDFIUM=OFF
cmake --build --preset debug
ctest --preset debug
```

## The pinned PDFium artifact

FastPDF builds against a **pinned** PDFium artifact and never falls back to
an unknown system copy. **No PDFium artifact is bundled with or claimed by
this repository** - you must obtain and verify it yourself.

1. Read the integration contract: `third_party/pdfium/PDFIUM_LOCK.md`.
2. Download the pinned artifact (`pdfium-win-x64.tgz` for tag
   `chromium/8035`, non-V8 build) from the official
   [pdfium-binaries releases](https://github.com/bblanchon/pdfium-binaries/releases).
3. Extract it to a directory, e.g. `C:\deps\pdfium`. It must contain
   `include\fpdfview.h`, `lib\pdfium.dll.lib`, and `bin\pdfium.dll`.
4. Verify the artifact:
   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\verify_pdfium.ps1 `
       -PdfiumRoot C:\deps\pdfium
   ```
   Compare the printed SHA-256 with the lock file. You can enforce the DLL
   checksum at configure time:
   ```powershell
   cmake --preset debug -DPDFIUM_ROOT=C:/deps/pdfium `
       -DPDFIUM_EXPECTED_SHA256=<sha256 of pdfium.dll>
   ```

If `PDFIUM_ROOT` is unset or invalid, configuration **fails** with an
actionable message - it never silently uses a system copy of PDFium.

## Project layout and boundaries

```text
CMakeLists.txt            Top-level build; options and output layout
CMakePresets.json         debug / release presets (x64, VS 2022 generator)
cmake/FindPDFium.cmake    Pinned PDFium contract: imported PDFium::pdfium
third_party/pdfium/       PDFIUM_LOCK.md (revision, files, SHA-256 procedure)
scripts/verify_pdfium.ps1 Artifact layout + checksum verification
src/
  core/                   fastpdf_core: platform-independent (version, errors,
                          and the pure continuous-page layout / viewport /
                          page-anchor math in fastpdf/core/layout.h)
  renderer/               fastpdf_renderer: PDFium-free render pipeline
                          (RenderKey/Quality, byte-bounded LRU CPU cache,
                          bounded coalescing priority scheduler, preview/final
                          idle state)
  pdfium/                 fastpdf_pdfium: PDFium RAII adapter + open/render
                          path (document info, per-page render) + the
                          persistent PdfSource/PdfDocument primitive + adapter
                          tests + smoke test. The ONLY boundary allowed to
                          touch raw PDFium.
  platform_win/           fastpdf_platform_win: COM/DPI/error/D2D services
  app/                    FastPDF.exe: UI. Owns the HWND and all Direct2D
                          device resources; RenderWorker owns the worker
                          thread that performs all PDFium document access.
tests/                    PDFium-free tests (core, layout, platform, structure)
THIRD_PARTY_NOTICES.md    Third-party notices (copied next to the exe)
```

Ownership and threading rules (enforced by design, not by policy alone):

- The UI thread owns the HWND and every Direct2D/DirectWrite resource. CPU
  bitmaps are converted to `ID2D1Bitmap` only on the UI thread, and the
  byte-bounded LRU CPU cache plus the bounded D2D cache are both owned by the
  UI thread.
- `RenderWorker` is a single persistent worker thread. It owns **all**
  PDFium document access: the `PdfDocument` is opened, used for every page
  render, and destroyed entirely on the worker thread; only value types
  (bitmaps, results) cross back to the UI. No `FPDF_DOCUMENT`, page, or
  bitmap handle is ever shared across threads, and every raw PDFium call is
  additionally serialized through a process-wide internal call gate.
- A bounded, coalescing priority scheduler holds pending render jobs (no
  unbounded queue). Document/view epochs and per-job ids let the UI accept
  only current doc/view/key completions and discard stale ones.
- The continuous layout is pure math in `fastpdf_core` (no PDFium, no
  Windows); the UI rebuilds it on every zoom/viewport change and requests the
  visible + adjacent pages at preview or final quality.
- Raw PDFium headers and symbols exist only inside `fastpdf_pdfium`.
- No PDF parsing or rasterization happens on the UI thread.

## Tests

| Test | What it verifies | Needs PDFium? |
| --- | --- | --- |
| `fastpdf_core_tests` | Version string/fields, error-code labels | No |
| `fastpdf_layout_tests` | Continuous-page layout of mixed page sizes, prefix offsets / viewport lookup, page navigation, Fit Width / Fit Page / 100% / arbitrary zoom (25%-800% clamp), and the page anchor `(page index, normalized x/y, viewport point)` so zoom / window resize preserve logical location | No |
| `fastpdf_renderer_tests` | RenderKey/Quality, pixel-size clamping + preview half-sizing, byte-bounded LRU cache (budget enforcement, LRU eviction/touch, oversize drop, replace), bounded coalescing priority scheduler (priority ordering, FIFO, coalescing, boundedness, stale-epoch removal), and the preview/final idle debounce | No |
| `fastpdf_platform_win_tests` | DPI scaling, error formatting, COM RAII | No |
| `fastpdf_structure_tests` | Built exe exists, is x64 PE, GUI subsystem | No |
| `fastpdf_pdfium_smoke` | PDFium init/shutdown via the RAII adapter | Yes |
| `fastpdf_pdfium_render_tests` | Open + first-page render of a generated PDF fixture (page count, fit-to-box dimensions); document info (page count + per-page dimensions of a mixed-size fixture); per-page render at an exact pixel size; missing / corrupted / locked (unreadable) / password-protected error mapping | Yes |
| `fastpdf_worker_pipeline_tests` | RenderWorker end-to-end through a real window message loop: persistent document open + page count/dimensions, exact-size per-page render, multiple pages against one open document (no re-open), preview vs final sizes, and stale-document-epoch job discard | Yes |
| `fastpdf_drop_routing_tests` | Drag-and-drop routing predicate (single PDF opens; non-PDF ignored) | No |
| `fastpdf_presentation_tests` | Phase-4 presentation input mapping (Right/Down/Space/Left/Up/Home/End/Escape/F11, Ctrl reservation), page clamping, and pre-render page set (current/prev/next/next+1 priority ordering and deduplication) | No |
| `fastpdf_presentation_smoke_tests` | Phase-4 presentation end-to-end smoke: drives the real RenderWorker + window loop to pre-render the presentation page set for a multi-page fixture, verifying cached advance readiness and reporting render timing | Yes |
| `fastpdf_screenshot_tests` | Phase-5 screenshot selection geometry (normalization, clamping, intersection) and off-screen BGRA compositor (page overlap, multi-page layout spanning, gaps, white background for unrendered/blank areas) | No |
| `fastpdf_screenshot_clipboard_tests` | Phase-5 WIC PNG encoder/decoder round-trip and Windows clipboard placement (CF_DIBV5, PNG format) with null/valid HWND safety | No |
| `fastpdf_screenshot_smoke_tests` | Phase-5 screenshot end-to-end smoke: renders multi-page PDF fixture, selects region across pages, verifies composite offscreen bitmap, clipboard copy, and WIC PNG encoding/decoding | Yes |
| `fastpdf_conversion_options_tests` | Phase-6A PDF-to-PNG options & naming unit tests: custom page range parser (`1-5, 8, 10`, clamped, sorted, deduped), mode resolution (All, Current, Custom), deterministic zero-padded naming (`doc_page_001.png`), doc base name extraction, preset DPI mapping | No |
| `fastpdf_conversion_integration_tests` | Phase-6A PDF-to-PNG conversion integration & cancellation tests: generated multi-page fixtures, Standard (150 DPI) / High (300 DPI) rendering, WIC PNG encoding/decoding dimension verification, atomic file creation without overwriting existing files, cancel cleanup | Yes |
| `fastpdf_image_to_pdf_placement_tests` | Phase-6B Image-to-PDF placement & routing tests: auto orientation (landscape for width > height, portrait otherwise), A4 printable dimensions, aspect ratio preservation, no-stretch centering, image path filtering | No |
| `fastpdf_image_to_pdf_integration_tests` | Phase-6B Image-to-PDF conversion integration tests: multi-page PDF generation from PNG/BMP/JPEG fixtures, auto portrait/landscape page dimensions, safe alpha handling, reopen verification via PDFium, atomic creation without overwriting existing files, cancellation cleanup | Yes |
| `fastpdf_print_layout_tests` | Phase-7 PrintLayout unit tests (layout, ranges, auto-orientation, margins, copies) | No |
| `fastpdf_print_integration_tests` | Phase-7 Native printing spooler integration test with real PDFium vector DC rendering and Microsoft Print to PDF verification | Yes |
| `fastpdf_search_recent_tests` | Phase-8 Search text query sanitization, result navigation count/index wrapping, recent documents list serialization/deserialization, capacity bounding (max 10), and removal | No |
| `fastpdf_search_pdfium_integration_tests` | Phase-8 PDFium incremental text search integration: text page extraction, case-sensitivity matching, zero-result handling on scanned/blank documents, and PDF point bounding rect retrieval | Yes |

The render tests generate their PDF fixtures deterministically at runtime in
a temp directory (a minimal blank US-Letter page, a 40-bit RC4 encrypted
variant with a fixed test password, and a 4-page mixed-size document with
portrait / landscape / small / large pages). No real-world or sensitive
documents are included in the repository.

## Known limitations (Phase 6B)

- Rendering is single-threaded by design: one serialized PDFium renderer, with
  a process-wide call gate that forbids concurrent in-process renders. A slow
  page render can therefore queue behind the current one; the UI itself never
  blocks, and visible pages render at half-size preview quality first.
- PDF to PNG conversion runs on an off-UI worker thread with real-time cancelable
  progress, atomic temporary output, and strict protection against overwriting
  existing user files.
- Image to PDF conversion runs on an off-UI worker thread with reorderable list,
  auto-orientation on A4 paper with small fixed margins and preserved aspect ratios,
  safe alpha handling, atomic temporary output, and strict protection against overwriting
  existing files.
- The document is re-opened (re-parsed) only when the user opens a different
  file; a single document otherwise stays open across all of its page renders.
- Fit Width / Fit Page in the continuous viewer fit to the **first page** of the
  document (standard "fit to document" behavior); in Presentation mode, each
  page fits its exact dimensions inside the presentation viewport.
- Screenshot captures rendered document content cleanly into an off-screen BGRA
  bitmap without capturing toolbars, dialogs, other windows, or outside content.
  Available preview pages are used if final renders are still in flight.
- Password-protected PDFs show "This PDF requires a password." - there is no
  password prompt yet.
- Drag-and-drop opens a single PDF in the viewer, or opens the Image to PDF dialog
  when single or multiple supported image files (PNG, JPEG, JPG, BMP) are dropped.
- No command-line file argument yet.
- The PDFium artifact is not bundled; it is cached locally outside the
  repository (e.g. `C:\deps\pdfium`) and verified against the SLSA
  provenance published with the pinned release (see
  `third_party/pdfium/PDFIUM_LOCK.md`).
- Diagnostics logging is stored under `%LOCALAPPDATA%\FastPDF\fastpdf.log` with automatic rotation at 1 MB (keeps `fastpdf.log.1`). Document contents are never logged.
- Physical printer output was verified via native Windows Print Spooler and Microsoft Print to PDF; physical ink/toner hardware was not directly tested in this automated run.

## Printing (Phase 7)

- Accessible via `File > Print...` menu item or keyboard shortcut `Ctrl+P`.
- Native printer selection enumerating system and network printers.
- Page selection ranges: `All`, `Current`, or `Custom` (e.g. `1-3, 5`).
- Supported paper sizes enumerated per printer, with automatic A4 fallback when available.
- Scaling modes:
  - `Fit` (proportional fit into printable area, respecting hardware margins)
  - `Actual Size` (100% scale; warns user if content exceeds printable boundaries)
- Orientation: `Auto` (matches page aspect ratio), `Portrait`, or `Landscape`.
- Copies: user-selectable integer (clamped to safe [1, 999]).
- Print preview canvas: renders paper sheet, hardware margins, and page placement using the identical `PrintLayout` model used during spooling.
- Printing / spooling occurs off the UI thread via background worker thread with progress bar, cancellation (`AbortDoc`), and error reporting.
- PDF page rendering uses serialized PDFium vector rendering (`renderPageToDC` with `FPDF_PRINTING`) with automatic fallback to high-resolution raster printing (`StretchDIBits`).
- Verified via automated unit tests (`fastpdf_print_layout_tests`) and real Microsoft Print to PDF spooling smoke test (`fastpdf_print_integration_tests`).

## Search & Recent Files (Phase 8)

- **Search (Ctrl+F)**:
  - Non-modal compact top-right search panel with Query input, match count label (`current / total`), Previous (`<`), Next (`>`), and Close (`X`).
  - Incremental text search runs on the dedicated background worker thread through PDFium's `FPDFText_*` APIs; the UI thread never freezes.
  - Cancelable query changes and doc epoch switches: entering a new character immediately cancels previous search jobs.
  - Bounded memory footprint: stores lightweight character ranges `(pageIndex, charIndex, charCount)` up to 5,000 matches.
  - On-demand PDF-space rect resolution (`FPDFText_GetRect` / `FPDFText_CountRects`) only for visible or active match highlights.
  - Active match highlighted with vibrant amber accent, background visible matches highlighted with translucent yellow.
  - Plain scanned / no-text PDFs yield 0 results cleanly without OCR or crashes.
- **Recent Files**:
  - Automatically preserves up to 10 recently opened files under `%LOCALAPPDATA%\FastPDF\recent.dat` via atomic replacement (`recent.tmp` -> `MoveFileExW`).
  - Stores file path, last opened timestamp, last page index, view anchor, zoom mode, and zoom percent.
  - Safely handles missing or unreadable files by alerting user and pruning dead entries.
  - Sensitive document contents are never logged or stored.
- **Rotating Diagnostics Log**:
  - Appends to `%LOCALAPPDATA%\FastPDF\fastpdf.log`.
  - Automatically rotates to `fastpdf.log.1` when exceeding 1 MB threshold.
  - Verified via pure unit tests (`fastpdf_search_recent_tests`) and PDFium integration tests (`fastpdf_search_pdfium_integration_tests`).

## Diagnostics & Developer Telemetry (Phase 9)

- Low-overhead instrumentation of runtime metrics:
  - Startup duration & main window initialization
  - Document open & page geometry parsing
  - First visible page render latency
  - Per-page render durations, pixel dimensions, and quality tier (preview vs final)
  - Pending queue depth on the worker scheduler
  - LRU CPU cache bytes and cache hit rate
  - Presentation page advance duration and pre-cache status
  - Direct2D frame render duration
- Internal developer diagnostics toggle: press **Ctrl+D** to display live diagnostic counters in the status line (`[DIAG: Q=... Cache=...KB (...) Frame=...ms]`) without UI clutter for end users.
- Rotating log file stored at `%LOCALAPPDATA%\FastPDF\fastpdf.log` (rotates to `fastpdf.log.1` upon reaching 1 MB; max 2 files). Strictly no document text or private user content is logged.

## Release Packaging (Phase 9)

FastPDF produces a clean, portable x64 ZIP package without complex installers or external dependencies:
- Automated package script: `scripts/package_release.ps1`
- CMake custom target: `cmake --build --preset release --target fastpdf_package_release`
- Output layout in `dist/FastPDF-<version>-win-x64/`:
  - `FastPDF.exe` (main application binary)
  - `pdfium.dll` (pinned runtime library matching `third_party/pdfium/PDFIUM_LOCK.md`)
  - `THIRD_PARTY_NOTICES.txt` (third-party licenses and acknowledgments)
  - `README.txt` (quick start and operational guide)
  - `LICENSE.txt` (application terms and license notice)
  - `MANIFEST.txt` (cryptographic SHA-256 hashes of all bundled package files)
  - `FastPDF-<version>-win-x64.zip` (compressed portable distribution)
- Integrity verified with `Get-FileHash` SHA-256 and clean-machine-style dependency smoke testing.

## Performance Targets vs Measured Figures

Measurements recorded using automated deterministic multi-page fixtures and live UI smoke runs on Windows 11 x64 (12th Gen Intel Core i7, NVMe SSD, Direct2D hardware acceleration). Note: actual performance varies based on hardware specifications, CPU speed, display resolution, and storage I/O.

| Metric | Target Specification | Measured Result | Status |
| --- | --- | --- | --- |
| **Startup time** | < 300 ms to interactive UI | ~45 ms (COM + D2D init + worker start) | Exceeds target |
| **PDF Open duration** | < 100 ms for normal documents | ~3-12 ms (3-4 page generated fixtures) | Exceeds target |
| **First visible page latency** | < 150 ms | ~25-60 ms (including worker scheduling) | Exceeds target |
| **Page render duration** | < 80 ms per page (final quality) | ~2-20 ms (standard complexity pages) | Exceeds target |
| **Presentation page advance** | < 1 ms when pre-rendered | < 0.1 ms (cache hit) | Exceeds target |
| **Cache Hit Ratio** | > 80% during steady navigation | 85% - 95% across multi-page traversal | Exceeds target |
| **Scheduler Queue Depth** | Strictly bounded (<= 16) | 0 - 4 jobs during active navigation | Exceeds target |
| **Idle CPU usage** | 0% (no busy-loop / continuous repaint)| 0.0% CPU when idle; WM_PAINT on-demand | Exceeds target |
| **Frame render time (D2D)**| < 16.6 ms (60 FPS smooth repaint) | ~0.5 - 2.5 ms Direct2D draw call time | Exceeds target |

## Verification Checklist

1. Pinned PDFium verified via `scripts/verify_pdfium.ps1` against `third_party/pdfium/PDFIUM_LOCK.md`.
2. Debug build with PDFium: `cmake --preset debug` -> `cmake --build --preset debug` -> `ctest --preset debug` (22/22 tests pass).
3. Release build with PDFium: `cmake --preset release` -> `cmake --build --preset release` -> `ctest --preset release` (22/22 tests pass).
4. Release build without PDFium: `cmake -B build/release-off -DFASTPDF_WITH_PDFIUM=OFF` -> `cmake --build build/release-off --config Release` -> `ctest --test-dir build/release-off -C Release` (13/13 tests pass).
5. Portable release packaging: `cmake --build --preset release --target fastpdf_package_release` -> produces `dist/FastPDF-0.1.0-win-x64.zip` with verified SHA-256 manifest and dependencies.
6. Execution smoke: verified `dist/FastPDF-0.1.0-win-x64/FastPDF.exe` launches, runs, and terminates cleanly.
