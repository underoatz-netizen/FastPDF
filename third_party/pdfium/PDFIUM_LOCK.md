# PDFium Integration Lock

This file is the checked-in integration contract for the PDFium dependency of
FastPDF. It pins the required revision, architecture, expected files, source
process, and checksum procedure. **FastPDF never downloads PDFium and never
falls back to an unknown system copy.** Configuration fails with an actionable
message when `PDFIUM_ROOT` is unset or invalid (see `cmake/FindPDFium.cmake`).

> **Status: VERIFIED 2026-09-08.** The SHA-256 values below were produced by
> `scripts/verify_pdfium.ps1` on a local copy of the pinned artifact and
> cross-checked against the SLSA provenance attestation published with the
> immutable release (see section 4). The artifact is cached locally at
> `C:\deps\pdfium` (outside the repository; never committed).

## 1. Pinned revision

| Field | Value |
| --- | --- |
| Source | `bblanchon/pdfium-binaries` (official prebuilt PDFium releases) |
| Release tag | `chromium/8035` |
| PDFium version | 154.0.8035.0 |
| Release date | 2026-08-31 |
| Artifact | `pdfium-win-x64.tgz` |
| Build flavor | **non-V8** (JavaScript/V8/XFA disabled) — required for release |
| Immutable artifact URL | `https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F8035/pdfium-win-x64.tgz` |
| Provenance URL | `https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F8035/pdfium-attestation.json` |
| Release page | `https://github.com/bblanchon/pdfium-binaries/releases/tag/chromium/8035` |

The revision above is the pin this project targets. If a newer revision is
required (e.g. a security fix), update this lock file in the same commit that
updates the artifact, and re-run the verification procedure.

## 2. Architecture

- Windows 10/11, **x64 only** (the project refuses to configure for any other
  architecture).
- Toolchain: Visual Studio 2022 (v143), Windows SDK >= 10.0.19041.

## 3. Expected files after extraction

`pdfium-win-x64.tgz` extracts to a directory that must contain exactly this
layout (this is what `PDFIUM_ROOT` must point at):

```text
<PDFIUM_ROOT>/
  include/
    fpdfview.h          (plus sibling headers: fpdf_doc.h, fpdf_text.h, ...)
  lib/
    pdfium.dll.lib      (import library)
  bin/
    pdfium.dll          (runtime DLL)
```

`cmake/FindPDFium.cmake` validates all three paths and fails configuration
with a clear message when any is missing.

## 4. Source process (how the artifact is obtained and verified)

1. Go to <https://github.com/bblanchon/pdfium-binaries/releases> and open the
   release tagged `chromium/8035`.
2. Download `pdfium-win-x64.tgz` **and** `pdfium-attestation.json` (SLSA
   provenance for the release) from the immutable release URLs in section 1.
3. Extract the archive into a directory of your choice, e.g. `C:\deps\pdfium`
   (Windows 10+ ships `tar`; 7-Zip also works).
4. Verify the extraction matches the layout in section 3.
5. Run the checksum procedure in section 5.
6. Set `PDFIUM_ROOT` to the extracted directory and configure:
   `cmake --preset debug -DPDFIUM_ROOT=C:/deps/pdfium`

Do **not** download PDFium from any other source, and do not use an unpinned
"latest" build.

### Provenance (verified 2026-09-08)

The `pdfium-attestation.json` asset is a SLSA provenance v1 statement
(`https://slsa.dev/provenance/v1`) wrapped in a DSSE envelope signed by the
Sigstore/GitHub Actions identity. The certificate binds the attestation to:

- Builder: `https://github.com/bblanchon/pdfium-binaries/.github/workflows/build-all.yml@refs/heads/master`
- Source repository: `bblanchon/pdfium-binaries`
- Source repository digest: `5453f3afc4785cbad82c05f6ceb4dabea0cb81a0`
  (the commit the `chromium/8035` release was built from)
- Rekor transparency log entry: logIndex `2664026297`

The attestation's subject list contains the exact SHA-256 of every release
asset. The digest for `pdfium-win-x64.tgz` is
`61513d611ad200a383456140739be77d156f1e3a2eef22bd89f6c3bda79bdd41`, and the
locally downloaded archive matches it byte-for-byte (see section 5). The
artifact's `args.gn` confirms the build flavor: `pdf_enable_v8 = false`,
`pdf_enable_xfa = false`, `target_cpu = "x64"`, `target_os = "win"`.

The attestation can be independently checked with `slsa-verifier` (see the
pdfium-binaries README) or by inspecting the DSSE envelope payload.

## 5. SHA-256 verification

The pinned SHA-256 values below were computed locally on 2026-09-08 with
`scripts/verify_pdfium.ps1` and cross-checked against the SLSA provenance
subject digests from `pdfium-attestation.json`.

```powershell
# Computes and prints the SHA-256 of the three expected files.
powershell -ExecutionPolicy Bypass -File scripts\verify_pdfium.ps1 -PdfiumRoot C:\deps\pdfium
```

Then record the printed hashes in the table below and, for the DLL, enforce
them at configure time:

```powershell
cmake --preset debug -DPDFIUM_ROOT=C:/deps/pdfium `
  -DPDFIUM_EXPECTED_SHA256=<sha256 of pdfium.dll from the table>
```

| File | SHA-256 (verified 2026-09-08) |
| --- | --- |
| `bin/pdfium.dll` | `ccfac1aad9e78624ebfb3f54f3f4ddb77af6db2f52803f150e2f9876beda49fe` |
| `lib/pdfium.dll.lib` | `8d87791fc1a088528ee81ddd7a9aa94798c0a41da1c0f2bd4b2b3cec7ac74990` |
| `include/fpdfview.h` | `3c563e6851d86e7ed834e5df369f84990b5e1198631d60e806e9b6406a61d32f` |

Configure fails with a checksum-mismatch error when `PDFIUM_EXPECTED_SHA256`
is set and does not match. When it is not set, configure emits a warning and
the artifact must still be verified manually before shipping.

## 6. License and notices

- PDFium itself is BSD 3-Clause (see `THIRD_PARTY_NOTICES.md`).
- The artifact archive ships its own `LICENSE` (MIT, for the packaging by
  Benoit Blanchon) and a `licenses/` directory with the license texts of all
  bundled third-party components (abseil, freetype, icu, libjpeg_turbo,
  libopenjpeg, libpng, zlib, and others). These notices are distributed with
  the artifact and must accompany any redistribution of `pdfium.dll`.

## 7. Runtime behavior

- With `FASTPDF_WITH_PDFIUM=ON` (default): `pdfium.dll` is copied next to
  `FastPDF.exe` and the smoke-test executable at build time. The application
  initializes PDFium through the `fastpdf_pdfium` RAII adapter and reports the
  SDK version in the window status line.
- With `FASTPDF_WITH_PDFIUM=OFF`: the `fastpdf_pdfium` boundary compiles a
  stub; the application shell remains fully runnable and reports that PDFium
  is not available. No PDFium symbols are referenced.