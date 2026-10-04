[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sourceDirectory = Join-Path $repositoryRoot 'third_party\mupdf\src'
$buildDirectory = Join-Path $repositoryRoot "third_party\mupdf\build\x64\$Configuration"
$expectedCommit = '8ad45e92f0935d3d87f1db3f873086472a5e1b24'
$tag = '1.28.5'

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git is required to fetch the pinned MuPDF source.'
}

if (-not (Test-Path (Join-Path $sourceDirectory '.git'))) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $sourceDirectory) | Out-Null
    & git clone --branch $tag --depth 1 --recurse-submodules https://github.com/ArtifexSoftware/mupdf.git $sourceDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Cloning MuPDF failed.' }
}

$actualCommit = (& git -C $sourceDirectory rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $expectedCommit) {
    throw "MuPDF must be tag $tag at commit $expectedCommit. Found $actualCommit. Move the existing source directory aside and rerun this script."
}

& git -C $sourceDirectory submodule update --init --recursive --depth 1
if ($LASTEXITCODE -ne 0) { throw 'Updating MuPDF submodules failed.' }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'Visual Studio 2022 with Desktop development with C++ is required.'
}
$visualStudio = (& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath).Trim()
$msbuild = Join-Path $visualStudio 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path $msbuild)) { throw "MSBuild was not found at $msbuild." }
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw 'Python 3 is required by MuPDF to generate its DLL export list.'
}

# Keep PDF and the Story/HTML layout engine. Compile out document JavaScript,
# OCR output, barcode support, and non-PDF document handlers.
$featureFlags = @(
    '/DFZ_ENABLE_PDF=1', '/DFZ_ENABLE_HTML_ENGINE=1', '/DFZ_ENABLE_JS=0',
    '/DFZ_ENABLE_XPS=0', '/DFZ_ENABLE_SVG=0', '/DFZ_ENABLE_CBZ=0', '/DFZ_ENABLE_IMG=0',
    '/DFZ_ENABLE_HTML=0', '/DFZ_ENABLE_MD=0', '/DFZ_ENABLE_EPUB=0', '/DFZ_ENABLE_FB2=0',
    '/DFZ_ENABLE_MOBI=0', '/DFZ_ENABLE_TXT=0', '/DFZ_ENABLE_OFFICE=0',
    '/DFZ_ENABLE_OCR_OUTPUT=0', '/DFZ_ENABLE_DOCX_OUTPUT=0', '/DFZ_ENABLE_ODT_OUTPUT=0',
    '/DFZ_ENABLE_BARCODE=0'
) -join ' '

$previousCl = $env:CL
try {
    $env:CL = "$previousCl $featureFlags".Trim()
    $solution = Join-Path $sourceDirectory 'platform\win32\mupdf.sln'
    & $msbuild $solution /t:mupdfcpp /m /nologo `
        "/p:Configuration=$Configuration" /p:Platform=x64 /p:PlatformToolset=v143
    if ($LASTEXITCODE -ne 0) { throw "MuPDF $Configuration build failed." }
}
finally {
    $env:CL = $previousCl
}

$muPdfOutput = Join-Path $sourceDirectory "platform\win32\x64\$Configuration"
$library = Join-Path $muPdfOutput 'mupdfcpp64.lib'
$runtime = Join-Path $muPdfOutput 'mupdfcpp64.dll'
if (-not (Test-Path $library) -or -not (Test-Path $runtime)) {
    throw "The MuPDF build completed but its DLL/import library were not found in $muPdfOutput."
}

New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
Copy-Item -Force $library, $runtime -Destination $buildDirectory
$symbols = Join-Path $muPdfOutput 'mupdfcpp64.pdb'
if (Test-Path $symbols) { Copy-Item -Force $symbols -Destination $buildDirectory }

Write-Host "MuPDF $tag ($expectedCommit) is ready in $buildDirectory" -ForegroundColor Green
