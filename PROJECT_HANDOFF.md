# PDF Editor Milestone 1 — Project Handoff

Last updated: October 4, 2026

Canonical source repository: <https://github.com/monoIith/pdfbeamer>

If the local project folder is missing, restore it with:

```powershell
git clone https://github.com/monoIith/pdfbeamer.git pdfbeamer
Set-Location .\pdfbeamer
```

## Purpose of this document

This file records the current implementation state and provides the information a future developer or Codex chat should need to turn the project into a runnable Windows `.exe` and an installable MSIX package.

The repository was created on macOS. The portable C++ core and the real MuPDF integration have been tested there, but WinUI 3 and MSIX can only be built and exercised on Windows. The next meaningful step is therefore a Windows 11 x64 build, followed by UI and deployment testing.

An additional portable deployment path is now implemented in source. On Windows, `scripts\build-portable.ps1` builds an unpackaged, self-contained Release x64 application, runs the automated tests, stages the runtime beside `PdfEditor.App.exe`, performs a launch smoke test, and creates a ZIP plus SHA-256 checksum under `dist`. This path still requires its first real Windows build and clean-machine launch validation.

## Product scope

This milestone is a native Windows 11 PDF editor focused on one workflow:

1. Open an ordinary, unencrypted `.pdf`.
2. Navigate its pages.
3. Draw one or more textboxes on any page.
4. Type, edit, move, resize, delete, and format those boxes.
5. Prevent saving while text overflows a box.
6. Save a new PDF in which the added text is permanent and searchable.

It is written in C++20 using C++/WinRT and packaged WinUI 3. MuPDF is the PDF rendering, layout, extraction, font, and writing backend.

## Current implementation status

### Completed application work

- A three-project Visual Studio solution exists:
  - `PdfEditor.App`: WinUI 3 interface and input handling.
  - `PdfEditor.Core`: document model, transforms, undo/redo, worker, and MuPDF backend.
  - `PdfEditor.Tests`: portable unit tests and MuPDF integration tests.
- The application shell contains:
  - Open, Save, Save As, Undo, and Redo commands.
  - Select and Text tools.
  - A collapsible thumbnail rail.
  - A virtualized continuous-scroll page area.
  - Fit Width, Fit Page, and 25–400% zoom behavior.
  - Ctrl+mouse-wheel zoom and thumbnail/page synchronization.
  - Light/dark WinUI theming while PDF pages retain their original colors.
- Textbox editing supports:
  - Dragging a new rectangle and typing multiline text inline.
  - Selection, movement, resizing, editing, and deletion.
  - Page-bound clamping and rotated/cropped-page coordinate conversion.
  - Font family, size, color, bold, italic, alignment, border, fill, and opacity.
  - Red overflow feedback and save blocking.
  - Automatic removal of empty new textboxes when editing is abandoned.
  - Keyboard shortcuts and arrow-key nudging.
- Undo/redo supports textbox creation, deletion, text changes, style changes, movement, and resizing. Typing and pointer drags are transaction-coalesced.
- The UI uses a native WinUI `TextBox` only while a box is actively being edited. Committed previews are rasterized using the same MuPDF Story layout path used for overflow validation and export, reducing preview/output wrapping differences.
- Full-resolution page images are generated for the visible page neighborhood. Off-screen full-resolution images are released, while lower-resolution thumbnails are retained.

### Completed PDF backend work

- All MuPDF access is intended to pass through one serialized `PdfWorker` background thread. UI code does not call MuPDF concurrently.
- Input validation includes:
  - Case-insensitive `.pdf` extension checking.
  - `%PDF-` magic checking.
  - Password-protected PDF rejection.
  - Malformed/repaired PDF rejection.
  - Digital-signature detection and a warning before producing a derived file.
  - MuPDF document JavaScript disabled at runtime.
- Page inspection records media box, crop box, and rotation.
- Coordinate storage uses unrotated PDF points relative to each page crop box.
- Rendering produces premultiplied BGRA data suitable for WinUI.
- Text layout uses MuPDF Story with HTML escaping, UTF-16 to UTF-8 conversion, and overflow detection.
- Saving always starts from the immutable source PDF plus the current staged textbox models. Repeated saves therefore do not duplicate previously inserted text.
- On every affected page, the backend appends an overlay Form XObject above the existing content. It draws fill, border, and clipped text in that order.
- Overlay resource names are collision-safe and fonts are embedded/subset through MuPDF.
- Saving refuses to overwrite the source PDF.
- Output is first written to a sibling temporary file, reopened and page-count checked, and then atomically installed at the requested destination. Temporary output is cleaned up on failure.
- Saved text can be extracted after reopening and is ordinary permanent PDF page content rather than an editable application annotation.

