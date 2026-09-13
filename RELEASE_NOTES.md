# FastPDF release notes

[English](RELEASE_NOTES.md) · [ไทย](RELEASE_NOTES.th.md) — [README](README.md)

Release-note text for FastPDF versions, written from what this repository
actually supports. The last section is copy/paste-ready for the GitHub Release
description field.

## Release asset status

| | |
| --- | --- |
| Version in this repository | `0.2.1` (`CMakeLists.txt`, embedded as the file/product version) |
| Published GitHub Releases | `v0.2.0` (`FastPDF 0.2.0 (Windows x64)`, with asset `FastPDF-0.2.0-win-x64.zip`), plus earlier `v0.1.1` and `v0.1.0`. Verified with `gh release list` / `gh release view v0.2.0` |
| Published `v0.2.1` release | **None yet.** No `v0.2.1` tag, no GitHub Release and no asset exist until someone builds the package below, creates the Release and attaches the ZIP |
| Expected asset once a `v0.2.1` release is created | `FastPDF-0.2.1-win-x64.zip`, built locally with [`scripts/package_release.ps1`](scripts/package_release.ps1) or `cmake --build --preset release --target fastpdf_package_release` |
| Tag name proposed for that release | `v0.2.1` (planned — not created yet; the existing `v0.2.0` tag is left untouched) |

The `0.2.0` sections below describe the published release. Everything about
`0.2.1` is **draft release text**: the facts about the software are supported
by the repository, while the `v0.2.1` tag, Release and asset do not exist on
GitHub until someone creates them.

## What's new in 0.2.1

This is a patch release over the published `0.2.0`. It adds a Help menu with
an About dialog and a manual update notification, plus discoverable
normal-View navigation (keyboard stepping, a native vertical scrollbar and
content-only hand panning). It changes no dependency, no product scope and no
renderer/PDFium behavior.

- **Help > About FastPDF** — a new Help menu whose About dialog shows
  `FastPDF` plus the compiled version string (for this release, `0.2.1`),
  taken from the same `FASTPDF_VERSION_STRING` the structure tests verify.
- **Help > Check for Updates (manual notification only)** — an asynchronous,
  strictly manual check against the fixed
  `api.github.com/repos/underoatz-netizen/FastPDF/releases/latest` endpoint
  over HTTPS with system certificate validation, finite timeouts and a bounded
  response body. There is no startup, timer or polling trigger, nothing is
  downloaded or installed, and no document data or telemetry is transmitted.
  When a newer release exists the dialog offers to open the fixed repository
  releases page after explicit user confirmation; otherwise it reports the
  installed version as up to date, and failures report a plain message without
  touching document or rendering state.
- **Discoverable normal-View navigation** — Up/Down/Left/Right arrows nudge
  the view by a DPI-scaled step, Space / Shift+Space move by approximately one
  viewport, a native vertical scrollbar (range/page/thumb derived from the
  layout geometry) supports line, page and thumb-track scrolling, and
  click-drag on rendered page content pans the document 1:1 under the cursor.
  All of it runs through the existing scroll path, keeps the
  preview-to-final render behavior, and stays out of presentation mode,
  screenshot capture and the save-notification toast. The pure navigation math
  is covered by `fastpdf_view_navigation_tests` and the update parsing and
  version comparison by `fastpdf_update_check_tests` (the WinHTTP fetch path
  itself is intentionally not unit-tested and was exercised by hand only to
  the extent of its error dialogs, not against the live network).
- **Test status:** 26 CTest cases pass with PDFium enabled (Debug and
  Release); 17 are registered without it. These are maintainer-recorded
  results; the repository has no CI workflow.

## What's new in 0.2.0

This is a minor release over `0.1.1`. It adds the modern-UI and per-monitor DPI
work, the double-click-to-Fit-Page gesture, and the release-scope command-line
open, and it changes no dependency or product scope.

- **Modern native UI** — the executable now opts into the Windows
  common-controls v6 visual styles, so the menus, find-panel edit/buttons and
  scrollbars are drawn with the current Windows theme, including their
  built-in hover / focus / pressed states. This is an OS component, not a new
  third-party dependency.
