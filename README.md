# PDF Textbox Editor

A Windows 11 x64 C++20/WinUI 3 prototype for placing formatted textboxes on
PDF pages and saving them as permanent, searchable PDF page content.

## What is implemented

- Packaged WinUI 3/MSIX desktop shell with Open, Save, Save As, undo/redo,
  Select/Text tools, Fit Width/Fit Page and 25–400% zoom.
- Collapsible thumbnail rail and continuous-scroll multipage canvas.
- Drag-to-create multiline textboxes; select, edit, move, resize, delete, and
  arrow-key nudge them.
- Whole-box Noto Sans, Noto Serif, or Noto Sans Mono formatting: size, color,
  bold, italic, alignment, border, fill, and opacity.
- Crop-local PDF-point geometry with transforms for 0/90/180/270-degree pages.
- A single serialized MuPDF worker for PDF open, render, Story layout checks,
  and save operations.
- Red overflow indication and save blocking for non-fitting text.
- Save As from the immutable source on every save, using a temporary file,
  reopen validation, and atomic destination replacement.
- Permanent page-content output using a collision-safe Form XObject per edited
  page, embedded/subset fonts, and unchanged original page streams.
- Rejection of non-PDF, encrypted, malformed, and same-as-source destinations;
  signed-document warning before creating an edited copy.
- Session undo/redo with coalesced typing and pointer transforms, dirty prompts,
  and the keyboard shortcuts described below.

The first milestone deliberately does not edit existing PDF text or support
forms, annotations, OCR, redaction, signatures, printing, right-to-left text,
vertical text, or CJK typography.

## Windows prerequisites

- Windows 11 x64 with Developer Mode enabled.
- Visual Studio 2022 or newer with:
  - Desktop development with C++
  - Windows application development / Windows App SDK C++ tools
  - MSIX Packaging Tools
  - Windows 11 SDK 10.0.26100 (or retarget `Directory.Build.props` to an
    installed newer SDK)
- Git and Python 3 on `PATH` (MuPDF's Windows DLL build generates exports with
  Python).

The solution pins Microsoft.WindowsAppSDK 2.5.1, C++/WinRT 3.0.260818.1,
Windows SDK BuildTools 10.0.28000.2705, and MuPDF 1.28.5 commit
`8ad45e92f0935d3d87f1db3f873086472a5e1b24`.

## Build and run

Open a **Developer PowerShell for Visual Studio**, then run:

```powershell
Set-Location 'C:\path\to\Bluebeam clone'
.\scripts\bootstrap-mupdf.ps1 -Configuration Debug
.\scripts\build-windows.ps1 -Configuration Debug
```

Alternatively open `PdfEditor.sln`, choose `Debug | x64`, set
`PdfEditor.App` as the startup project, and press F5 after bootstrapping MuPDF.
NuGet restore supplies the Windows App SDK and GoogleTest packages.

To ask MSBuild for an unsigned development MSIX as well:

```powershell
.\scripts\build-windows.ps1 -Configuration Release -Package
```

Configure a trusted package-signing certificate in the app project's Packaging
properties before distributing or installing a Release MSIX outside Developer
Mode. The checked-in manifest publisher is `CN=PdfEditor Development`.

## Use

1. Choose **Open** and select an ordinary, unencrypted `.pdf`.
2. Choose **Text**, drag a rectangle on any page, and type.
3. Use the formatting bar. The top-left blue handle moves the selected box;
   the bottom-right handle resizes it.
4. Resolve any red overflow boxes, then choose **Save** or **Save As**. The
   first save always asks for a new path and will not overwrite the source.

Shortcuts: `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S`, `Ctrl+Z`, `Ctrl+Y`, `Delete`,
`Escape`, arrow-key nudge, `Shift+arrow` 10-point nudge, and `Ctrl+mouse-wheel`
zoom.

## Project layout

- `src/PdfEditor.App` — C++/WinRT and WinUI 3 view/interaction layer.
- `src/PdfEditor.Core` — models, geometry, session history, serialized worker,
  MuPDF Story layout/render/save backend.
- `src/PdfEditor.Tests` — GoogleTest unit tests and a Windows PDF round-trip
  integration test. The root CMake target provides a dependency-light portable
  core test path and a fallback test harness.
- `scripts/bootstrap-mupdf.ps1` — fetches, verifies, configures, and builds the
  exact MuPDF source release.

## Verification

On Windows, `scripts/build-windows.ps1` builds all three projects and executes
the test binary. The integration test creates a two-page PDF with a rotated,
cropped page; inserts accented Latin, Greek, and Cyrillic text; saves and
reopens it; extracts the searchable text; renders the output; checks page
geometry/count; and confirms that the source bytes did not change.

On non-Windows hosts, the portable checks can be built with CMake:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Licensing

MuPDF is AGPL-3.0-or-later/commercial dual-licensed. This prototype is suitable
for personal AGPL-compatible use. A closed-source distributed product must
obtain an Artifex commercial license or replace the backend. See
`THIRD_PARTY_NOTICES.md` and `third_party/mupdf/COPYING`. Bundled Noto fonts are
under the SIL Open Font License 1.1.
