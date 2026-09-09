# verify_pdfium.ps1 - verifies a downloaded pinned PDFium artifact.
#
# Checks the expected layout (include/, lib/, bin/) and prints the SHA-256 of
# pdfium.dll so it can be compared against third_party/pdfium/PDFIUM_LOCK.md.
# Pass -ExpectedSha256 to enforce a checksum (exit code 1 on mismatch).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\verify_pdfium.ps1 `
#       -PdfiumRoot C:\deps\pdfium [-ExpectedSha256 <hex>]

param(
    [Parameter(Mandatory = $true)]
    [string]$PdfiumRoot,

    [string]$ExpectedSha256 = ""
)

$ErrorActionPreference = "Stop"

$required = @(
    "include\fpdfview.h",
    "lib\pdfium.dll.lib",
    "bin\pdfium.dll"
)

$missing = @()
foreach ($rel in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $PdfiumRoot $rel))) {
        $missing += $rel
    }
}

if ($missing.Count -gt 0) {
    Write-Error "PDFium artifact at '$PdfiumRoot' is incomplete. Missing: $($missing -join ', ')"
    exit 1
}

$dll = Join-Path $PdfiumRoot "bin\pdfium.dll"
$hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "pdfium.dll SHA-256: $hash"

if ($ExpectedSha256) {
    $expected = $ExpectedSha256.ToLowerInvariant()
    if ($hash -ne $expected) {
        Write-Error "Checksum mismatch. Expected: $expected"
        exit 1
    }
    Write-Host "Checksum OK."
} else {
    Write-Host "No expected checksum supplied. Compare the hash above with"
    Write-Host "third_party/pdfium/PDFIUM_LOCK.md and pass -ExpectedSha256 to enforce."
}

exit 0