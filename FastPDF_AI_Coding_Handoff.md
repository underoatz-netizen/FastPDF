# Fast PDF Utility Viewer — AI Coding Handoff Specification

## 1. Project Goal

Build a **lightweight, fast, minimal Windows PDF application** focused on:

- Fast PDF opening
- Smooth continuous scrolling
- Fast and stable zooming
- Instant full-screen presentation
- Screenshot capture
- PDF to PNG conversion
- Image to PDF conversion
- Standard, reliable printing
- Minimal UI
- Very few settings
- Smart defaults

This application is **not** intended to be a full PDF editor.

The product philosophy is:

> Open → Read → Present → Capture → Convert → Print

Every primary function should be accessible within **one click whenever possible**.

The user should not need to understand DPI, rendering engines, print scaling, rasterization, cache management, or internal PDF details.

The application should make good decisions automatically.

---

# 2. Core Product Principles

## 2.1 Performance First

The most important requirement is responsiveness.

The UI must never feel blocked while:

- Scrolling
- Zooming
- Rendering pages
- Changing presentation pages
- Opening large PDFs
- Converting files
- Preparing print preview

The rendering pipeline must be designed so that rendering work does not block user input.

---

## 2.2 Minimal UI

The main UI should expose only the essential actions.

Recommended main commands:

1. Open
2. Present
3. Screenshot
4. Convert
5. Print

Secondary functions:

- Zoom
- Fit Page
- Fit Width
- Search
- Recent files
- Page navigation

Avoid large toolbars and nested menus.

---

## 2.3 Smart Defaults

The program should automatically choose sensible defaults.

Examples:

- Fit Width for normal reading
- Auto orientation for printing
- Fit to Page when page size differs from printer paper
- Standard 150 DPI for PDF to PNG conversion
- 300 DPI for High Quality
- Auto page orientation for image-to-PDF
- Auto caching of adjacent PDF pages
- Auto high-resolution rerender after scrolling stops

Advanced settings should be hidden unless genuinely necessary.

---

# 3. Target Platform

Primary target:

- Windows 10
- Windows 11
- x64

Future cross-platform support is not a V1 requirement.

---

# 4. Recommended Technology Stack

## Language

Use:

- C++20 or newer

## UI

Preferred:

- Win32
- Direct2D
- DirectWrite

Avoid Electron unless there is a very strong technical reason.

The application should remain native and lightweight.

## PDF Engine

Preferred:

- PDFium

Reasons:

- Mature PDF rendering
- Good rendering performance
- Supports bitmap rendering
- Supports Windows printing workflows
- Permissive licensing compared with some alternatives

## Image Handling

Use:

- Windows Imaging Component (WIC)

Supported image formats should initially include:

- PNG
- JPEG
- JPG
- BMP

Optional later:

- TIFF
- WEBP

## Printing

Use:

- Native Windows print APIs
- PDFium rendering

---

# 5. High-Level Architecture

Use a modular architecture.

```text
                    Application
                         |
        +----------------+----------------+
        |                |                |
      Viewer          Utilities        Printing
        |                |                |
        v                v                v
   Render Engine      Converter      Print Engine
        |                |                |
        +----------------+----------------+
                         |
                         v
                       PDFium
                         |
              +----------+----------+
              |                     |
              v                     v
         Bitmap Cache            PDF Core
              |
              v
           Direct2D
              |
              v
            Screen
```

Recommended logical modules:

```text
src/
  app/
  ui/
  viewer/
  renderer/
  presentation/
  screenshot/
  converter/
  printing/
  cache/
  pdf/
  image/
  platform/
  common/
```

Do not create excessive abstractions.

Favor clear, maintainable modules over complicated enterprise architecture.

---

# 6. Viewer Requirements

The Viewer is the most important part of the application.

It must feel extremely smooth.

Required capabilities:

- Continuous scrolling
- Single-page mode
- Fit Width
- Fit Page
- 100% zoom
- Arbitrary zoom
- Mouse wheel scroll
- Ctrl + mouse wheel zoom
- Keyboard navigation
- Drag-and-drop PDF opening
- Page indicator
- Search text
- Recent files

---

# 7. Rendering Architecture

Do not render every PDF page at full resolution.

Use:

## Virtualized Continuous Rendering

Only render pages near the viewport.

