[CmdletBinding()]
param(
    [switch] $SkipLaunchTest,
    [switch] $CreateSourceArchive
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$version = '0.1.0'
$artifactName = "PDF-Textbox-Editor-$Version-win-x64-portable"
$outputRoot = Join-Path $repositoryRoot 'out\portable\x64\Release'
$applicationOutput = Join-Path $outputRoot 'PdfEditor.App'
$distributionRoot = Join-Path $repositoryRoot 'dist'
$stagingDirectory = Join-Path $distributionRoot 'PDF Textbox Editor'
$archivePath = Join-Path $distributionRoot "$artifactName.zip"
$checksumPath = "$archivePath.sha256"

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory)] [string] $Executable,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]] $Arguments,
        [Parameter(Mandatory)] [string] $FailureMessage
    )

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw $FailureMessage
    }
}

function Copy-RuntimeTree {
    param(
        [Parameter(Mandatory)] [string] $Source,
        [Parameter(Mandatory)] [string] $Destination
    )

    $excludedExtensions = @('.pdb', '.ilk', '.lib', '.exp', '.iobj', '.ipdb', '.appx', '.msix')
    Get-ChildItem -LiteralPath $Source -Recurse -File | ForEach-Object {
        if ($excludedExtensions -notcontains $_.Extension.ToLowerInvariant()) {
            $relativePath = $_.FullName.Substring($Source.Length).TrimStart([char]'\', [char]'/')
            $destinationPath = Join-Path $Destination $relativePath
            $destinationDirectory = Split-Path -Parent $destinationPath
            New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $destinationPath -Force
        }
    }
}

function Find-VcRuntimeDirectory {
    param([Parameter(Mandatory)] [string] $VisualStudioRoot)

    $redistRoot = Join-Path $VisualStudioRoot 'VC\Redist\MSVC'
    if (-not (Test-Path $redistRoot)) {
        throw "The Visual C++ redistributable directory was not found under $redistRoot."
    }

    $candidates = Get-ChildItem -LiteralPath $redistRoot -Directory | Sort-Object {
        try { [Version] $_.Name } catch { [Version] '0.0' }
    } -Descending
    foreach ($candidate in $candidates) {
        $runtime = Join-Path $candidate.FullName 'x64\Microsoft.VC143.CRT'
        if (Test-Path (Join-Path $runtime 'vcruntime140.dll')) {
            return $runtime
        }
    }
    throw 'An x64 Microsoft.VC143.CRT redistributable directory was not found.'
}

function Find-Dumpbin {
    param([Parameter(Mandatory)] [string] $VisualStudioRoot)

    $toolsRoot = Join-Path $VisualStudioRoot 'VC\Tools\MSVC'
    if (-not (Test-Path $toolsRoot)) {
        throw "The MSVC tools directory was not found under $toolsRoot."
    }
    $candidates = Get-ChildItem -LiteralPath $toolsRoot -Directory | Sort-Object {
        try { [Version] $_.Name } catch { [Version] '0.0' }
    } -Descending
    foreach ($candidate in $candidates) {
        $dumpbin = Join-Path $candidate.FullName 'bin\Hostx64\x64\dumpbin.exe'
        if (Test-Path $dumpbin) { return $dumpbin }
    }
    throw 'The x64 dumpbin.exe tool was not found in the Visual Studio installation.'
}

function Assert-PortableContents {
    param([Parameter(Mandatory)] [string] $Root)

    $requiredFiles = @(
        'PdfEditor.App.exe',
        'mupdfcpp64.dll',
        'resources.pri',
        'Microsoft.UI.Xaml.dll',
        'vcruntime140.dll',
        'msvcp140.dll',
        'START_HERE.txt',
        'LICENSE',
        'THIRD_PARTY_NOTICES.md',
        'SOURCE_URL.txt',
        'Licenses\MuPDF-AGPL-3.0.txt',
        'Licenses\Microsoft-Windows-App-SDK-License.txt',
        'Licenses\Microsoft-Windows-App-SDK-NOTICE.txt',
        'Assets\Fonts\OFL-NotoSans.txt',
        'Assets\Fonts\OFL-NotoSansMono.txt',
        'Assets\Fonts\OFL-NotoSerif.txt'
    )
    foreach ($relativePath in $requiredFiles) {
        if (-not (Test-Path (Join-Path $Root $relativePath))) {
            throw "The portable build is missing required file: $relativePath"
        }
    }

    $runtimeDlls = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Filter 'Microsoft.WindowsAppRuntime*.dll')
    if ($runtimeDlls.Count -eq 0) {
        throw 'No self-contained Windows App SDK runtime DLLs were found in the portable build.'
    }

    $fontDirectory = Join-Path $Root 'Assets\Fonts'
    $fonts = @(Get-ChildItem -LiteralPath $fontDirectory -File -Filter '*.ttf' -ErrorAction SilentlyContinue)
    if ($fonts.Count -ne 10) {
        throw "Expected 10 bundled fonts in $fontDirectory, but found $($fonts.Count)."
    }

    $forbidden = @(Get-ChildItem -LiteralPath $Root -Recurse -File | Where-Object {
        $_.Extension.ToLowerInvariant() -in @('.pdb', '.ilk', '.lib', '.exp', '.iobj', '.ipdb', '.appx', '.msix')
    })
    if ($forbidden.Count -ne 0) {
        throw "Development-only files were staged: $($forbidden.FullName -join ', ')"
    }
}