- **Completed per-monitor DPI support** — the find panel and its controls
  rescale and reposition live when the window moves to a monitor with a
  different DPI, the control font is recreated only when the height changes,
  and the status line is rebuilt for the new DPI. The status line now reads as
  two levels: the document name leads on the left while page / zoom (and the
  optional diagnostics readout) is right-aligned in a muted tone.
- **Double-click a page to Fit Page** — double-clicking directly on rendered
  page content fits that page to the viewport, reusing the existing Fit Page
  command (`Ctrl+0`). A double-click on the margin beside a page, in the gap
  between pages, in presentation mode or during screenshot capture does
  nothing.
- **Command-line / file-association open, verified** — `FastPDF.exe
  "C:\path\to\file.pdf"` opens the document directly. An end-to-end test now
  reproduces the exact Explorer `shell\open\command` shape: an executable path
  containing spaces, plus a document path containing spaces and Thai Unicode.
  The path is never split and the executable is never opened as a document.

## v0.2.0 at a glance

- **What it is:** a lightweight, fast, minimal Windows PDF viewer — Win32 +
  Direct2D + PDFium, continuous single-column reading, rendering off the UI
  thread.
- **Platform:** Windows 10 / 11, **x64 only**; needs `pdfium.dll` beside
  `FastPDF.exe`; no installer and no administrator rights.
- **Main capabilities:** viewing and zooming, presentation mode, screenshot
  capture, PDF → PNG, images → PDF, native printing, incremental text search,
  recent files, direct command-line PDF open, an optional diagnostics overlay,
  and an original embedded application icon.
- **Not in this release:** password prompt, OCR,
  bundled PDFium in the source repository, and a top-level `LICENSE` file.
- **Test status:** 24 CTest cases pass with PDFium enabled (Debug and Release);
  15 are registered without it. These are maintainer-recorded results; the
  repository has no CI workflow.

## Copy/paste text for the GitHub Release description

### For v0.2.1 (pending — paste this when creating the v0.2.1 release)

- Suggested **tag**: `v0.2.1` (create it on the commit you are releasing; it does not exist yet — leave the existing `v0.2.0` tag untouched).
- Suggested **release title**: `FastPDF 0.2.1 (Windows x64)`.

Paste the block below into the release description. It uses absolute links so it
renders correctly outside the repository file view, and indented code blocks so
the whole block stays copyable as one piece. Replace nothing except the parts
that describe assets you did not attach.

