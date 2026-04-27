cmake_minimum_required(VERSION 3.21)
project(ai-reader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Qml
    Quick
    QuickControls2
    Pdf
    PdfQuick
    Widgets       # MicroTeX's Qt backend pulls in QtWidgets/QtPrintSupport.
    PrintSupport
)

find_package(Qt6Keychain REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CMARK_GFM REQUIRED IMPORTED_TARGET libcmark-gfm)

# ── MicroTeX (LaTeX/math rendering) ────────────────────────────────────
# Pulled via FetchContent so the build is reproducible and we don't
# need a packaged libmicrotex on the host. The upstream CMake exposes
# the static target as `LaTeX`, the Qt backend gates on -DQT=ON, and a
# `res/` directory must be reachable at runtime — its absolute source
# path is baked in as AIREADER_MICROTEX_RES_DIR for now (M5 packaging
# will switch to a relocatable copy beside the binary).
include(FetchContent)
set(QT             ON  CACHE BOOL "" FORCE)
set(HAVE_LOG       OFF CACHE BOOL "" FORCE)
set(GRAPHICS_DEBUG OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLE  OFF CACHE BOOL "" FORCE)
set(MEM_CHECK      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    microtex
    GIT_REPOSITORY https://github.com/NanoMichael/MicroTeX.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(microtex)

qt_standard_project_setup()

qt_add_executable(ai-reader
    src/main.cpp
    src/Block.h
    src/BlockClusterer.h
    src/BlockClusterer.cpp
    src/BlockListModel.h
    src/BlockListModel.cpp
    src/PaperController.h
    src/PaperController.cpp
    src/LlmClient.h
    src/LlmClient.cpp
    src/AnthropicClient.h
    src/AnthropicClient.cpp
    src/OpenAiClient.h
    src/OpenAiClient.cpp
    src/Settings.h
    src/Settings.cpp
    src/TranslationService.h
    src/TranslationService.cpp
    src/TranslationCache.h
    src/TranslationCache.cpp
    src/SummaryService.h
    src/SummaryService.cpp
    src/SummaryCache.h
    src/SummaryCache.cpp
    src/Section.h
    src/TocModel.h
    src/TocModel.cpp
    src/TocService.h
    src/TocService.cpp
    src/TocCache.h
    src/TocCache.cpp
    src/VisionService.h
    src/VisionService.cpp
    src/ChatModel.h
    src/ChatModel.cpp
    src/ChatService.h
    src/ChatService.cpp
    src/ChatHistoryCache.h
    src/ChatHistoryCache.cpp
    src/MarkdownRenderer.h
    src/MarkdownRenderer.cpp
    src/LatexRenderer.h
    src/LatexRenderer.cpp
)

target_compile_definitions(ai-reader PRIVATE
    AIREADER_MICROTEX_RES_DIR="${microtex_SOURCE_DIR}/res"
)

qt_add_qml_module(ai-reader
    URI AiReader
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/PasswordDialog.qml
        qml/BlockList.qml
        qml/SettingsDialog.qml
        qml/SummaryDialog.qml
        qml/TocSidebar.qml
        qml/VisionDialog.qml
        qml/PromptsDialog.qml
        qml/ChatPane.qml
)

qt_add_resources(ai-reader "icons"
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES
        resources/icons/app-16.png
        resources/icons/app-32.png
        resources/icons/app-48.png
        resources/icons/app-64.png
        resources/icons/app-128.png
        resources/icons/app-256.png
)

# Windows: embed multi-resolution .ico into the .exe and ship as a GUI
# subsystem binary (no console window).
if(WIN32)
    target_sources(ai-reader PRIVATE resources/icons/app.rc)
    set_target_properties(ai-reader PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(ai-reader PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Pdf
    Qt6::PdfQuick
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6Keychain::Qt6Keychain
    PkgConfig::CMARK_GFM
    LaTeX                # MicroTeX static lib (provided by FetchContent)
)