function Assert-NativeDependencies {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $Dumpbin
    )

    $stagedNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    Get-ChildItem -LiteralPath $Root -Recurse -File | ForEach-Object {
        [void] $stagedNames.Add($_.Name)
    }

    $missing = [System.Collections.Generic.List[string]]::new()
    $systemDirectory = Join-Path $env:WINDIR 'System32'
    $nativeFiles = @(Get-ChildItem -LiteralPath $Root -Recurse -File | Where-Object {
        $_.Extension -in @('.exe', '.dll')
    })
    foreach ($nativeFile in $nativeFiles) {
        $output = & $Dumpbin /nologo /dependents $nativeFile.FullName
        if ($LASTEXITCODE -ne 0) {
            throw "dumpbin could not inspect $($nativeFile.FullName)."
        }
        foreach ($line in $output) {
            if ($line -notmatch '^\s+([^\s]+\.dll)\s*$') { continue }
            $dependency = $Matches[1]
            if ($dependency -match '^(api-ms-win-|ext-ms-win-)') { continue }
            if ($stagedNames.Contains($dependency)) { continue }
            if (Test-Path (Join-Path $systemDirectory $dependency)) { continue }
            $missing.Add("$($nativeFile.Name) -> $dependency")
        }
    }
    if ($missing.Count -ne 0) {
        throw "Portable native dependencies are missing: $($missing -join ', ')"
    }
}

function Test-PortableLaunch {
    param([Parameter(Mandatory)] [string] $Source)

    $smokeRoot = Join-Path ([System.IO.Path]::GetTempPath()) 'PDF Textbox Editor Portable Smoke'
    if (Test-Path $smokeRoot) { Remove-Item -LiteralPath $smokeRoot -Recurse -Force }
    Copy-Item -LiteralPath $Source -Destination $smokeRoot -Recurse -Force
    $executable = Join-Path $smokeRoot 'PdfEditor.App.exe'
    $process = $null
    try {
        $process = Start-Process -FilePath $executable -WorkingDirectory $smokeRoot -PassThru
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        do {
            Start-Sleep -Milliseconds 250
            $process.Refresh()
            if ($process.HasExited) {
                throw "The portable application exited during startup with code $($process.ExitCode)."
            }
        } while ($process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)

        if ($process.MainWindowHandle -eq 0) {
            throw 'The portable application did not create a main window within 20 seconds.'
        }
        Write-Host 'Portable launch smoke test passed.' -ForegroundColor Green
    }
    finally {
        if ($null -ne $process -and -not $process.HasExited) {
            if (-not $process.CloseMainWindow()) { Stop-Process -Id $process.Id -Force }
            elseif (-not $process.WaitForExit(5000)) { Stop-Process -Id $process.Id -Force }
        }
        if (Test-Path $smokeRoot) { Remove-Item -LiteralPath $smokeRoot -Recurse -Force }
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'Visual Studio 2022 with Desktop development with C++ is required.'
}
$visualStudio = (& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($visualStudio)) {
    throw 'A Visual Studio installation containing MSBuild was not found.'
}
$msbuild = Join-Path $visualStudio 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path $msbuild)) { throw "MSBuild was not found at $msbuild." }

$muPdfLibrary = Join-Path $repositoryRoot 'third_party\mupdf\build\x64\Release\mupdfcpp64.lib'
if (-not (Test-Path $muPdfLibrary)) {
    & (Join-Path $PSScriptRoot 'bootstrap-mupdf.ps1') -Configuration Release
    if ($LASTEXITCODE -ne 0) { throw 'Bootstrapping MuPDF Release failed.' }
}

$solutionDirectory = "$repositoryRoot\"
$commonArguments = @('/restore', '/t:Rebuild', '/m', '/nologo', '/p:Configuration=Release', '/p:Platform=x64', "/p:SolutionDir=$solutionDirectory")

# Build and run the normal Release tests first. The GoogleTest package is built
# against the dynamic CRT, so it intentionally does not participate in the
# separate Hybrid CRT portable build.
$testProject = Join-Path $repositoryRoot 'src\PdfEditor.Tests\PdfEditor.Tests.vcxproj'
Invoke-CheckedCommand -Executable $msbuild -Arguments (@($testProject) + $commonArguments + @('/p:PortableBuild=false')) `
    -FailureMessage 'The Release test build failed.'
$testExecutable = Join-Path $repositoryRoot 'out\x64\Release\PdfEditor.Tests.exe'
if (-not (Test-Path $testExecutable)) { throw "The test executable was not found at $testExecutable." }
Invoke-CheckedCommand -Executable $testExecutable -Arguments @() -FailureMessage 'The C++ test suite failed.'

# Build only the app and its project references with the portable properties.
$applicationProject = Join-Path $repositoryRoot 'src\PdfEditor.App\PdfEditor.App.vcxproj'
$portableArguments = @($applicationProject) + $commonArguments + @('/p:PortableBuild=true')
Invoke-CheckedCommand -Executable $msbuild -Arguments $portableArguments `
    -FailureMessage 'The self-contained portable application build failed.'

