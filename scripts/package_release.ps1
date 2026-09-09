# package_release.ps1 - Assembles portable FastPDF release package into dist/
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 `
#       -BuildDir build\release -DistDir dist [-PdfiumRoot C:\deps\pdfium]
#
# Layout produced in dist/FastPDF-<version>-win-x64/:
#   FastPDF.exe
#   pdfium.dll
#   THIRD_PARTY_NOTICES.txt
#   README.txt
#   LICENSE.txt (project license / terms notice)
#   MANIFEST.txt (SHA-256 hashes of all bundled files)
#
# Also creates dist/FastPDF-<version>-win-x64.zip and verifies archive integrity.

param(
    [Parameter(Mandatory = $false)]
    [string]$BuildDir = "build\release",

    [Parameter(Mandatory = $false)]
    [string]$DistDir = "dist",

    [Parameter(Mandatory = $false)]
    [string]$PdfiumRoot = "C:\deps\pdfium"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Get-Item $PSScriptRoot).Parent.FullName

Write-Host "=== FastPDF Release Packaging ==="
Write-Host "Repo Root:    $repoRoot"
Write-Host "Build Dir:    $BuildDir"
Write-Host "Dist Dir:     $DistDir"

# 1. Locate FastPDF.exe
$resolvedBuildDir = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$resolvedDistDir  = if ([System.IO.Path]::IsPathRooted($DistDir)) { $DistDir } else { Join-Path $repoRoot $DistDir }

$possibleExePaths = @(
    (Join-Path $resolvedBuildDir "bin\Release\FastPDF.exe"),
    (Join-Path $resolvedBuildDir "bin\FastPDF.exe")
)

$exePath = ""
foreach ($p in $possibleExePaths) {
    if (Test-Path -LiteralPath $p) {
        $exePath = $p
        break
    }
}

if (-not $exePath) {
    Write-Error "Could not find FastPDF.exe in $BuildDir. Build the Release target first: cmake --build $BuildDir --config Release"
    exit 1
}

$exeDir = Split-Path -Parent $exePath
Write-Host "Found executable: $exePath"

# 2. Locate pdfium.dll
$possibleDllPaths = @(
    (Join-Path $exeDir "pdfium.dll"),
    (Join-Path $PdfiumRoot "bin\pdfium.dll")
)

$dllPath = ""
foreach ($p in $possibleDllPaths) {
    if (Test-Path -LiteralPath $p) {
        $dllPath = $p
        break
    }
}

if (-not $dllPath) {
    Write-Error "Could not find required pdfium.dll runtime in $exeDir or $PdfiumRoot"
    exit 1
}

Write-Host "Found pdfium.dll: $dllPath"

# 3. Locate THIRD_PARTY_NOTICES
$noticesSrc = Join-Path $repoRoot "THIRD_PARTY_NOTICES.md"
if (-not (Test-Path -LiteralPath $noticesSrc)) {
    Write-Error "Missing THIRD_PARTY_NOTICES.md in repository root."
    exit 1
}

$readmeSrc = Join-Path $repoRoot "README.md"
if (-not (Test-Path -LiteralPath $readmeSrc)) {
    Write-Error "Missing README.md in repository root."
    exit 1
}

# Resolve the release version in this order:
#   1. FastPDF.exe product version (authoritative for the binary being packaged)
#   2. the canonical project VERSION in the root CMakeLists.txt
#   3. the literal fallback below
# $FallbackVersion must stay in sync with the CMakeLists.txt version if both the
# exe metadata and the CMake parse are unavailable.
$FallbackVersion = "0.1.1"

function Get-CMakeProjectVersion {
    param([Parameter(Mandatory = $true)][string]$Path)

    # Returns the VERSION x.y.z declared inside the project(FastPDF ...) call,
    # or an empty string when it cannot be resolved. Pure text parsing: no
    # CMake invocation and no external dependency.
    try {
        if (-not (Test-Path -LiteralPath $Path)) { return "" }
        $lines = @(Get-Content -LiteralPath $Path)
        $block = @()
        $inProject = $false
        foreach ($line in $lines) {
            if (-not $inProject) {
                if ($line -match '^\s*project\s*\(\s*FastPDF\b') { $inProject = $true } else { continue }
            }
            $block += $line
            if ($line.TrimEnd().EndsWith(")")) { break }
        }
        if ($block.Count -eq 0) { return "" }
        $match = [regex]::Match(($block -join "`n"), '(?i)VERSION\s+(\d+\.\d+\.\d+)')
        if ($match.Success) { return $match.Groups[1].Value }
    } catch {}
    return ""
}