### Fonts and licenses

The repository includes the following bundled Noto font files and their OFL license files:

- Noto Sans: Regular, Bold, Italic, BoldItalic.
- Noto Serif: Regular, Bold, Italic, BoldItalic.
- Noto Sans Mono: Regular and Bold.

Noto Sans Mono does not provide matching true italic files in the selected upstream family, so italic and bold-italic variants are synthesized from the upright masters. This is intentional for this milestone.

MuPDF is pinned to version 1.28.5 at commit:

```text
8ad45e92f0935d3d87f1db3f873086472a5e1b24
```

MuPDF is AGPL/commercially licensed. The current arrangement is appropriate for an AGPL-compatible personal prototype. Obtain an appropriate commercial MuPDF license, or replace the backend, before closed-source distribution.

## Verification already completed

The following checks were completed on macOS:

- The portable core and test harness compiled with C++20.
- Actual MuPDF 1.28.5 static libraries were built from the pinned source.
- The complete core was linked against those libraries.
- 15 of 15 unit and integration tests passed.
- The integration test generated, edited, saved, reopened, rendered, and extracted text from a real PDF.
- Tested content included accented Latin, Greek, and Cyrillic text.
- The integration test covered a multipage document, a cropped/rotated page, all three font families and requested styles, fill opacity, and boxes on multiple pages.
- Page count, media/crop geometry, rotation, and expected render dimensions were checked after saving.
- The source PDF was verified byte-for-byte unchanged.
- A save attempt targeting the source path was rejected.
- The MuPDF backend compiled cleanly with `-Wall -Wextra -Wpedantic`.
- XAML, manifests, Visual Studio project files, and `Directory.Build.props` passed XML parsing.

This does **not** prove that the WinUI project compiles or that the resulting package launches. That validation must happen on Windows.

## Repository map

```text
PdfEditor.sln
Directory.Build.props
README.md
THIRD_PARTY_NOTICES.md
PROJECT_HANDOFF.md

scripts/
  bootstrap-mupdf.ps1
  build-windows.ps1
  build-portable.ps1

third_party/mupdf/
  PINNED_VERSION.txt
  COPYING
  src/                    # Created/populated by the bootstrap script
  build/x64/              # Generated MuPDF DLL/import library outputs

src/PdfEditor.App/
  PdfEditor.App.vcxproj
  Package.appxmanifest
  app.manifest
  App.xaml
  App.xaml.cpp/.h
  MainWindow.xaml
  MainWindow.xaml.cpp/.h
  Project.idl
  Assets/
    Fonts/

src/PdfEditor.Core/
  PdfEditor.Core.vcxproj
  include/PdfEditor/Core/
    Models.h
    Geometry.h
    TextMarkup.h
    DocumentSession.h
    PdfBackend.h
    PdfWorker.h
    MuPdfBackend.h
  src/
    Geometry.cpp
    TextMarkup.cpp
    DocumentSession.cpp
    PdfBackend.cpp
    PdfWorker.cpp
    MuPdfBackend.cpp

src/PdfEditor.Tests/
  PdfEditor.Tests.vcxproj
  TestHarness.h
  TestMain.cpp
  GeometryTests.cpp
  DocumentSessionTests.cpp
  TextMarkupTests.cpp
  PdfWorkerTests.cpp
  MuPdfIntegrationTests.cpp
```

## Pinned Windows dependencies

The project files currently pin these packages:

- Windows App SDK 2.5.1.
- Microsoft.Windows.CppWinRT 3.0.260818.1.
- Microsoft.Windows.SDK.BuildTools 10.0.28000.2705.
- GoogleTest native package 1.8.1.8.
- MuPDF 1.28.5 at the exact commit listed above.

Do not casually update these during the first Windows build. Establish a working baseline first; update one dependency at a time afterward.

## Windows machine prerequisites