if (-not (Test-Path $applicationOutput)) {
    throw "The portable application output was not found at $applicationOutput."
}

New-Item -ItemType Directory -Force -Path $distributionRoot | Out-Null
if (Test-Path $stagingDirectory) { Remove-Item -LiteralPath $stagingDirectory -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stagingDirectory | Out-Null
Copy-RuntimeTree -Source $applicationOutput -Destination $stagingDirectory

$vcRuntimeDirectory = Find-VcRuntimeDirectory -VisualStudioRoot $visualStudio
Copy-Item -Path (Join-Path $vcRuntimeDirectory '*.dll') -Destination $stagingDirectory -Force

Copy-Item -LiteralPath (Join-Path $repositoryRoot 'START_HERE.txt') -Destination $stagingDirectory -Force
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'SOURCE_URL.txt') -Destination $stagingDirectory -Force
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'LICENSE') -Destination $stagingDirectory -Force
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'THIRD_PARTY_NOTICES.md') -Destination $stagingDirectory -Force
$licenseDirectory = Join-Path $stagingDirectory 'Licenses'
New-Item -ItemType Directory -Force -Path $licenseDirectory | Out-Null
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'third_party\mupdf\COPYING') `
    -Destination (Join-Path $licenseDirectory 'MuPDF-AGPL-3.0.txt') -Force
$nugetPackages = if ($env:NUGET_PACKAGES) { $env:NUGET_PACKAGES } else { Join-Path $env:USERPROFILE '.nuget\packages' }
$windowsAppSdkPackage = Join-Path $nugetPackages 'microsoft.windowsappsdk\2.5.1'
$windowsAppSdkLicense = Join-Path $windowsAppSdkPackage 'license.txt'
$windowsAppSdkNotice = Join-Path $windowsAppSdkPackage 'NOTICE.txt'
if (-not (Test-Path $windowsAppSdkLicense) -or -not (Test-Path $windowsAppSdkNotice)) {
    throw "The restored Windows App SDK license files were not found under $windowsAppSdkPackage."
}
Copy-Item -LiteralPath $windowsAppSdkLicense `
    -Destination (Join-Path $licenseDirectory 'Microsoft-Windows-App-SDK-License.txt') -Force
Copy-Item -LiteralPath $windowsAppSdkNotice `
    -Destination (Join-Path $licenseDirectory 'Microsoft-Windows-App-SDK-NOTICE.txt') -Force

Assert-PortableContents -Root $stagingDirectory
$dumpbin = Find-Dumpbin -VisualStudioRoot $visualStudio
Assert-NativeDependencies -Root $stagingDirectory -Dumpbin $dumpbin

if (-not $SkipLaunchTest) {
    Test-PortableLaunch -Source $stagingDirectory
}

if (Test-Path $archivePath) { Remove-Item -LiteralPath $archivePath -Force }
if (Test-Path $checksumPath) { Remove-Item -LiteralPath $checksumPath -Force }
Compress-Archive -LiteralPath $stagingDirectory -DestinationPath $archivePath -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath $checksumPath -Value "$hash  $([System.IO.Path]::GetFileName($archivePath))" -Encoding ascii

if ($CreateSourceArchive) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'Git is required to create the source archive.' }
    if (-not (Test-Path (Join-Path $repositoryRoot '.git'))) { throw 'Create the final source archive from a Git checkout.' }
    $status = (& git -C $repositoryRoot status --porcelain)
    if ($LASTEXITCODE -ne 0 -or $status) { throw 'Commit all source changes before creating the release source archive.' }
    $tag = "v$Version"
    $headCommit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
    $tagCommitOutput = (& git -C $repositoryRoot rev-list -n 1 $tag 2>$null)
    $tagCommit = if ($null -eq $tagCommitOutput) { '' } else { "$tagCommitOutput".Trim() }
    if ($LASTEXITCODE -ne 0 -or $tagCommit -ne $headCommit) {
        throw "Tag $tag must exist and point to HEAD before creating the source archive."
    }
    $sourceArchive = Join-Path $distributionRoot "PDF-Textbox-Editor-$Version-source.zip"
    if (Test-Path $sourceArchive) { Remove-Item -LiteralPath $sourceArchive -Force }
    Invoke-CheckedCommand -Executable 'git' -Arguments @('-C', $repositoryRoot, 'archive', '--format=zip', "--output=$sourceArchive", $tag) `
        -FailureMessage 'Creating the corresponding source archive failed.'
}

Write-Host "Portable folder: $stagingDirectory" -ForegroundColor Green
Write-Host "Portable archive: $archivePath" -ForegroundColor Green
Write-Host "SHA-256: $hash" -ForegroundColor Green
if (-not $CreateSourceArchive) {
    Write-Host "For a public release, tag the clean source as v$Version and rerun with -CreateSourceArchive." -ForegroundColor Yellow
}