Example:

```text
Page 22    unloaded
Page 23    cached
Page 24    rendered
Page 25    rendered <- current viewport
Page 26    rendered
Page 27    pre-render
Page 28    cached
Page 29    unloaded
```

As the user scrolls, update the render window.

Suggested cache strategy:

- Current page: highest priority
- Visible pages: highest priority
- Previous 1–2 pages: cached
- Next 2–3 pages: pre-render
- Far pages: unload

Cache limits must be configurable internally.

Do not expose cache settings to the user.

---

# 8. Rendering Threading Model

Rendering must not block the UI thread.

Recommended structure:

```text
UI Thread
   |
   +-- Input
   +-- Scroll
   +-- Zoom
   +-- Window events
         |
         v
    Render Scheduler
         |
         v
     Render Queue
         |
      Worker Pool
         |
         v
     Bitmap Cache
         |
         v
      Direct2D
```

Important rules:

- Never perform expensive page rendering directly on the UI thread.
- Cancel obsolete rendering work.
- Prioritize visible pages.
- Low-priority pre-render tasks must never delay visible-page rendering.
- Do not allow unbounded render queues.

---

# 9. Smooth Scrolling

Target:

- 60 FPS interaction whenever hardware permits

The user must be able to scroll even when new pages are being rendered.

Use placeholders or previously rendered low-resolution bitmaps rather than blocking.

Avoid layout recomputation during every pixel of scrolling where possible.

---

# 10. Adaptive Rendering

Use adaptive rendering during fast movement.

While user is actively scrolling:

- Display cached page if available
- Otherwise render lower-resolution preview

When scrolling stops for approximately:

- 100–150 ms

Then:

- Render high-quality page bitmap
- Replace preview seamlessly

The transition must not change page geometry or scroll position.

---

# 11. Zoom Behavior

Zoom must feel stable.

Required modes:

- Fit Width
- Fit Page
- 100%
- User-defined zoom percentage

Recommended zoom range:

- 25% to 800%

Use Ctrl + Mouse Wheel.

Important UX requirement:

## Cursor-Centered Zoom

When zooming with the mouse:

The content underneath the mouse pointer should remain approximately in the same logical location.

Do not make the page jump to another area.

---

# 12. Page Anchor System

To prevent jumping during zoom or rerender:

Track:

```text
page_index
relative_page_x
relative_page_y
```

Example:

```text
Page 23
Vertical position = 47%
```

After zoom:

The viewport should remain around:

```text
Page 23
Vertical position = 47%
```

This anchor system should be used whenever layout scale changes.

---

# 13. Presentation Mode

Presentation must be extremely simple.

User action:

```text
Click Present
```

Result:

- Enter full-screen immediately
- Hide all toolbars
- Hide unnecessary UI
- Display page centered
- Fit full page inside screen
- Black or neutral presentation background

Controls:

| Input | Action |
|---|---|
| Right Arrow | Next page |
| Down Arrow | Next page |
| Space | Next page |
| Left Mouse Click | Next page |
| Left Arrow | Previous page |
| Up Arrow | Previous page |
| Mouse Wheel | Previous / Next |
| Home | First page |
| End | Last page |
| Escape | Exit presentation |

Optional:

- F11 enters presentation
- Double-click may enter/exit presentation

---

# 14. Presentation Performance

The application must pre-render presentation pages.

Example:

```text
Previous: page 7 cached
Current:  page 8 ready
Next:     page 9 ready
Next + 1: page 10 rendering
```

When user presses Next:

- page 9 should already be available
- the visual transition should be nearly instantaneous

Do not add page transition animations in V1.

Simple instant replacement is preferred.

---

# 15. Screenshot Feature

Main action:

```text
Screenshot
```

Behavior:

1. Enter area-selection mode
2. Dim non-selected region slightly
3. User drags rectangle
4. User releases mouse
5. Captured image is automatically copied to Windows clipboard
6. Show small non-blocking notification

Example:

```text
✓ Copied to clipboard

Save PNG
```

Default behavior:

- Copy immediately
- Do not ask where to save first

Optional secondary action:

- Save PNG

Screenshot should capture the rendered PDF content cleanly.

---

# 16. PDF to PNG Conversion

Accessible from:

```text
Convert
```

Minimal UI:

