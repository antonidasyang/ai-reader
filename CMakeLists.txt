cmake_minimum_required(VERSION 3.21)
project(ai-reader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(_aireader_qt_components
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
    LinguistTools # qt_add_translations / lupdate / lrelease for i18n.
)
if(UNIX AND NOT APPLE)
    # qtkeychain's Linux backend (libsecret IPC) uses QtDBus; on Windows
    # it uses Credential Manager and on macOS Security.framework, neither
    # of which need DBus.
    list(APPEND _aireader_qt_components DBus)
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS ${_aireader_qt_components})

include(FetchContent)

# ── cmark-gfm (CommonMark + GFM extensions) ────────────────────────────
# FetchContent for the same uniformity reasons as qtkeychain: Windows
# typically has no pkg-config, and bundling the static libs avoids
# shipping cmark-gfm DLLs alongside the binary. Tests + shared build
# are turned off so we only get the two static targets we link below.
set(CMARK_TESTS  OFF CACHE BOOL "" FORCE)
set(CMARK_SHARED OFF CACHE BOOL "" FORCE)
set(CMARK_STATIC ON  CACHE BOOL "" FORCE)
FetchContent_Declare(
    cmark
    GIT_REPOSITORY https://github.com/github/cmark-gfm.git
    GIT_TAG        0.29.0.gfm.13
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cmark)

# ── qtkeychain (secure credential storage) ─────────────────────────────
# FetchContent so we don't require a system install — Linux distros
# usually ship it via libqt6keychain-dev but Windows/macOS rarely do.
# Built static so no extra DLL has to ship with the executable.
set(BUILD_WITH_QT6         ON  CACHE BOOL "" FORCE)
set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
set(BUILD_TRANSLATIONS     OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    qtkeychain
    GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
    GIT_TAG        0.14.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(qtkeychain)

# ── tinyxml2 ───────────────────────────────────────────────────────────
# MicroTeX's only external dep. Upstream CMake calls find_package on it,
# which on Windows MSVC means vcpkg and on Linux means pkg-config — both
# absent on a vanilla install. FetchContent makes it portable.
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG        10.0.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tinyxml2)

# ── MicroTeX (LaTeX/math rendering) ────────────────────────────────────
# Pulled via FetchContent and patched in-place to skip its
# find_package(tinyxml2)/PkgConfig lookups — our FetchContent above
# already provides the `tinyxml2` (and `tinyxml2::tinyxml2`) targets
# that LaTeX target links against. Resources live under res/ and are
# reached at runtime via AIREADER_MICROTEX_RES_DIR; M5 packaging will
# replace the absolute source path with a relocatable copy beside the
# binary.
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
FetchContent_GetProperties(microtex)
if(NOT microtex_POPULATED)
    # CMP0169 (CMake 3.30+) deprecates the single-arg FetchContent_Populate.
    # We need the populate-then-patch flow until upstream stops calling
    # find_package on tinyxml2.
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(microtex)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()

    file(READ "${microtex_SOURCE_DIR}/CMakeLists.txt" _mt_cmake)
    string(REPLACE
        "find_package(tinyxml2 CONFIG REQUIRED)"
        "# find_package(tinyxml2 CONFIG REQUIRED)  # ai-reader: from FetchContent"
        _mt_cmake "${_mt_cmake}")
    string(REPLACE
        "find_package(PkgConfig REQUIRED)"
        "# find_package(PkgConfig REQUIRED)  # ai-reader: tinyxml2 from FetchContent"
        _mt_cmake "${_mt_cmake}")
    string(REPLACE
        "pkg_check_modules(tinyxml2 REQUIRED IMPORTED_TARGET tinyxml2)"
        "# pkg_check_modules(tinyxml2 ...)  # ai-reader: from FetchContent"
        _mt_cmake "${_mt_cmake}")
    file(WRITE "${microtex_SOURCE_DIR}/CMakeLists.txt" "${_mt_cmake}")

    add_subdirectory("${microtex_SOURCE_DIR}" "${microtex_BINARY_DIR}")
endif()