Use a Windows 11 x64 machine. Install:

1. Visual Studio 2022 with the **Desktop development with C++** workload.
2. MSVC x64 C++ build tools and C++20 support.
3. A Windows 11 SDK compatible with the SDK version pinned by the project.
4. Windows App SDK C++/WinRT and MSIX packaging support.
5. Git for Windows, including submodule support.
6. Python 3 available as `python` on `PATH`; MuPDF uses it to generate its Windows DLL export list.
7. PowerShell 7 or Windows PowerShell 5.1.
8. Developer Mode or a trusted development signing certificate if the package will be installed locally.

Run all build commands from a normal PowerShell prompt whose working directory is the repository root. A Visual Studio Developer PowerShell prompt is preferable if MSBuild discovery fails.

## First Windows build

### 1. Confirm the repository is complete

From the repository root:

```powershell
Get-ChildItem .\PdfEditor.sln
Get-ChildItem .\scripts\bootstrap-mupdf.ps1
Get-ChildItem .\scripts\build-windows.ps1
Get-ChildItem .\src\PdfEditor.App\Assets\Fonts
```

There should be ten `.ttf` font files. Do not proceed if the fonts, application assets, or project files are missing.

### 2. Fetch and build the pinned MuPDF source

```powershell
.\scripts\bootstrap-mupdf.ps1 -Configuration Debug
```

The script is the authoritative setup path. It clones MuPDF into `third_party\mupdf\src`, verifies the exact version recorded in `third_party\mupdf\PINNED_VERSION.txt`, initializes the needed MuPDF submodules, and builds `mupdfcpp64.lib` plus `mupdfcpp64.dll` into `third_party\mupdf\build\x64\<Configuration>`.

If PowerShell blocks local scripts, use a process-scoped policy rather than changing the machine policy permanently:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\bootstrap-mupdf.ps1 -Configuration Debug
```

### 3. Build the solution

```powershell
.\scripts\build-windows.ps1 -Configuration Debug
```

For a release build:

```powershell
.\scripts\bootstrap-mupdf.ps1 -Configuration Release
.\scripts\build-windows.ps1 -Configuration Release
```

The intended platform is `x64`. Do not use Win32 or ARM64 for the first validation.

### 4. Find the generated executable

The checked-in project currently writes the application under `out\x64\<Configuration>\PdfEditor.App`. Confirm the result with:

```powershell
Get-ChildItem .\out\x64 -Recurse -Filter PdfEditor.App.exe |
    Select-Object FullName, Length, LastWriteTime
```

A packaged WinUI 3 program is not necessarily a portable single-file executable. `PdfEditor.App.exe` depends on its generated resources, native libraries, Windows App SDK runtime, and package identity. For normal distribution, produce and sign an MSIX rather than copying only the `.exe`.

### 5. Run from Visual Studio

If command-line compilation succeeds:

1. Open `PdfEditor.sln` in Visual Studio.
2. Select `x64` and `Debug`.
3. Set `PdfEditor.App` as the startup project if it is not already selected.
4. Deploy/run with F5.
5. If Visual Studio reports an unregistered package, enable Developer Mode and use the project’s Deploy command before running.

## Running tests on Windows

Build the test project through the solution, then use Visual Studio Test Explorer. If tests are not discovered, verify that the GoogleTest adapter/package restored successfully and that `PdfEditor.Tests` was built for x64.

At minimum, rerun these categories before changing code:

- Geometry and rotation transforms.
- DocumentSession undo/redo and transaction coalescing.
- HTML escaping and style generation.
- PdfWorker serialization.
- MuPDF save/reopen/extract integration.
- Source-overwrite rejection.

The build script automatically launches the tests after compiling. The expected test path is `out\x64\<Configuration>\PdfEditor.Tests.exe`; it can also be run directly:

```powershell
.\out\x64\Debug\PdfEditor.Tests.exe
```

## Producing an MSIX installer

The repository’s build script accepts the packaging switch:

```powershell
.\scripts\build-windows.ps1 -Configuration Release -Package
```

The generated package is expected to be an unsigned development artifact unless a certificate is configured. The script reports packages under `src\PdfEditor.App\AppPackages`; confirm them with:

```powershell
Get-ChildItem .\src\PdfEditor.App\AppPackages -Recurse -Include *.msix,*.msixbundle,*.appx,*.appxbundle |
    Select-Object FullName, Length, LastWriteTime