```text
PDF -> PNG

Pages:
(*) All pages
( ) Current page
( ) Custom: 1-5, 8, 10

Quality:
(*) Standard
( ) High

[ Convert ]
```

Internal defaults:

```text
Standard = 150 DPI
High     = 300 DPI
```

Do not expose raw DPI unless an Advanced section is later added.

Naming example:

```text
document_page_001.png
document_page_002.png
document_page_003.png
```

Conversion must run outside the UI thread.

Show simple progress.

Example:

```text
Converting 12 / 78
```

Allow cancel.

---

# 17. Image to PDF Conversion

Support:

- PNG
- JPEG
- JPG
- BMP

Input methods:

- Convert menu
- Drag multiple images into application

Expected flow:

```text
Drop images
      |
      v
Preview image order
      |
      v
Create PDF
```

Allow drag-to-reorder images before creating PDF.

Default behavior:

- One image per page
- Preserve aspect ratio
- Auto orientation
- Fit image to printable page
- Small margin
- Do not stretch

Suggested paper defaults:

- A4 where appropriate
- Auto orientation

If image is landscape:

```text
Landscape page
```

If image is portrait:

```text
Portrait page
```

Use smart defaults instead of asking the user to configure every page.

---

# 18. Printing

Printing must be reliable and conventional.

Minimal UI:

```text
PRINT

Printer:
[ selected printer ]

Pages:
(*) All
( ) Current
( ) Custom

Paper:
A4

Scale:
(*) Fit
( ) Actual Size

Orientation:
(*) Auto
( ) Portrait
( ) Landscape

Copies:
1

[ Print ]
```

Optional preview area should show:

- Paper
- PDF page
- Margins
- Scaling

---

# 19. Print Logic

Smart automatic behavior:

## Example 1

PDF:

```text
A4
```

Printer:

```text
A4
```

Result:

```text
Actual size
```

## Example 2

PDF:

```text
Letter
```

Printer:

```text
A4
```

Result:

```text
Fit to printable area
```

## Example 3

PDF page:

```text
Landscape
```

Orientation:

```text
Auto
```

Result:

```text
Landscape
```

Respect printer hardware margins.

Never crop content silently.

---

# 20. Search

V1 should include text search.

Functions:

- Ctrl + F
- Search next
- Search previous
- Highlight current result
- Result count

Example:

```text
12 / 48
```

Search must not freeze UI on large documents.

Run text extraction/indexing incrementally where practical.

---

# 21. Drag and Drop

Required behavior:

## PDF

Drop one PDF:

```text
Open immediately
```

## Images

Drop multiple images:

```text
Offer "Create PDF"
```

Do not require navigating through File > Open when drag and drop is available.

---

# 22. Recent Files

Keep a small recent file list.

Suggested maximum:

```text
10
```

Store:

- File path
- Last opened time
- Last page
- Last zoom mode if appropriate

When reopening a PDF, optionally restore:

- Last page
- View position

Avoid restoring state if it causes confusing behavior.

---

# 23. Main Window UI

Recommended basic layout:

```text
+------------------------------------------------------------+
| Menu / Open       document.pdf          37 / 124      100% |
|                                                            |
|                                                            |
|                        PDF VIEW                            |
|                                                            |
|                                                            |
|                                                            |
+------------------------------------------------------------+
| Open   Present   Screenshot   Convert   Print              |
+------------------------------------------------------------+
```

Alternative:

A single compact top toolbar is acceptable.

Main requirement:

Do not allow the interface to become visually crowded.

---

# 24. Keyboard Shortcuts

Recommended:

| Shortcut | Action |
|---|---|
| Ctrl + O | Open |
| Ctrl + F | Search |
| Ctrl + P | Print |
| Ctrl + Mouse Wheel | Zoom |
| Ctrl + 0 | Fit Page |
| Ctrl + 1 | 100% |
| Ctrl + 2 | Fit Width |
| F11 | Present |
| Esc | Exit presentation |
| Page Up | Previous page |
| Page Down | Next page |
| Home | First page |
| End | Last page |

Avoid excessive shortcuts.

---

# 25. Performance Targets

These are engineering targets, not absolute guarantees.

## Startup

Target:

```text
< 300 ms
```

on a typical SSD Windows PC.

## Small PDF opening