```markdown
**FastPDF 0.2.1** is a patch update to a lightweight, fast, minimal Windows
PDF viewer built with Win32 + Direct2D + PDFium. It adds a Help menu with an
About dialog and a manual-only update notification, plus discoverable
normal-View navigation (arrow-key stepping, a native vertical scrollbar and
content-only hand panning); it is the successor to the published 0.2.0 and
changes no dependency or product scope.

> **Assets:** attach `FastPDF-0.2.1-win-x64.zip` only after it has been produced
> locally by [`scripts/package_release.ps1`](https://github.com/underoatz-netizen/FastPDF/blob/master/scripts/package_release.ps1).
> If no asset is attached to this release, FastPDF is available as source code
> only — see the build instructions in the
> [README](https://github.com/underoatz-netizen/FastPDF/blob/master/README.md).

## Supported platform

- Windows 10 or Windows 11, **64-bit (x64)** only.
- `FastPDF.exe` must sit in the same folder as `pdfium.dll` (the portable ZIP
  above already contains both).
- No installer, no administrator rights and no .NET runtime dependency. The UI
  (menus, dialogs, messages) is English only.

## How to run the portable package

1. Download and extract the ZIP to any folder, for example `C:\Tools\FastPDF\`.
2. Start `FastPDF.exe`. Nothing is installed and no registry entries are written.
3. Press **Ctrl+O** (or **File > Open...**) and choose a PDF, or drag a single
   PDF onto the window. You can also open one PDF directly at launch:
   `FastPDF.exe "C:\path\to\file.pdf"` (the path may contain spaces and
   Unicode; quote it as shown).
4. To move or remove the app, move or delete the folder.

## What is new in 0.2.1

- **Help > About FastPDF** — shows the application name and the compiled
  version (`0.2.1`).
- **Help > Check for Updates** — a strictly manual, asynchronous notification
  check against the published GitHub releases. Nothing runs at startup or on a
  timer, nothing is downloaded or installed, and no document data or telemetry
  leaves the machine. When a newer release exists you are asked whether to
  open the releases page; otherwise you are told the installed version is up
  to date.
- **Arrow-key and page navigation** — arrow keys nudge the view, Space /
  Shift+Space move by about one viewport, all in normal View only.
- **Native vertical scrollbar** — a standard Windows scrollbar whose range,
  page size and thumb position follow the document layout, including
  thumb-track dragging.
- **Content-only hand panning** — click-drag directly on rendered page content
  to pan; drags starting on margins, gaps, in presentation mode, during
  screenshot capture or under the save notification never pan.
- Everything from the published 0.2.0 (continuous viewing, modern native UI,
  per-monitor DPI, double-click to Fit Page, command-line open, zoom and page
  anchor, presentation mode, screenshot capture, PDF to PNG, images to PDF,
  native printing, incremental text search, recent files, diagnostics,
  graceful error messages, application icon) is unchanged.

## Portable package contents

        FastPDF.exe                application binary
        pdfium.dll                 pinned PDFium runtime
        THIRD_PARTY_NOTICES.txt    third-party licenses and acknowledgments
        README.txt                 project README
        LICENSE.txt                license / terms notice from the packaging script
        MANIFEST.txt               SHA-256 hashes of every file in the package

The package is produced from a Release build; `MANIFEST.txt` lets you verify the
files with `Get-FileHash`.

## Testing status

26 CTest cases pass with PDFium enabled, in both Debug and Release. Built with
`-DFASTPDF_WITH_PDFIUM=OFF` the configuration registers 17 cases; its last
recorded full run predates the search-panel routing, command-line-open,
view-navigation and update-check tests, so that configuration has not been
re-verified since. These results were
recorded by the maintainer on a single Windows 11 x64 reference machine; this
repository has no CI workflow, and `fastpdf_print_integration_tests`
additionally requires Microsoft Print to PDF to be available on the machine that
runs it.
```

### Published v0.2.0 text (historical — already used for the v0.2.0 release)

- Published **tag**: `v0.2.0`.
- Published **release title**: `FastPDF 0.2.0 (Windows x64)`.

Paste the block below into the release description. It uses absolute links so it
renders correctly outside the repository file view, and indented code blocks so
the whole block stays copyable as one piece. Replace nothing except the parts
that describe assets you did not attach.