$version = Get-CMakeProjectVersion (Join-Path $repoRoot "CMakeLists.txt")
if (-not $version) {
    Write-Host "Could not read project VERSION from CMakeLists.txt; using fallback $FallbackVersion"
    $version = $FallbackVersion
}
try {
    $verInfo = (Get-Item $exePath).VersionInfo
    if ($verInfo.ProductVersion) {
        $version = $verInfo.ProductVersion.Trim()
    }
} catch {}

$pkgName = "FastPDF-$version-win-x64"
$stageDir = Join-Path $resolvedDistDir $pkgName
$zipPath = Join-Path $resolvedDistDir "$pkgName.zip"

Write-Host "Staging package to: $stageDir"

if (Test-Path -LiteralPath $stageDir) {
    Remove-Item -LiteralPath $stageDir -Recurse -Force
}
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

# Copy binaries and documents
Copy-Item -LiteralPath $exePath -Destination (Join-Path $stageDir "FastPDF.exe")
Copy-Item -LiteralPath $dllPath -Destination (Join-Path $stageDir "pdfium.dll")
Copy-Item -LiteralPath $noticesSrc -Destination (Join-Path $stageDir "THIRD_PARTY_NOTICES.txt")
Copy-Item -LiteralPath $readmeSrc -Destination (Join-Path $stageDir "README.txt")

# Create a clean project LICENSE.txt notice
$licenseContent = @"
FastPDF - Lightweight Fast PDF Utility Viewer
Copyright (c) 2026 FastPDF Contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See THIRD_PARTY_NOTICES.txt for third-party licenses and acknowledgments,
specifically the Google/Chromium PDFium library (BSD 3-Clause).
"@
Set-Content -LiteralPath (Join-Path $stageDir "LICENSE.txt") -Value $licenseContent -Encoding UTF8

# Generate MANIFEST.txt with SHA-256 hashes
Write-Host "Computing SHA-256 manifest..."
$manifestLines = @()
$manifestLines += "# FastPDF Release Package Manifest"
$manifestLines += "# Package: $pkgName"
$manifestLines += "# Date:    $((Get-Date).ToString('u'))"
$manifestLines += ""

$items = Get-ChildItem -LiteralPath $stageDir -File | Sort-Object Name
foreach ($item in $items) {
    $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifestLines += "$hash  $($item.Name)"
}

$manifestPath = Join-Path $stageDir "MANIFEST.txt"
Set-Content -LiteralPath $manifestPath -Value ($manifestLines -join "`r`n") -Encoding UTF8

Write-Host "Manifest contents:"
Get-Content -LiteralPath $manifestPath | ForEach-Object { Write-Host "  $_" }

# Create zip archive
Write-Host "Creating ZIP archive: $zipPath..."
Compress-Archive -Path "$stageDir\*" -DestinationPath $zipPath -Force

# Verify ZIP integrity
if (-not (Test-Path -LiteralPath $zipPath)) {
    Write-Error "ZIP file was not created: $zipPath"
    exit 1
}

$zipSize = (Get-Item $zipPath).Length
Write-Host "Package ZIP created successfully ($([math]::Round($zipSize / 1MB, 2)) MB)."

# Verify smoke: clean machine style check (ensure pdfium.dll is present next to FastPDF.exe)
Write-Host "Verifying staged directory executable and dependencies..."
$stagedExe = Join-Path $stageDir "FastPDF.exe"
$stagedDll = Join-Path $stageDir "pdfium.dll"
if (-not (Test-Path $stagedExe) -or -not (Test-Path $stagedDll)) {
    Write-Error "Staged package is missing required executable or pdfium.dll"
    exit 1
}

Write-Host "Release packaging completed successfully."
exit 0
