# FastPDF

**English** · [ไทย](README.th.md)

A lightweight, fast, minimal **Windows** PDF viewer built with Win32 + Direct2D +
PDFium — with presentation mode, screenshot capture, PDF ⇄ image conversion,
native printing and incremental text search.

There is no installer, no service and no app store: you either build it from
this repository or run the portable folder that the packaging script produces.

| | |
| --- | --- |
| Version | `0.2.0` (`CMakeLists.txt`) |
| Platform | Windows 10 / 11, **x64 only** |
| Toolkit | C++20, MSVC v143, Win32, Direct2D, PDFium |
| Status | Phase 9 — release readiness, performance instrumentation, portable packaging |
| Repository | <https://github.com/underoatz-netizen/FastPDF> (branch `master`) |

---

## Table of contents

- [What FastPDF does](#what-fastpdf-does)
- [Features](#features)
- [Supported platform and requirements](#supported-platform-and-requirements)
- [Quick start: running FastPDF](#quick-start-running-fastpdf)
- [Opening a PDF](#opening-a-pdf)
- [Reading: scrolling, zoom and view modes](#reading-scrolling-zoom-and-view-modes)
- [Menus](#menus)
- [Keyboard shortcuts](#keyboard-shortcuts)
- [Search inside a document](#search-inside-a-document)
- [Presentation mode](#presentation-mode)
- [Screenshot capture](#screenshot-capture)
- [Convert PDF to PNG](#convert-pdf-to-png)
- [Convert images to PDF](#convert-images-to-pdf)
- [Printing](#printing)
- [Recent files](#recent-files)
- [Diagnostics overlay](#diagnostics-overlay)
- [Files FastPDF writes on your machine](#files-fastpdf-writes-on-your-machine)
- [Build from source](#build-from-source)
- [Build options](#build-options)
- [Building without PDFium](#building-without-pdfium)
- [Release packaging](#release-packaging)
- [Releases and prebuilt binaries](#releases-and-prebuilt-binaries)
- [Tests](#tests)
- [Project layout and boundaries](#project-layout-and-boundaries)
- [Threading and ownership rules](#threading-and-ownership-rules)
- [Performance](#performance)
- [Known limitations](#known-limitations)
- [Verification record](#verification-record)
- [Contributing](#contributing)
- [License and third-party notices](#license-and-third-party-notices)
- [Related documents](#related-documents)

---

## What FastPDF does

FastPDF opens a PDF through a native Unicode File Open dialog (**Ctrl+O** or
**File > Open...**), by **drag-and-drop**, or directly from the command line
(`FastPDF.exe "path\to\file.pdf"`), loads it through the
`fastpdf_pdfium` adapter, and renders pages **off the UI thread** into immutable
CPU BGRA bitmaps that the Direct2D window displays in a **continuous
single-column layout**.

Mouse-wheel scrolling, keyboard page navigation, Fit Width / Fit Page / 100% /
custom zoom (clamped to 25%–800%), and Ctrl+mouse-wheel **cursor-centered
zoom** are supported, with a **page anchor** so that zooming and window resizing
preserve the logical location instead of jumping. A compact status line shows
the file, the current page and the zoom level.

Resizing the window and per-monitor DPI changes re-layout and re-render
correctly; the UI stays responsive while documents are opened and rendered; a
Direct2D device loss (for example a display-mode change) recovers automatically.
Missing, unreadable, corrupted and password-protected PDFs produce simple,
non-technical messages.

## Features

- **Continuous viewing** — single-column layout with preview-then-final rendering.
- **Zoom** — Fit Width, Fit Page, 100%, arbitrary zoom (25%–800%), cursor-centered Ctrl+wheel.
- **Presentation mode (F11)** — borderless fullscreen, Fit Page per page, pre-rendered adjacent pages for near-instant advance.
- **Screenshot capture (Ctrl+S)** — marquee selection across pages, copied to the clipboard (`CF_DIBV5` and PNG), optional save to a PNG file.
- **PDF → PNG** — multi-page export at Standard (150 DPI) or High (300 DPI) with progress, cancellation and conflict-free output.
- **Images → PDF** — batch convert PNG / JPEG / BMP with reordering, automatic A4 orientation and preserved aspect ratio.
- **Native printing (Ctrl+P)** — print preview, printer enumeration, page ranges, scaling and orientation, cancellation.
- **Incremental text search (Ctrl+F)** — background search, match navigation, highlighted result rectangles.
- **Recent files** — up to 10 entries with preserved page, view position and zoom.
- **Diagnostics (Ctrl+D)** — optional live telemetry in the status line plus a rotating local log.
- **Portable packaging** — a clean `dist\FastPDF-<version>-win-x64.zip` with a SHA-256 manifest and third-party notices.

## Supported platform and requirements

**To run FastPDF**

- Windows 10 or Windows 11, **64-bit (x64)**.
- `FastPDF.exe` with `pdfium.dll` in the same folder (PDF support requires that
  DLL; see [Releases and prebuilt binaries](#releases-and-prebuilt-binaries)).
- No installer, no administrator rights, no .NET or Visual Studio required at
  runtime.

**To build FastPDF from source**

- Visual Studio 2022 (or Build Tools 2022) with the *Desktop development with
  C++* workload (MSVC v143).
- Windows SDK >= 10.0.19041.
- CMake >= 3.24 (bundled with Visual Studio, or from cmake.org).
- C++20 (set by `CMakeLists.txt`).
- The pinned PDFium artifact, obtained and verified by you
  ([Build from source](#build-from-source)). FastPDF never downloads it and
  never falls back to a system copy.

**Notes**

- The project refuses to configure for anything other than x64
  (`CMakeLists.txt` fails with an explicit message).
- The build uses the default MSVC **dynamic** CRT (no runtime-library override
  is set in the CMake files). Whether a machine without the Visual C++
  2015–2022 redistributable can start the packaged `FastPDF.exe` has **not**
  been tested on a clean machine; treat that as an unverified manual check.
- The application UI (menus, dialogs, messages) is English only.

## Quick start: running FastPDF

If you already have a FastPDF folder or ZIP (built locally with
[Release packaging](#release-packaging), or copied from such a build):

1. Extract it anywhere you like — for example `C:\Tools\FastPDF\` or a USB
   stick. `FastPDF.exe` and `pdfium.dll` must stay in the same folder.
2. Start `FastPDF.exe`. Nothing is installed and no registry entries are
   written.
3. The empty window shows: `Press Ctrl+O or File > Open to open a PDF.`
4. To move or remove FastPDF, delete or move the folder. The only per-user
   state is described in
   [Files FastPDF writes on your machine](#files-fastpdf-writes-on-your-machine).

If you do not have a build yet, go to [Build from source](#build-from-source).

## Opening a PDF

1. Launch `FastPDF.exe`.
2. Press **Ctrl+O** (or **File > Open...**) and choose a `.pdf` file. The dialog
   filter is `PDF files (*.pdf)` with an `All files (*.*)` option.
   Alternatively, **drag and drop** a single PDF onto the window.
3. The document opens in the continuous layout (Fit Width by default) and the
   status line shows `file.pdf   N / M   Z%` — current page, total pages, zoom.

**Direct command-line open.** You can also open one PDF directly at launch by
passing its path as the first command-line argument:

    FastPDF.exe "C:\Users\me\My Folder\report.pdf"

The path may contain spaces and Unicode characters; quote it as shown. Only the
first explicit argument is used as the document to open — any further arguments
are ignored. Launching `FastPDF.exe` with **no** argument opens the normal empty
window (ready for **Ctrl+O** or drag-and-drop); it never tries to open the
executable itself as a PDF. A missing or unreadable path shows the same
plain-language error as opening it from the dialog (see below).

Dropping a single supported image file (`.png`, `.jpg`, `.jpeg`, `.bmp`) opens
the Image to PDF dialog instead. Dropping several files starts Image to PDF with
the supported images from that drop; PDFs and other file types in a multi-file
drop are ignored.

If a document cannot be opened, the viewer shows one of these plain-language
messages (from `AppWindow::UserMessageForError`):

| Situation | Message shown |
| --- | --- |
| File not found | `The file could not be found.` |
| File cannot be read | `The file could not be read.` |
| Damaged or not a PDF | `The PDF could not be opened. It may be damaged or not a PDF.` |
| Encrypted, password needed | `This PDF requires a password.` |
| Unsupported security scheme | `This PDF uses an unsupported security scheme.` |
| Built without PDFium | `PDF support is not available in this build.` |

## Reading: scrolling, zoom and view modes

- **Scroll** with the mouse wheel; **Page Up / Page Down** jump one page,
  **Home / End** go to the first / last page.
- **Zoom**: **Ctrl+mouse wheel** zooms around the cursor. **Ctrl+0** = Fit Page,
  **Ctrl+1** = 100%, **Ctrl+2** = Fit Width. Free zoom is clamped to 25%–800%.
- **Page anchor**: the logical location under the viewport is preserved when you
  zoom or resize the window, and when you move the window to a
  different-DPI monitor.
- Fit Width / Fit Page in the viewer fit to the **first page** of the document
  (standard "fit to document" behaviour). In presentation mode each page fits
  its own dimensions inside the presentation viewport.

## Menus

| Menu | Items |
| --- | --- |
| **File** | `Open... (Ctrl+O)`, `Recent Files ▸` (entries, `Clear Recent Files`), `Print... (Ctrl+P)` |
| **View** | `Fit Width (Ctrl+2)`, `Fit Page (Ctrl+0)`, `100% (Ctrl+1)`, `Find... (Ctrl+F)`, `Zoom In`, `Zoom Out`, `Next Page (Page Down)`, `Previous Page (Page Up)`, `Present (F11)`, `Screenshot (Ctrl+S)` |
| **Convert** | `PDF to PNG...`, `Image to PDF...` |

## Keyboard shortcuts

| Key | Action |
| --- | --- |
| `Ctrl+O` | Open a PDF |
| `Ctrl+F` | Find panel (`Esc` closes it) |
| `Ctrl+S` | Screenshot marquee selection |
| `Ctrl+P` | Print dialog |
| `Ctrl+D` | Toggle the diagnostics overlay in the status line |
| `Ctrl+0` / `Ctrl+1` / `Ctrl+2` | Fit Page / 100% / Fit Width |
| `Ctrl` + mouse wheel | Zoom centered on the cursor |
| `Page Up` / `Page Down` | Previous / next page |
| `Home` / `End` | First / last page |
| `F11` | Enter or leave presentation mode |
| `Right` / `Down` / `Space` / `PgDn` / wheel down / left click | Presentation: next slide |
| `Left` / `Up` / `PgUp` / wheel up | Presentation: previous slide |
| `Home` / `End` (in presentation) | First / last slide |
| `Esc` | Leave presentation mode, or close the Find panel |

## Search inside a document

- Press **Ctrl+F** (or **View > Find...**) to open a compact, non-modal panel in
  the top-right corner: query field, `current / total` match counter, `<`, `>`,
  and a close button.
- Search runs incrementally on the background worker thread through PDFium's
  `FPDFText_*` APIs, so the UI never freezes; typing a new character cancels the
  previous search job.
- Navigate matches with the panel's `<` / `>` buttons or with Enter /
  Shift+Enter, and dismiss the panel with its close button or **Esc**.
- The panel's edit box and buttons report to the main window through a
  command forwarder installed on the panel, so typing and clicking both
  drive the search. That routing is covered by the automated
  `fastpdf_search_ui_routing_tests` case; the panel's on-screen behaviour has
  not been re-checked manually.
- Matches are stored as lightweight character ranges, bounded at 5,000 results
  (`kMaxSearchResults`); rectangles are resolved on demand only for visible or
  active highlights. The active match is highlighted in amber, other visible
  matches in translucent yellow.
- Scanned or text-less PDFs simply return 0 results.

## Presentation mode

- Press **F11** (or **View > Present**) for instant borderless fullscreen with
  the current page fitted to the viewport.
- Advance with **Right / Down / Space / Page Down / wheel down / left click**,
  go back with **Left / Up / Page Up / wheel up**, jump with **Home / End**.
- Adjacent pages are pre-rendered, so a cached advance is effectively instant.
- Exit with **Esc** or **F11**; the previous window placement, styles, scroll
  and zoom state are restored.

## Screenshot capture

- Press **Ctrl+S** (or **View > Screenshot**) and drag a marquee across the
  document — the selection may span more than one page.
- The captured region is composited off-screen from rendered page content only:
  toolbars, dialogs and other windows are never captured. Where a page has not
  finished rendering, the area is white; available preview renders are used if
  final renders are still in flight.
- The result is placed on the clipboard as `CF_DIBV5` and PNG, and can be saved
  as a PNG file in one step.

## Convert PDF to PNG

**Convert > PDF to PNG...** exports pages as PNG files.

- Modes: **All**, **Current** page, or **Custom** ranges such as `1-5, 8, 10`
  (parsed, sorted, de-duplicated and clamped to the document).
- Quality presets: **Standard = 150 DPI**, **High = 300 DPI**.
- Output names are deterministic and zero-padded, e.g. `doc_page_001.png`.
- The work runs on a worker thread with a progress bar and cancellation, writes
  through a temporary file, and **never overwrites an existing file**.

## Convert images to PDF

**Convert > Image to PDF...** (or drop image files on the window) builds one PDF
from PNG / JPEG / JPG / BMP files.

- The list is reorderable before conversion.
- Each page is A4 with small fixed margins; orientation is chosen automatically
  (landscape when width > height), the aspect ratio is preserved and the image
  is centered without stretching.
- Alpha is handled safely, output is written atomically, existing files are not
  overwritten, and the job can be cancelled from a non-blocking progress dialog.

## Printing

**File > Print...** or **Ctrl+P** (available when a document is open):

- Native printer selection enumerating system and network printers.
- Page selection: `All`, `Current`, or `Custom` (for example `1-3, 5`).
- Paper sizes enumerated per printer, with automatic A4 fallback when available.
- Scaling: `Fit` (proportional, respecting hardware margins) or `Actual Size`
  (100%, warns if content exceeds the printable area).
- Orientation: `Auto` (matches page aspect), `Portrait`, `Landscape`.
- Copies: 1–999.
- The print preview uses the same `PrintLayout` model as the spooling path.
- Spooling runs off the UI thread with progress, cancellation (`AbortDoc`) and
  error reporting; pages are rendered as PDFium vector output to the printer DC
  with automatic fallback to high-resolution raster output.

## Recent files

- **File > Recent Files** lists up to 10 previously opened documents;
  **Clear Recent Files** empties the list.
- Each entry stores the path, last-opened timestamp, last page index, view
  anchor, zoom mode and zoom percent, so a reopened document returns to where
  you left it.
- Entries are persisted in `%LOCALAPPDATA%\FastPDF\recent.dat` through an atomic
  replacement (`recent.tmp` → `MoveFileExW`).
- Missing or unreadable files are reported and pruned from the list.

## Diagnostics overlay

- Press **Ctrl+D** to append live counters to the status line:
  `[DIAG: Q=<queue> Cache=<KB> (<hit>%) Frame=<ms>]`.
- Instrumented metrics include startup duration, document open and geometry
  parsing, first-visible-page latency, per-page render duration and quality
  tier, scheduler queue depth, LRU cache bytes and hit rate, presentation
  advance time and Direct2D frame time.
- The overlay is off by default and is intended for developers and bug reports.

## Files FastPDF writes on your machine

| Path | Purpose |
| --- | --- |
| `%LOCALAPPDATA%\FastPDF\recent.dat` | Recent-files list (paths, page, view anchor, zoom) |
| `%LOCALAPPDATA%\FastPDF\fastpdf.log` | Rotating diagnostics log |
| `%LOCALAPPDATA%\FastPDF\fastpdf.log.1` | Previous log generation after rotation at 1 MB |
| Files you explicitly export | PDF→PNG and image→PDF output goes to the location you choose |

No document text or private content is written to the log; the log holds timing
and counter data only. Nothing is uploaded anywhere — FastPDF has no network
code path in this repository.

## Build from source

### 1. Obtain and verify the pinned PDFium artifact

Read the integration contract first:
[`third_party/pdfium/PDFIUM_LOCK.md`](third_party/pdfium/PDFIUM_LOCK.md).
**No PDFium artifact is bundled with or claimed by this repository** — you must
obtain and verify it yourself.

1. Download `pdfium-win-x64.tgz` (non-V8 build) for release tag `chromium/8035`
   from the official
   [pdfium-binaries releases](https://github.com/bblanchon/pdfium-binaries/releases),
   together with `pdfium-attestation.json`.
2. Extract it to a directory outside the repository, for example `C:\deps\pdfium`.
   It must contain `include\fpdfview.h`, `lib\pdfium.dll.lib` and `bin\pdfium.dll`.
3. Verify the layout and print the SHA-256 of `pdfium.dll`:

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\verify_pdfium.ps1 `
       -PdfiumRoot C:\deps\pdfium
   ```

4. Compare the printed hash with the table in `PDFIUM_LOCK.md`, then enforce it
   at configure time (and optionally pass `-ExpectedSha256` to the script):

   ```powershell
   cmake --preset debug -DPDFIUM_ROOT=C:/deps/pdfium `
       -DPDFIUM_EXPECTED_SHA256=<sha256 of pdfium.dll>
   ```

If `PDFIUM_ROOT` is unset or invalid, configuration **fails** with an actionable
message; it never silently uses a system copy of PDFium.

### 2. Configure, build, test, run

From the repository root (PowerShell):

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

`pdfium.dll` is copied next to `FastPDF.exe` automatically at build time, and
`THIRD_PARTY_NOTICES.txt` is copied there by the
`fastpdf_third_party_notices` target.

## Build options

| Option | Default | Effect |
| --- | --- | --- |
| `FASTPDF_WITH_PDFIUM` | `ON` | Build the real PDFium adapter; `OFF` compiles a stub and the app reports that PDF support is unavailable |
| `FASTPDF_BUILD_TESTS` | `ON` | Build the test executables and register them with CTest |
| `FASTPDF_BUILD_SMOKE` | `ON` | Build the PDFium smoke executable (requires `FASTPDF_WITH_PDFIUM`) |
| `PDFIUM_ROOT` | env `PDFIUM_ROOT` | Path to the extracted, verified pinned artifact |
| `PDFIUM_EXPECTED_SHA256` | empty | Enforce the `pdfium.dll` checksum at configure time |

Presets: `debug` and `release`, both `Visual Studio 17 2022`, architecture
`x64`, binary dirs `build/debug` and `build/release`
([`CMakePresets.json`](CMakePresets.json)).

## Building without PDFium

The shell still builds and runs without PDFium; opening a PDF then reports
`PDF support is not available in this build.`:

```powershell
cmake --preset debug -DFASTPDF_WITH_PDFIUM=OFF
cmake --build --preset debug
ctest --preset debug
```

## Release packaging

The packaging script assembles a portable, dependency-light x64 ZIP — no
installer:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 `
    -BuildDir build\release -DistDir dist -PdfiumRoot C:\deps\pdfium
```

or through the CMake custom target (it depends on the `FastPDF` target, so run
it after configuring with a valid `PDFIUM_ROOT`):

```powershell
cmake --build --preset release --target fastpdf_package_release
```

Output layout in `dist/FastPDF-<version>-win-x64/`:

```text
FastPDF.exe                    application binary
pdfium.dll                     pinned runtime library
THIRD_PARTY_NOTICES.txt        third-party licenses and acknowledgments
README.txt                     copy of this README
LICENSE.txt                    license / terms notice written by the script
MANIFEST.txt                   SHA-256 hashes of all bundled files
FastPDF-<version>-win-x64.zip  compressed portable distribution (in dist/)
```

The script fails early if `FastPDF.exe`, `pdfium.dll`, `THIRD_PARTY_NOTICES.md`
or `README.md` cannot be found, computes the manifest with `Get-FileHash`,
creates the ZIP and checks that the staged folder contains both the executable
and `pdfium.dll`. `dist/` is excluded by [`.gitignore`](.gitignore).

## Releases and prebuilt binaries

**This repository does not publish prebuilt binaries.** As of the commit this
README describes, the GitHub repository has no published Releases and no tags,
and `dist/` is not committed. To use FastPDF you must build it from source as
described above, or run a package that someone built locally.

The version string in this repository is `0.2.0`, and the locally produced
package name is `FastPDF-0.2.0-win-x64.zip`; that file exists only on the
machine that built it.

Draft release-note text for the current release (English and Thai, plus a
copy/paste block for the GitHub Release description) lives in
[RELEASE_NOTES.md](RELEASE_NOTES.md).

## Tests

`ctest --preset debug` and `ctest --preset release` run **24 tests** when
PDFium is enabled (**15** of them when it is not, because the PDFium-dependent
tests are not built).

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
| `fastpdf_command_line_open_tests` | Direct command-line PDF open decision: no-argument launch selects nothing (never the executable), a single explicit path at `argv[1]` is returned verbatim, spaces/Unicode paths are preserved, multiple arguments use only the first, an empty argument is treated as no path, and an end-to-end `CommandLineToArgvW` parse of a realistic full command line selects the document not the executable | No |
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
| `fastpdf_search_ui_routing_tests` | Search panel command routing: a STATIC panel swallows its child controls' `WM_COMMAND` without the forwarder, and with the forwarder installed the close-button command and the edit box `EN_CHANGE` reach the main window (with correct control id and handle) while other messages pass through to the original panel procedure | No |
| `fastpdf_search_pdfium_integration_tests` | Phase-8 PDFium incremental text search integration: text page extraction, case-sensitivity matching, zero-result handling on scanned/blank documents, and PDF point bounding rect retrieval | Yes |

The render tests generate their PDF fixtures deterministically at runtime in a
temp directory (a minimal blank US-Letter page, a 40-bit RC4 encrypted variant
with a fixed test password, and a 4-page mixed-size document with portrait /
landscape / small / large pages). No real-world or sensitive documents are
included in the repository.

Some tests are environment-dependent: `fastpdf_print_integration_tests` spools
through **Microsoft Print to PDF**, so it needs that printer to be available on
the machine that runs it.

## Project layout and boundaries

```text
CMakeLists.txt            Top-level build; options and output layout
CMakePresets.json         debug / release presets (x64, VS 2022 generator)
cmake/FindPDFium.cmake    Pinned PDFium contract: imported PDFium::pdfium
third_party/pdfium/       PDFIUM_LOCK.md (revision, files, SHA-256 procedure)
scripts/verify_pdfium.ps1 Artifact layout + checksum verification
scripts/package_release.ps1 Portable release package assembly (dist/)
scripts/make_icon.py      Regenerates src/app/resources/fastpdf.ico (optional
                          dev tool; needs Python + Pillow; not part of the build)
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

## Threading and ownership rules

Ownership and threading rules (enforced by design, not by policy alone):

- The UI thread owns the HWND and every Direct2D/DirectWrite resource. CPU
  bitmaps are converted to `ID2D1Bitmap` only on the UI thread, and the
  byte-bounded LRU CPU cache plus the bounded D2D cache are both owned by the
  UI thread.
- `RenderWorker` is a single persistent worker thread. It owns **all** PDFium
  document access: the `PdfDocument` is opened, used for every page render, and
  destroyed entirely on the worker thread; only value types (bitmaps, results)
  cross back to the UI. No `FPDF_DOCUMENT`, page, or bitmap handle is ever
  shared across threads, and every raw PDFium call is additionally serialized
  through a process-wide internal call gate.
- A bounded, coalescing priority scheduler holds pending render jobs (no
  unbounded queue). Document/view epochs and per-job ids let the UI accept only
  current doc/view/key completions and discard stale ones.
- The continuous layout is pure math in `fastpdf_core` (no PDFium, no Windows);
  the UI rebuilds it on every zoom/viewport change and requests the visible +
  adjacent pages at preview or final quality.
- Raw PDFium headers and symbols exist only inside `fastpdf_pdfium`.
- No PDF parsing or rasterization happens on the UI thread.

## Performance

Measurements recorded with the automated deterministic multi-page fixtures and
live UI smoke runs on one Windows 11 x64 machine (12th Gen Intel Core i7, NVMe
SSD, Direct2D hardware acceleration). **These are observations on a single
reference machine, not guarantees** — actual results vary with CPU, display
resolution, document complexity and storage I/O.

| Metric | Target | Measured on that machine |
| --- | --- | --- |
| Startup time | < 300 ms to interactive UI | ~45 ms (COM + D2D init + worker start) |
| PDF open duration | < 100 ms for normal documents | ~3–12 ms (3–4 page generated fixtures) |
| First visible page latency | < 150 ms | ~25–60 ms (including worker scheduling) |
| Page render duration | < 80 ms per page (final quality) | ~2–20 ms (standard complexity pages) |
| Presentation page advance | < 1 ms when pre-rendered | < 0.1 ms (cache hit) |
| Cache hit ratio | > 80% during steady navigation | 85%–95% across multi-page traversal |
| Scheduler queue depth | strictly bounded (<= 16) | 0–4 jobs during active navigation |
| Idle CPU usage | 0% (no busy-loop / continuous repaint) | 0.0% when idle; `WM_PAINT` on demand |
| Direct2D frame render time | < 16.6 ms (60 FPS repaint) | ~0.5–2.5 ms draw-call time |

**Benchmark harness.** `scripts/benchmark_startup.ps1` reproduces the
direct-CLI startup/open/first-frame measurement by launching
`FastPDF.exe <pdf>` with the `FASTPDF_BENCHMARK` instrumentation enabled and
collecting the JSON phase output across runs. It measures **FastPDF's own**
phases only and does **not** compare against other products; any cross-product
comparison must be run with a matched methodology outside this script. These
numbers are single-machine observations, not guarantees, and are **not** a claim
that FastPDF is faster than SumatraPDF or any other viewer.

## Known limitations

- Rendering is single-threaded by design: one serialized PDFium renderer, with
  a process-wide call gate that forbids concurrent in-process renders. A slow
  page render can therefore queue behind the current one; the UI itself never
  blocks, and visible pages render at half-size preview quality first.
- Password-protected PDFs show `This PDF requires a password.` — there is no
  password prompt yet.
- Fit Width / Fit Page in the continuous viewer fit to the **first page** of the
  document.
- The document is re-parsed only when a different file is opened; one document
  otherwise stays open across all of its page renders.
- Drag-and-drop handles one file at a time for opening: a single PDF opens in the
  viewer, a single supported image (PNG, JPEG, JPG, BMP) starts Image to PDF, and
  a multi-file drop starts Image to PDF with only the supported images from that
  drop (PDFs and other types are ignored there).
- Search is text-layer only — there is no OCR, so scanned pages yield no
  results.
- The PDFium artifact is not bundled; it is cached locally outside the
  repository (for example `C:\deps\pdfium`) and verified against the SLSA
  provenance published with the pinned release
  ([`third_party/pdfium/PDFIUM_LOCK.md`](third_party/pdfium/PDFIUM_LOCK.md)).
- Physical printer output was verified through the native Windows print spooler
  and **Microsoft Print to PDF**; printing onto physical ink/toner hardware was
  not tested in the automated run.
- No prebuilt binaries are published in this repository
  ([Releases and prebuilt binaries](#releases-and-prebuilt-binaries)).

## Verification record

The results below were recorded by the maintainer on the machine described in
[Performance](#performance); they are not reproduced automatically by a CI
service in this repository.

1. Pinned PDFium verified with `scripts/verify_pdfium.ps1` against
   `third_party/pdfium/PDFIUM_LOCK.md`.
2. Debug build with PDFium: `cmake --preset debug` → `cmake --build --preset debug`
   → `ctest --preset debug` (24/24 tests pass).
3. Release build with PDFium: `cmake --preset release` →
   `cmake --build --preset release` → `ctest --preset release` (24/24 tests pass).
4. Release build without PDFium: `cmake -B build/release-off -DFASTPDF_WITH_PDFIUM=OFF`
   → `cmake --build build/release-off --config Release`
   → `ctest --test-dir build/release-off -C Release` (13/13 in that recorded run;
   the configuration now registers 15 cases and has not been re-run since the
   PDFium-free search-routing and command-line-open tests were added).
5. Portable packaging: `cmake --build --preset release --target fastpdf_package_release`
   → `dist/FastPDF-0.2.0-win-x64.zip` with a SHA-256 manifest.
6. Execution smoke: `dist/FastPDF-0.2.0-win-x64/FastPDF.exe` launches, runs and
   terminates cleanly.
7. Search panel command routing: `fastpdf_search_ui_routing_tests` (4 cases)
   proves the panel swallows its children's `WM_COMMAND` without the forwarder
   and delivers the close-button command and the edit `EN_CHANGE` to the main
   window with it. This is automated window-procedure evidence only - the panel
   was not exercised by hand in the running UI.

## Contributing

There is no `CONTRIBUTING.md` in this repository yet, and no pull-request
template or CI workflow. What the repository does establish:

- **Issue tracker** — GitHub Issues are enabled on
  <https://github.com/underoatz-netizen/FastPDF>. Include your Windows version,
  whether the build was made with PDFium, and the status-line `[DIAG: ...]`
  values from [Diagnostics overlay](#diagnostics-overlay) when relevant. Do not
  attach documents you are not allowed to share: tests use generated fixtures,
  not real files.
- **Build and test gates** — a change should keep both configurations green:
  `ctest --preset debug` and `ctest --preset release` with PDFium (24 tests),
  and the `-DFASTPDF_WITH_PDFIUM=OFF` configuration (15 tests).
- **Architecture boundaries** — keep raw PDFium usage inside `src/pdfium/`,
  keep layout math pure in `src/core/`, keep PDFium access on the `RenderWorker`
  thread ([Threading and ownership rules](#threading-and-ownership-rules)).
- **Dependency pins** — a PDFium revision change must update
  `third_party/pdfium/PDFIUM_LOCK.md` in the same commit, with the verification
  procedure re-run.
- **Notices** — when a dependency is added or its license changes, update
  `THIRD_PARTY_NOTICES.md` in the same change.
- **Code style** — the project builds with `/W4 /permissive- /Zc:__cplusplus /utf-8`
  and C++20; warnings are expected to stay clean.

## License and third-party notices

- **This repository does not contain a top-level `LICENSE` file at the commit
  this README describes**, and GitHub therefore reports no detected license for
  it. No license is claimed here beyond what is actually checked in.
- The packaging script `scripts/package_release.ps1` writes a `LICENSE.txt`
  into every produced package: a permissive MIT-style notice attributed to
  "FastPDF Contributors" (Copyright (c) 2026). If you redistribute a package,
  keep that file with it.
- **PDFium** is BSD 3-Clause and its notices are recorded in
  [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md); the artifact archive also
  ships its own `LICENSE` and `licenses/` directory, which must accompany any
  redistribution of `pdfium.dll`.
- **Windows SDK components** (Direct2D, DirectWrite, WIC, Win32 APIs) are
  Microsoft proprietary, governed by the Windows SDK license terms — see the
  same notices file.

If you need a definitive license statement before using or redistributing the
source, ask the maintainer to add a `LICENSE` file at the repository root.

## Related documents

- [Thai README](README.th.md)
- [Release notes](RELEASE_NOTES.md) · [บันทึกการเผยแพร่ (ไทย)](RELEASE_NOTES.th.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [PDFium integration lock](third_party/pdfium/PDFIUM_LOCK.md)
- [Product and architecture specification (AI coding handoff)](FastPDF_AI_Coding_Handoff.md)
- [Pinned PDFium finder](cmake/FindPDFium.cmake)
- [Artifact verification script](scripts/verify_pdfium.ps1)
- [Release packaging script](scripts/package_release.ps1)
