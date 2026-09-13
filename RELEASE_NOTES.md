# FastPDF release notes

[English](RELEASE_NOTES.md) · [ไทย](RELEASE_NOTES.th.md) — [README](README.md)

Release-note text for FastPDF versions, written from what this repository
actually supports. The last section is copy/paste-ready for the GitHub Release
description field.

## Release asset status

| | |
| --- | --- |
| Version in this repository | `0.2.0` (`CMakeLists.txt`, embedded as the file/product version) |
| Published GitHub Releases | **None.** The repository currently has no published Releases and no tags |
| Published release assets | **None.** No binaries are attached and `dist/` is excluded by [`.gitignore`](.gitignore) |
| Expected asset once a release is created | `FastPDF-0.2.0-win-x64.zip`, built locally with [`scripts/package_release.ps1`](scripts/package_release.ps1) or `cmake --build --preset release --target fastpdf_package_release` |
| Tag name proposed for that release | `v0.2.0` (planned — not created yet) |

Everything below is therefore **draft release text**: the facts about the
software are supported by the repository, while the asset itself does not exist
on GitHub until someone builds it, creates the Release and attaches the ZIP.

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

- Suggested **tag**: `v0.2.0` (create it on the commit you are releasing).
- Suggested **release title**: `FastPDF 0.2.0 (Windows x64)`.

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
  asset exists: `Get-FileHash .\dist\FastPDF-0.2.0-win-x64.zip -Algorithm SHA256`.
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