Target:

```text
< 200 ms
```

for initial parsing when possible.

## First page visible

Target:

```text
< 300 ms
```

for typical documents.

## Scrolling

Target:

```text
60 FPS
```

during ordinary use.

## Presentation page change

If next page is cached:

```text
< 50 ms
```

target.

## Idle CPU

Target:

```text
Near 0%
```

when application is idle.

## Memory

For typical ~100 page PDF:

Target:

```text
< 200 MB
```

where practical.

Do not sacrifice responsiveness purely to hit a memory number.

---

# 26. Performance Philosophy

Do not optimize for:

```text
Render every page as fast as possible
```

Optimize for:

```text
User input must never feel blocked
```

The UI should remain responsive even if the renderer is busy.

---

# 27. Memory Cache

Implement an LRU-style bitmap cache.

Possible cache key:

```cpp
struct PageRenderKey {
    int pageIndex;
    int width;
    int height;
    float scale;
    int rotation;
};
```

Consider caching:

- low-resolution render
- high-resolution render

Set internal memory budget.

Example initial target:

```text
128 MB bitmap cache
```

Then tune using real tests.

Memory pressure should trigger cache eviction.

---

# 28. Render Scheduling Priority

Suggested priorities:

```text
Priority 0: current visible region
Priority 1: visible pages
Priority 2: next presentation page
Priority 3: adjacent pages
Priority 4: background pre-render
```

Cancel obsolete jobs.

Example:

User quickly changes zoom from:

```text
100%
120%
150%
```

Do not continue rendering obsolete 100% and 120% pages unnecessarily.

---

# 29. File Handling

Handle:

- Password-protected PDF
- Invalid PDF
- Corrupted file
- Missing file
- Very large PDF
- Image-heavy scanned PDF
- PDFs containing mixed page sizes

Messages must be simple.

Examples:

```text
This PDF requires a password.
```

```text
The PDF could not be opened.
```

Do not display technical PDFium error codes to normal users.

Technical details may be written to logs.

---

# 30. Logging

Provide lightweight diagnostic logging.

Suggested:

```text
logs/app.log
```

Log:

- Startup
- File open
- Renderer initialization
- PDFium errors
- Print errors
- Conversion errors
- Unexpected exceptions

Do not log user document contents.

Allow logs to rotate.

---

# 31. Crash Resilience

The application must not crash because one page fails to render.

If rendering fails:

```text
Unable to render this page.
```

Other pages should remain usable.

Conversion failures should not corrupt existing user files.

Write converted output to a temporary file first when appropriate.

---

# 32. Settings Philosophy

V1 should have very few settings.

Potential settings:

```text
Default view:
- Fit Width
- Fit Page

Remember last page:
- On / Off

Theme:
- System
- Light
- Dark
```

Do not create a large Settings screen.

---

# 33. V1 Scope

Implement only:

## Viewer

- Open PDF
- Drag & drop
- Continuous scrolling
- Single page
- Fit Width
- Fit Page
- Zoom
- Page navigation
- Search
- Recent files

## Present

- Fullscreen
- Previous / Next page
- Pre-render
- Fast page change

## Screenshot

- Area capture
- Clipboard
- Save PNG

## Convert

- PDF -> PNG
- Image -> PDF

## Print

- Printer selection
- Page range
- Paper size
- Fit
- Actual size
- Auto orientation
- Copies
- Preview

---

# 34. Explicit Non-Goals for V1

Do NOT implement yet:

- PDF editing
- Text editing inside PDF
- Annotation
- Drawing
- PDF forms editor
- Digital signing
- OCR
- AI features
- Cloud sync
- User accounts
- Collaboration
- Bookmark editor
- Advanced document organizer
- PDF compression
- PDF merge
- PDF split
- Password removal
- Complex color management UI

These may be considered later.

Do not allow scope creep.

---

# 35. Development Order

Implement in this order.

## Phase 1 — Application Shell

Build:

- Main window
- PDFium initialization
- Open PDF
- Render one page
- Basic Direct2D display

Success criteria:

```text
PDF opens and one page displays correctly.
```

---

## Phase 2 — Viewer Core

Build:

- Continuous page layout
- Virtualized page system
- Scrolling
- Fit Width
- Fit Page
- Zoom
- Page anchor

