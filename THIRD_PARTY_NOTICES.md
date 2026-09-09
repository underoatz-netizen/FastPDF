# Third-Party Notices

FastPDF uses the following third-party components. This file is copied next to
`FastPDF.exe` at build time by the `fastpdf_third_party_notices` CMake target
(`THIRD_PARTY_NOTICES.txt` in the output directory).

## PDFium

- Project: PDFium (https://pdfium.googlesource.com/pdfium/)
- License: BSD 3-Clause (see below)
- Copyright: The PDFium Authors / Google LLC
- Distribution: FastPDF does **not** bundle PDFium. The application links
  against a developer-supplied, pinned prebuilt artifact from the official
  `bblanchon/pdfium-binaries` releases (see `third_party/pdfium/PDFIUM_LOCK.md`).
  The artifact's own license text ships inside the artifact archive.

```
Copyright 2014 The PDFium Authors. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above
     copyright notice, this list of conditions and the following
     disclaimer in the documentation and/or other materials provided
     with the distribution.
   * Neither the name of Google Inc. nor the names of its
     contributors may be used to endorse or promote products derived
     from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Microsoft Windows SDK components

- Components: Direct2D, DirectWrite, Windows Imaging Component (WIC), and the
  Windows API used by this application.
- License: Microsoft proprietary. These components are part of the Windows
  operating system / Windows SDK and are governed by the Microsoft Software
  License Terms for the Windows SDK. No additional redistribution is required
  for an application that runs on Windows.

## Notice pipeline

- Source of truth: `THIRD_PARTY_NOTICES.md` at the repository root.
- The `fastpdf_third_party_notices` CMake target copies it next to the built
  executable on every build.
- When a dependency is added or its license changes, update this file in the
  same change.