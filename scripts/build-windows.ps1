[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',
    [switch] $Package
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$muPdfLibrary = Join-Path $repositoryRoot "third_party\mupdf\build\x64\$Configuration\mupdfcpp64.lib"
if (-not (Test-Path $muPdfLibrary)) {
    & (Join-Path $PSScriptRoot 'bootstrap-mupdf.ps1') -Configuration $Configuration
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio 2022 was not found.' }
$visualStudio = (& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath).Trim()
$msbuild = Join-Path $visualStudio 'MSBuild\Current\Bin\amd64\MSBuild.exe'
$solution = Join-Path $repositoryRoot 'PdfEditor.sln'

$arguments = @(
    $solution, '/restore', '/t:Build', '/m', '/nologo',
    "/p:Configuration=$Configuration", '/p:Platform=x64'
)
if ($Package) {
    $arguments += '/p:GenerateAppxPackageOnBuild=true'
    $arguments += '/p:AppxPackageSigningEnabled=false'
}

& $msbuild @arguments
if ($LASTEXITCODE -ne 0) { throw 'The Windows solution build failed.' }

$testExecutable = Join-Path $repositoryRoot "out\x64\$Configuration\PdfEditor.Tests.exe"
if (-not (Test-Path $testExecutable)) { throw "The test executable was not found at $testExecutable." }
& $testExecutable
if ($LASTEXITCODE -ne 0) { throw 'The C++ test suite failed.' }

Write-Host "PdfEditor $Configuration build and tests completed successfully." -ForegroundColor Green
if ($Package) {
    Write-Host 'An unsigned development MSIX was generated under src\PdfEditor.App\AppPackages.'
}