# Upstream cmark-gfm and MicroTeX hard-code /WX in their CMakeLists, so any
# stray MSVC warning blows up the build: cmark's html.c is saved in a
# codepage that triggers C4819 on cp936 hosts, and MicroTeX's font_parser
# discards [[nodiscard]] returns (C4834). We don't want to fork those repos
# over cosmetics, so just turn warnings-as-errors back off for the three
# imported static libs. Our own targets stay strict.
if(MSVC)
    foreach(_aireader_relaxed_target
            libcmark-gfm_static
            libcmark-gfm-extensions_static
            LaTeX)
        if(TARGET ${_aireader_relaxed_target})
            target_compile_options(${_aireader_relaxed_target} PRIVATE /WX-)
        endif()
    endforeach()
endif()

qt_standard_project_setup()

qt_add_executable(ai-reader
    src/main.cpp
    src/Block.h
    src/BlockClusterer.h
    src/BlockClusterer.cpp
    src/BlockListModel.h
    src/BlockListModel.cpp
    src/BlockCache.h
    src/BlockCache.cpp
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
    src/ChatSession.h
    src/ChatSessionListModel.h
    src/ChatSessionListModel.cpp
    src/ChatContent.h
    src/ChatContent.cpp
    src/CodeHighlighter.h
    src/CodeHighlighter.cpp
    src/MarkdownRenderer.h
    src/MarkdownRenderer.cpp
    src/LatexRenderer.h
    src/LatexRenderer.cpp
    src/Library.h
    src/Library.cpp
    src/LayoutSettings.h
    src/LayoutSettings.cpp
    src/Tabs.h
    src/Tabs.cpp
)

target_compile_definitions(ai-reader PRIVATE
    AIREADER_MICROTEX_RES_DIR="${microtex_SOURCE_DIR}/res"
)

# Neither qtkeychain nor cmark-gfm publishes its include dirs to
# in-tree consumers (qtkeychain only sets INSTALL_INTERFACE; cmark-gfm
# uses bare include_directories() that don't propagate as target
# properties). Wire them up explicitly. Binary dirs are needed for the
# generated qkeychain_export.h / cmark-gfm_export.h / cmark-gfm_version.h.
target_include_directories(ai-reader PRIVATE
    ${qtkeychain_SOURCE_DIR}
    ${qtkeychain_BINARY_DIR}
    ${cmark_SOURCE_DIR}/src
    ${cmark_SOURCE_DIR}/extensions
    ${cmark_BINARY_DIR}/src
    ${cmark_BINARY_DIR}/extensions
)

qt_add_qml_module(ai-reader
    URI AiReader
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/PasswordDialog.qml
        qml/BlockList.qml
        qml/SettingsDialog.qml
        qml/SummaryPane.qml
        qml/TocSidebar.qml
        qml/VisionDialog.qml
        qml/PromptsDialog.qml
        qml/ChatPane.qml
        qml/FolderPane.qml
        qml/DockGrip.qml
        qml/MathBlock.qml
        qml/CodeBlock.qml
        qml/WelcomeWizard.qml
)

# Compile .ts → .qm and embed under :/i18n so QTranslator::load() in
# main.cpp can find them with no on-disk translations dir. The basename
# convention (ai-reader_<locale>.qm) is what main.cpp's load("ai-reader",
# "_", ":/i18n") expects; keep new languages on the same pattern.
qt_add_translations(ai-reader
    TS_FILES i18n/ai-reader_zh_CN.ts
    RESOURCE_PREFIX "/i18n"
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
    qt6keychain                          # qtkeychain static    (FetchContent)
    libcmark-gfm_static                  # cmark-gfm core       (FetchContent)
    libcmark-gfm-extensions_static       # cmark-gfm GFM exts   (FetchContent)
    LaTeX                                # MicroTeX static      (FetchContent)
)
if(UNIX AND NOT APPLE)
    target_link_libraries(ai-reader PRIVATE Qt6::DBus)
endif()

# ── Windows packaging: stage build output into dist\ ───────────────
# After every Release build, drop ai-reader.exe into <repo>/dist so
# windeploy.bat (Qt runtime) and AiReader.iss (Inno Setup) can pull
# from a stable, predictable path. Using copy_if_different keeps
# rebuilds quick when the binary hasn't changed.
if(WIN32)
    set(_aireader_dist_dir "${CMAKE_SOURCE_DIR}/dist")
    add_custom_command(TARGET ai-reader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_aireader_dist_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:ai-reader>"
                "${_aireader_dist_dir}/"
        COMMENT "Staging $<TARGET_FILE_NAME:ai-reader> into ${_aireader_dist_dir}"
        VERBATIM
    )
endif()