```

Before distributing the package:

1. Choose a publisher identity.
2. Set the same publisher value in `Package.appxmanifest` and the signing certificate subject.
3. Obtain or create an appropriate code-signing certificate.
4. Sign the final MSIX with the Windows SDK `signtool.exe` using SHA-256 and a timestamp service suitable for the intended distribution channel.
5. Verify the signature with `signtool verify /pa /v <package-path>`.
6. Install and test the signed package on a clean Windows 11 x64 account or virtual machine.

For Microsoft Store distribution, follow the Store-assigned package identity and signing workflow instead of inventing a permanent production identity locally.

## Required Windows acceptance pass

The following work is still outstanding and should be treated as the release gate:

- Resolve any C++/WinRT generated-code or WinUI API compile errors found by the first Windows build.
- Confirm NuGet restores all pinned packages.
- Confirm MuPDF headers and libraries are found for both Debug and Release x64.
- Launch the packaged application and check for missing DLL/runtime errors.
- Open single-page and multipage PDFs.
- Exercise rotated, cropped, landscape, mixed-size, image-heavy, and vector PDFs.
- Create multiple boxes, type multiline text, and test every formatting control.
- Move, resize, edit, delete, undo, and redo boxes.
- Verify empty new boxes disappear when abandoned.
- Verify overflow is visibly red and both Save and Save As are unavailable until corrected.
- Test Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+Z, Ctrl+Y, Delete, Escape, arrow nudge, and Shift+arrow nudge.
- Test Fit Width, Fit Page, fixed zoom, Ctrl+wheel zoom, thumbnails, and continuous scrolling.
- Confirm dirty-session prompts when opening another file and closing the window.
- Confirm the first save requires Save As and the source path cannot be selected.
- Save, reopen in this app and at least one independent PDF viewer, search/select the inserted text, and compare its placement with the preview.
- Confirm the source file remains unchanged.
- Exercise malformed, encrypted, signed, read-only, and unwritable cases.
- Verify interrupted/failed saves leave no stale temporary file.
- Manually check 100%, 150%, and 200% Windows display scaling in both light and dark themes.
- Test a Release MSIX on a clean Windows 11 x64 machine or VM.

## Known limitations and deliberate exclusions

- Existing PDF text is not editable.
- Boxes remain editable only during the current document session. After reopening a saved PDF, inserted text is ordinary page content.
- Password entry, forms, OCR, redaction, signatures, printing, search UI, tabs, cloud collaboration, and autosave recovery are not implemented.
- Creating a derived output invalidates any signature validity from the source; the app warns rather than preserving signatures.
- Guaranteed text scope is left-to-right Unicode covering common accented Latin, Greek, and Cyrillic. Full CJK typography, right-to-left text, bidirectional layout, and vertical text are deferred.
- Mouse and keyboard are primary. Touch is for ordinary scrolling; pen-specific editing is not implemented.
- PDF/A preservation is not guaranteed.
- The application currently packages native assets and fonts; a copied executable by itself is not a supported deployment method.

## Troubleshooting guide

### NuGet restore or package version failure

- Open the solution once in an up-to-date Visual Studio 2022 installation and explicitly restore NuGet packages.
- Confirm the machine can reach NuGet.
- Do not immediately change version pins. First determine whether the failure is feed access, SDK installation, package restore, or an actual incompatibility.

### MuPDF headers or libraries are missing

- Rerun `bootstrap-mupdf.ps1` for the same configuration and x64 platform being built.
- Confirm the checked-out MuPDF commit matches `PINNED_VERSION.txt`.
- Confirm submodules are initialized.
- Inspect `PdfEditor.Core.vcxproj` and `Directory.Build.props` for the expected include and library directories before editing either one.
- Do not point the project at a random system MuPDF version; API differences can be subtle.

### Linker errors involving MuPDF

- Check Debug versus Release library-directory mismatches.
- Check x64 versus Win32 architecture mismatches.
- Ensure all MuPDF third-party libraries produced by the bootstrap are present.
- Compare the unresolved symbol’s subsystem with the disabled MuPDF features before adding unrelated libraries.

### WinUI generated-header or XAML errors

- Clean only generated build outputs, then restore and rebuild.
- Verify `Microsoft.Windows.CppWinRT` and Windows App SDK packages restored.
- Confirm `Project.idl`, XAML class names, namespaces, and C++ partial classes still agree.
- Treat Visual Studio’s first error as the most useful one; later XAML errors are often cascading.

### Application builds but does not launch

- Run it through Visual Studio once so package deployment diagnostics are visible.
- Check Developer Mode/package registration.
- Check Windows App SDK runtime availability.
- Inspect the Windows Event Viewer application log and Visual Studio debug output for a missing native DLL or activation failure.
- Verify the package architecture is x64.

### Text preview differs from saved output

- The committed preview, validation, and save paths should all use MuPDF Story.
- Check that the active WinUI editing overlay is being committed before comparing.
- Check font-family URI resolution and that the same bundled font file is supplied to MuPDF.
- Add or extend an integration render-snapshot test before changing layout constants.

### Repeated saves duplicate text

This should not happen. Saving must reopen the immutable source PDF and apply the current staged models. Do not change saving to use the previously generated output as its next input.

## Guidance for a future coding chat

Start by reading, in this order:

1. `PROJECT_HANDOFF.md`
2. `README.md`
3. `scripts/bootstrap-mupdf.ps1`
4. `scripts/build-windows.ps1`
5. `Directory.Build.props`
6. `PdfEditor.sln`
7. `src/PdfEditor.Core/src/MuPdfBackend.cpp`
8. `src/PdfEditor.App/MainWindow.xaml`
9. `src/PdfEditor.App/MainWindow.xaml.cpp`
10. `src/PdfEditor.Tests/MuPdfIntegrationTests.cpp`

Then:

- Inspect the exact first Windows compiler error before making broad project changes.
- Preserve the source-PDF baseline/save regeneration design.
- Preserve the one-worker MuPDF serialization rule.
- Keep textbox model coordinates in unrotated, crop-relative PDF points.
- Keep committed preview and exported text on the same Story layout path.
- Do not discard or replace bundled fonts without updating font lookup, licenses, tests, and package contents together.
- Do not weaken atomic saving or allow output to overwrite the source.
- Keep unrelated user edits intact.
- Run the portable tests and MuPDF integration tests after core/backend changes.
- Run the full Windows UI acceptance pass after app/XAML changes.

A useful first prompt for the next chat is:

> Continue the Windows PDF editor from `PROJECT_HANDOFF.md`. Work on a Windows 11 x64 machine. First run the pinned MuPDF bootstrap and Debug x64 build without changing dependency versions. Diagnose the first real compiler or deployment error, make the smallest appropriate fix, rerun tests, and continue until `PdfEditor.App` launches. Then perform the acceptance checklist and produce a signed or clearly identified unsigned Release MSIX artifact.

## Definition of “ready as an .exe”

For this project, completion should mean all of the following—not merely that an `.exe` file exists:

- `PdfEditor.App.exe` builds in Release x64.
- Its required native DLLs, WinUI resources, fonts, and package identity are present.
- The packaged app installs and launches on a clean Windows 11 x64 environment.
- The complete textbox workflow works without a debugger attached.
- The automated tests pass on Windows.
- A Release MSIX is generated.
- The package is either signed and installable, or explicitly labeled as an unsigned development artifact with installation instructions.
- MuPDF licensing is resolved for the intended distribution model.

Until the Windows build and acceptance pass are completed, describe the project as **implemented and backend-tested, but not yet Windows-build-verified**.

## Portable friend build

The intended friend-facing artifact is now a folder-based executable inside
`PDF-Textbox-Editor-0.1.0-win-x64-portable.zip`, not a standalone zero-file-
dependency EXE. The friend extracts the folder and launches
`PdfEditor.App.exe`; all adjacent DLLs, resources, fonts, and license files must
remain in place.

From a clean Git checkout on Windows 11 x64:

```powershell
.\scripts\build-portable.ps1
```

Before distributing the final release, commit the exact source, create and push
tag `v0.1.0`, then rerun with `-CreateSourceArchive`. Publish the binary ZIP,
its `.sha256` file, and the source ZIP together. The current release is unsigned
and can trigger SmartScreen; avoiding that requires a trusted code-signing
certificate.