```markdown
**FastPDF 0.2.0** is a minor update to a lightweight, fast, minimal Windows
PDF viewer built with Win32 + Direct2D + PDFium. It adds a modern native UI,
completed per-monitor DPI handling, double-click-to-Fit-Page and verified
command-line / file-association open; it is the successor to 0.1.1.

> **Assets:** attach `FastPDF-0.2.0-win-x64.zip` only after it has been produced
> locally by [`scripts/package_release.ps1`](https://github.com/underoatz-netizen/FastPDF/blob/master/scripts/package_release.ps1).
> If no asset is attached to this release, FastPDF is available as source code
> only — see the build instructions in the
> [README](https://github.com/underoatz-netizen/FastPDF/blob/master/README.md).

## Supported platform

- Windows 10 or Windows 11, **64-bit (x64)** only.
- `FastPDF.exe` must sit in the same folder as `pdfium.dll` (the portable ZIP
  above already contains both).
- No installer, no administrator rights and no .NET runtime dependency. The UI
  (menus, dialogs, messages) is English only.

## How to run the portable package

1. Download and extract the ZIP to any folder, for example `C:\Tools\FastPDF\`.
2. Start `FastPDF.exe`. Nothing is installed and no registry entries are written.
3. Press **Ctrl+O** (or **File > Open...**) and choose a PDF, or drag a single
   PDF onto the window. You can also open one PDF directly at launch:
   `FastPDF.exe "C:\path\to\file.pdf"` (the path may contain spaces and
   Unicode; quote it as shown).
4. To move or remove the app, move or delete the folder.

## What is included in 0.2.0

- **Continuous PDF viewing** — single-column layout, pages rendered off the UI
  thread into CPU bitmaps and displayed with Direct2D; half-size preview first,
  then final quality.
- **Modern native UI** — Windows common-controls v6 visual styles are enabled,
  so menus, find-panel controls and scrollbars use the current Windows theme and
  its hover / focus / pressed states; the status line shows the document name on
  the left and page / zoom right-aligned in a muted tone.
- **Per-monitor DPI** — the window, status line and find panel follow the DPI of
  the monitor the window is on, re-laying out live on a DPI transition instead
  of only at startup.
- **Double-click to Fit Page** — double-clicking on rendered page content fits
  that page to the viewport (the Fit Page command); clicks on the margin or the
  inter-page gap are ignored.
- **Direct command-line open** — pass one PDF path as the first argument
  (`FastPDF.exe "path\to\file.pdf"`); spaces and Unicode are supported, including
  the exact Explorer file-association command shape. A no-argument launch opens
  the normal empty window and never tries to open the executable itself as a
  PDF.
- **Zoom and page anchor** — Fit Width, Fit Page, 100%, free zoom clamped to
  25%-800%, cursor-centered Ctrl+mouse-wheel zoom, and a page anchor that keeps
  the logical location across zoom, window resize and per-monitor DPI changes.
- **Presentation mode (F11)** — borderless fullscreen, each page fitted to the
  viewport, adjacent pages pre-rendered for near-instant advance, full state
  restored on exit.
- **Screenshot capture (Ctrl+S)** — marquee selection that may span several
  pages, composited from rendered page content only, copied to the clipboard as
  `CF_DIBV5` and PNG, with optional one-step PNG file save.
- **PDF to PNG export** — All / Current / Custom page ranges (for example
  `1-5, 8, 10`), Standard (150 DPI) or High (300 DPI), progress with
  cancellation, deterministic zero-padded names such as `doc_page_001.png`, and
  no overwriting of existing files.
- **Images to PDF** — batch conversion of PNG / JPEG / JPG / BMP with list
  reordering, automatic A4 portrait/landscape choice, preserved aspect ratio,
  safe alpha handling, atomic output and cancellation.
- **Native printing (Ctrl+P)** — printer enumeration, page ranges, per-printer
  paper sizes with A4 fallback, Fit or Actual Size scaling, Auto/Portrait/
  Landscape orientation, 1-999 copies, live preview using the same layout model
  as spooling, background spooling with cancellation, and PDFium vector
  rendering to the printer DC with a high-resolution raster fallback.
- **Incremental text search (Ctrl+F)** — background search with a non-modal
  panel, `current / total` counter, `<` / `>` navigation, up to 5,000 stored
  matches and highlighted result rectangles. The panel's edit box and its
  `<` / `>` / close buttons reach the main window through a command forwarder
  installed on the panel, so typing and clicking both drive the search; that
  routing is proven by `fastpdf_search_ui_routing_tests` (4 cases) and was not
  re-checked by hand in the running UI.
- **Recent files** — up to 10 entries with restored page, view position and
  zoom, stored in `%LOCALAPPDATA%\FastPDF\recent.dat`; unreadable entries are
  pruned.
- **Diagnostics (Ctrl+D)** — optional live status-line counters (queue depth,
  cache size and hit rate, frame time) plus a rotating log at
  `%LOCALAPPDATA%\FastPDF\fastpdf.log` (rotates at 1 MB). Document text is never
  logged, and the application has no network code path.
- **Graceful error messages** — missing, unreadable, damaged, password-protected
  and unsupported-security PDFs each report a plain-language message instead of
  a crash.
- **Application icon** — an original, generated PDF-document icon embedded in the
  executable (Explorer, title bar, task bar and Alt+Tab).

## Portable package contents

        FastPDF.exe                application binary
        pdfium.dll                 pinned PDFium runtime
        THIRD_PARTY_NOTICES.txt    third-party licenses and acknowledgments
        README.txt                 project README
        LICENSE.txt                license / terms notice from the packaging script
        MANIFEST.txt               SHA-256 hashes of every file in the package

The package is produced from a Release build; `MANIFEST.txt` lets you verify the
files with `Get-FileHash`.

## Known limitations in 0.2.0

- Password-protected PDFs show `This PDF requires a password.` — there is no
  password prompt yet.
- Rendering is serialized through one PDFium call gate, so a slow page can queue
  behind the current one (the UI itself never blocks).
- Fit Width / Fit Page in the viewer fit to the **first page** of the document.
- Search covers the text layer only — no OCR, so scanned pages return no
  results.
- Multi-file drag-and-drop routes supported images to Image to PDF; PDFs in a
  multi-file drop are ignored.
- Printing was verified through the native Windows spooler and
  **Microsoft Print to PDF**; physical ink/toner hardware was not tested.
- PDFium is **not** bundled in the source repository. Building requires the
  pinned artifact described in
  [`third_party/pdfium/PDFIUM_LOCK.md`](https://github.com/underoatz-netizen/FastPDF/blob/master/third_party/pdfium/PDFIUM_LOCK.md)
  (release tag `chromium/8035`, version 154.0.8035.0, non-V8 build).
- The source repository has no top-level `LICENSE` file; `LICENSE.txt` inside
  this package is the license/terms notice written by the packaging script.
- Performance figures are single-machine observations from
  `scripts/benchmark_startup.ps1`, which measures FastPDF's own startup/open/
  first-frame phases only. They are **not** a claim that FastPDF is faster than
  SumatraPDF or any other viewer; any cross-product comparison must use a
  matched methodology.

## Build from source (if no binary asset is attached)

    cmake --preset release -DPDFIUM_ROOT=C:/deps/pdfium
    cmake --build --preset release
    ctest --preset release
    .\build\release\bin\Release\FastPDF.exe

## Testing status

24 CTest cases pass with PDFium enabled, in both Debug and Release. Built with
`-DFASTPDF_WITH_PDFIUM=OFF` the configuration now registers 15 cases; its last
recorded full run was 13/13, taken before the search-panel routing and
command-line-open tests were added, so that configuration has not been
re-verified since. These results were
recorded by the maintainer on a single Windows 11 x64 reference machine; this
repository has no CI workflow, and `fastpdf_print_integration_tests`
additionally requires Microsoft Print to PDF to be available on the machine that
runs it.

## Third-party notices

FastPDF links against **PDFium** (BSD 3-Clause, The PDFium Authors / Google LLC)
and uses Windows SDK components (Direct2D, DirectWrite, WIC, Win32 APIs). See
[`THIRD_PARTY_NOTICES.md`](https://github.com/underoatz-netizen/FastPDF/blob/master/THIRD_PARTY_NOTICES.md).
The PDFium artifact ships its own `LICENSE` and `licenses/` directory, which must
accompany any redistribution of `pdfium.dll`.
```

## Notes for whoever publishes a release

- Attach only files that were really built and checked: the ZIP plus, if you
  want, the `MANIFEST.txt` hashes as plain text in the release body.
- Do not paste a download URL by hand — GitHub generates the asset link when the
  file is attached.
- Do not claim a checksum for the ZIP in advance; publish the hash only after the
  asset exists: `Get-FileHash .\dist\FastPDF-0.2.1-win-x64.zip -Algorithm SHA256`.
- The version string comes from `CMakeLists.txt`; bump it there before building
  the next package so the ZIP name and `MANIFEST.txt` stay consistent.
- Commit and push this documentation first: the Thai release body links to
  `README.th.md`, which only resolves on GitHub once the documentation exists in
  the repository.
- The release body above is the English text. Use
  [RELEASE_NOTES.th.md](RELEASE_NOTES.th.md) for the Thai version.

## Related documents

- [Thai release notes](RELEASE_NOTES.th.md)
- [README (English)](README.md) · [README (ไทย)](README.th.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [PDFium integration lock](third_party/pdfium/PDFIUM_LOCK.md)
- [Release packaging script](scripts/package_release.ps1)