Success criteria:

```text
Large PDFs can be scrolled without UI blocking.
```

---

## Phase 3 — Async Renderer

Build:

- Worker pool
- Render queue
- Cache
- Job priorities
- Job cancellation
- Adaptive rendering

Success criteria:

```text
Scrolling and zoom remain responsive during rendering.
```

---

## Phase 4 — Presentation

Build:

- Fullscreen
- Keyboard controls
- Current/next/previous cache
- Fast page switching

Success criteria:

```text
Cached presentation pages change instantly.
```

---

## Phase 5 — Screenshot

Build:

- Selection overlay
- Bitmap capture
- Clipboard
- Save PNG

---

## Phase 6 — Conversion

Build:

- PDF -> PNG
- Multi-page batch conversion
- Image -> PDF
- Drag/reorder image list
- Progress
- Cancel

---

## Phase 7 — Printing

Build:

- Printer selection
- Print preview
- Scaling
- Orientation
- Page ranges
- Copies

Test with real printers and Microsoft Print to PDF.

---

## Phase 8 — Search + Recent Files

Build:

- Ctrl + F
- Search navigation
- Recent documents
- Restore last page

---

## Phase 9 — Performance Optimization

Measure:

- Startup
- first-page latency
- memory
- scrolling frame rate
- render queue
- cache hit ratio
- presentation switching

Do not optimize blindly.

Measure first.

---

# 36. Acceptance Criteria

V1 is ready only when all of the following are true.

## Viewer

- [ ] Application opens quickly
- [ ] PDF loads correctly
- [ ] Continuous scrolling is smooth
- [ ] No visible page jumping during normal scroll
- [ ] Zoom does not unexpectedly relocate the viewport
- [ ] Fit Width works correctly
- [ ] Fit Page works correctly
- [ ] Large PDFs do not freeze the UI
- [ ] Rendering work happens asynchronously

## Present

- [ ] Enter fullscreen with one action
- [ ] No toolbar in presentation
- [ ] Next/previous controls work
- [ ] Next cached page appears almost instantly
- [ ] Escape exits presentation

## Screenshot

- [ ] User can select an area
- [ ] Screenshot automatically copies to clipboard
- [ ] PNG can be saved

## Conversion

- [ ] PDF -> PNG works
- [ ] Page ranges work
- [ ] 150 DPI Standard works
- [ ] 300 DPI High works
- [ ] Image -> PDF works
- [ ] Aspect ratio is preserved
- [ ] Multiple images can be reordered

## Print

- [ ] Printer selection works
- [ ] A4 printing works
- [ ] Auto orientation works
- [ ] Fit works
- [ ] Actual size works
- [ ] Custom page range works
- [ ] Multiple copies work
- [ ] Print preview matches print result closely

---

# 37. Testing Documents

Test using several PDF types.

## Document A

```text
1–5 page text PDF
```

Purpose:

- startup
- first page

## Document B

```text
100+ page textbook PDF
```

Purpose:

- scrolling
- memory
- search

## Document C

```text
500–1000 page document
```

Purpose:

- virtualization
- cache

## Document D

```text
High-resolution scanned PDF
```

Purpose:

- renderer load

## Document E

```text
Mixed portrait + landscape pages
```

Purpose:

- page layout
- presentation
- printing

## Document F

```text
Mixed page sizes
```

Purpose:

- scaling logic

## Document G

```text
Password-protected PDF
```

Purpose:

- error handling

---

# 38. Benchmarking

Add optional internal diagnostics.

Measure:

```text
startup_ms
pdf_open_ms
first_page_render_ms
render_ms_per_page
cache_hit_rate
cache_memory_mb
frame_time_ms
presentation_page_switch_ms
```

Diagnostic UI does not need to be visible in release builds.

Use debug logs or developer overlay.

---

# 39. UI Design Style

Desired UI style:

- Minimal
- Clean
- Neutral
- Modern
- Native Windows feeling
- Small number of controls
- Large readable document area
- No unnecessary visual effects
- No heavy animations

Avoid:

- Giant ribbon UI
- Dense settings panels
- Excessive gradients
- Large icons everywhere
- Multiple sidebars
- Modal dialogs for simple operations

---

# 40. UX Rule

Whenever possible:

```text
One action = one result
```

Examples:

```text
Present
-> fullscreen immediately
```

```text
Screenshot
-> select
-> copied
```

```text
Drop PDF
-> open
```

```text
Drop images
-> Create PDF
```

```text
Print
-> standard print screen
```

Do not require unnecessary confirmation steps.

---

# 41. Code Quality Requirements

Use:

- RAII
- smart pointers
- deterministic resource cleanup
- const correctness
- clear ownership
- scoped Windows handles
- thread-safe queues
- cancellation tokens where needed

Avoid:

- global mutable state
- raw owning pointers
- hidden thread lifetimes
- blocking UI calls
- unnecessary singletons
- excessive templates
- premature abstractions

---

# 42. Build System

Recommended:

- CMake

Example:

```text
CMakeLists.txt
src/
third_party/
assets/
tests/
```

Support:

```text
Debug
Release
```

Prefer static/runtime packaging choices that keep deployment simple.

---

# 43. Dependency Policy

Keep dependencies minimal.

Every third-party dependency should have a clear reason.

Preferred dependency set:

```text
PDFium
Windows API
Direct2D
DirectWrite
WIC
```

Do not introduce large UI frameworks unless absolutely necessary.

---

# 44. Packaging

Desired release:

```text
FastPDF.exe
```

Optional:

```text
FastPDF-Setup.exe
```

Goals:

- Small install size
- No administrator rights if avoidable
- Fast uninstall
- No background service
- No tray app
- No auto-start
- No telemetry by default

---

# 45. Security

The application opens untrusted PDF files.

Therefore:

- Validate file operations
- Avoid unsafe pointer operations
- Keep PDFium version current
- Avoid executing embedded JavaScript in V1
- Do not launch embedded files automatically
- Do not execute external programs from PDF content
- Do not silently open web links

Use conservative defaults.

---

# 46. Source Control

Use Git.

Initial branches:

```text
main
develop
```

Recommended commits should be small and descriptive.

Examples:

```text
feat: initialize PDFium
feat: render PDF page with Direct2D
feat: add virtualized page layout
perf: cancel stale zoom render jobs
fix: preserve scroll anchor during zoom
```

---

# 47. Initial Milestone

The first usable milestone should contain only:

```text
Open PDF
Continuous View
Smooth Scroll
Zoom
Fit Width
Fit Page
Presentation
```

Do not begin Convert or Print until the viewer architecture is stable.

The Viewer is the foundation of the entire project.

---

# 48. Definition of "Good"

This project is successful when a user thinks:

> "This PDF program opens quickly, feels smooth, and I immediately know how to use it."

Not:

> "This application has many features."

The competitive advantage is:

```text
Speed
Responsiveness
Simplicity
Predictability
```

---

# 49. AI Coding Instructions

When implementing this project:

1. Do not change the product scope without a technical reason.
2. Do not add unrelated features.
3. Prioritize viewer performance before utility features.
4. Keep UI minimal.
5. Prefer native Windows APIs.
6. Keep rendering outside the UI thread.
7. Implement measurable performance instrumentation.
8. Use real PDFs for testing.
9. Before large refactors, explain the technical reason.
10. Do not replace PDFium or the native architecture without demonstrating a clear advantage.
11. Implement in small milestones.
12. Keep the application runnable after every milestone.
13. Add tests where practical.
14. Avoid placeholder implementations in production paths.
15. When uncertain, prefer simpler behavior.

---

# 50. First Task for the Coding Agent

Start with **Phase 1 only**.

Create:

```text
Windows x64 C++20 project
CMake build system
Win32 application shell
Direct2D initialization
PDFium integration
File Open dialog
PDF loading
Render first page
Fit page to application window
```

Expected result:

```text
Launch FastPDF.exe
-> Ctrl+O
-> choose PDF
-> first page displays correctly
-> resizing window resizes page correctly
-> application remains stable
```

Do not implement scrolling, presentation, screenshot, conversion, or printing until Phase 1 is stable.

At the end of Phase 1, provide:

```text
1. Current architecture
2. File/folder structure
3. Build instructions
4. Dependencies
5. Known limitations
6. Performance measurements
7. Next recommended task
```

---

# Final Product Direction

Keep asking:

> Does this make the PDF experience faster, simpler, or more predictable?

If not, it probably does not belong in V1.
