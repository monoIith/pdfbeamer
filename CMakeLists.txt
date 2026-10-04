cmake_minimum_required(VERSION 3.24)
project(PdfEditorPortableCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(PdfEditorCore
    src/PdfEditor.Core/src/DocumentSession.cpp
    src/PdfEditor.Core/src/Geometry.cpp
    src/PdfEditor.Core/src/PdfBackend.cpp
    src/PdfEditor.Core/src/PdfWorker.cpp
    src/PdfEditor.Core/src/TextMarkup.cpp)
target_include_directories(PdfEditorCore PUBLIC src/PdfEditor.Core/include)

add_executable(PdfEditorCoreTests
    src/PdfEditor.Tests/TestMain.cpp
    src/PdfEditor.Tests/GeometryTests.cpp
    src/PdfEditor.Tests/PdfWorkerTests.cpp
    src/PdfEditor.Tests/DocumentSessionTests.cpp
    src/PdfEditor.Tests/TextMarkupTests.cpp)
target_include_directories(PdfEditorCoreTests PRIVATE src/PdfEditor.Tests)
target_link_libraries(PdfEditorCoreTests PRIVATE PdfEditorCore)

enable_testing()
add_test(NAME PdfEditorCoreTests COMMAND PdfEditorCoreTests)
