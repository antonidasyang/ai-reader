cmake_minimum_required(VERSION 3.21)
project(ai-reader VERSION 0.2.0 LANGUAGES CXX)

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

# ── Optional: Sentry-Native (crash reporting) ──────────────────────
# Pulled via FetchContent only when the user opts in via
# -DAIREADER_ENABLE_SENTRY=ON. The "inproc" backend captures crashes
# via in-process signal / SetUnhandledExceptionFilter handlers, so we
# don't have to ship an external crashpad_handler.exe alongside the
# binary; it's slightly less reliable than crashpad for hard SEH
# crashes but materially simpler to package.
if(AIREADER_ENABLE_SENTRY)
    set(SENTRY_BACKEND          "inproc" CACHE STRING "" FORCE)
    set(SENTRY_BUILD_TESTS      OFF      CACHE BOOL   "" FORCE)
    set(SENTRY_BUILD_EXAMPLES   OFF      CACHE BOOL   "" FORCE)
    set(SENTRY_BUILD_SHARED_LIBS OFF     CACHE BOOL   "" FORCE)
    if(WIN32)
        set(SENTRY_TRANSPORT "winhttp" CACHE STRING "" FORCE)
    elseif(APPLE)
        set(SENTRY_TRANSPORT "curl"    CACHE STRING "" FORCE)
    else()
        set(SENTRY_TRANSPORT "curl"    CACHE STRING "" FORCE)
    endif()
    FetchContent_Declare(
        sentry
        GIT_REPOSITORY https://github.com/getsentry/sentry-native.git
        GIT_TAG        0.7.20
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(sentry)
endif()

# Vendored libs ship MSVC builds that promote stray warnings to
# errors via /WX in their *own* CMakeLists — appended to CMAKE_C_FLAGS
# inside their subdirectory, which is parent-scope mutation we can't
# reach from out here. Two warnings actually trigger:
#
#   C4819 in cmark/src/html.c  — file contains multi-byte chars saved
#       in UTF-8 that MSVC can't read on a cp936 (Chinese Windows)
#       host without /utf-8. Cosmetic on every other host.
#   C4834 in microtex/font_parser.cpp — discards a [[nodiscard]]
#       return from fread. Real bug but upstream's, not ours.
#
# Suppressing the specific warning IDs disarms /WX cleanly: cl never
# emits the warning so /WX has nothing to promote to error. This
# avoids D9025 ("overriding '/WX' with '/WX-'"), which the older
# /WX- override approach generated on every translation unit.
if(MSVC)
    foreach(_aireader_cmark_target
            libcmark-gfm_static
            libcmark-gfm-extensions_static)
        if(TARGET ${_aireader_cmark_target})
            target_compile_options(${_aireader_cmark_target} PRIVATE /wd4819)
        endif()
    endforeach()
    if(TARGET LaTeX)
        target_compile_options(LaTeX PRIVATE /wd4834 /wd4819)
    endif()
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
    src/UpdateChecker.h
    src/UpdateChecker.cpp
    src/CrashReporter.h
    src/CrashReporter.cpp
)

target_compile_definitions(ai-reader PRIVATE
    AIREADER_MICROTEX_RES_DIR="${microtex_SOURCE_DIR}/res"
    AIREADER_VERSION="${PROJECT_VERSION}"
)

if(AIREADER_ENABLE_SENTRY)
    if(NOT AIREADER_SENTRY_DSN)
        message(WARNING
            "AIREADER_ENABLE_SENTRY=ON but AIREADER_SENTRY_DSN is empty; "
            "Sentry will be linked but no events will be sent. "
            "Pass -DAIREADER_SENTRY_DSN=https://… to fix.")
    endif()
    target_compile_definitions(ai-reader PRIVATE
        AIREADER_HAS_SENTRY=1
        AIREADER_SENTRY_DSN="${AIREADER_SENTRY_DSN}"
    )
    target_link_libraries(ai-reader PRIVATE sentry::sentry)
endif()

# Diagnostic build flag — flip to ON to use the console subsystem on
# Windows so qDebug / Qt error messages are visible in cmd. The GUI
# build always tees Qt log messages to <AppData>/launch.log via the
# message handler in main.cpp; reach for AIREADER_CONSOLE only when
# you need to catch failures that happen *before* QGuiApplication
# starts (missing DLL, missing platform plugin), which never reach
# the message handler.
option(AIREADER_CONSOLE "Build a console subsystem binary on Windows for visible stderr" OFF)

# ── Optional Sentry-Native crash reporting ─────────────────────────
# Off by default so the basic build stays dependency-light. When ON,
# FetchContent pulls sentry-native and links it into ai-reader; main
# starts the SDK only after both:
#   * settings.crashReportsOptIn() returns true (the user has ticked
#     the Settings → Updates & privacy box), AND
#   * AIREADER_SENTRY_DSN was set at configure time.
# Sentry DSNs are designed to be embedded in client builds — they're
# project identifiers, not secrets, so safe to bake into the binary.
option(AIREADER_ENABLE_SENTRY "Enable Sentry-Native crash reporting (requires AIREADER_SENTRY_DSN)" OFF)
set(AIREADER_SENTRY_DSN "" CACHE STRING "Sentry DSN URL embedded into the build (empty = no events sent)")

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
        qml/ChangelogDialog.qml
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

# CHANGELOG.md is loaded by qml/ChangelogDialog.qml via XMLHttpRequest
# from qrc:/CHANGELOG.md. Embedding it into the binary's resource
# system (rather than reading from disk) means a packaged install
# always has the right version of the changelog -- the one that
# shipped with the binary -- regardless of where it's installed.
qt_add_resources(ai-reader "changelog"
    PREFIX "/"
    FILES CHANGELOG.md
)

# Windows: embed multi-resolution .ico into the .exe and ship as a GUI
# subsystem binary (no console window). AIREADER_CONSOLE flips us to
# the console subsystem so launch failures become visible in cmd.
if(WIN32)
    target_sources(ai-reader PRIVATE resources/icons/app.rc)
    if(AIREADER_CONSOLE)
        set_target_properties(ai-reader PROPERTIES WIN32_EXECUTABLE FALSE)
    else()
        set_target_properties(ai-reader PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
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

    # MicroTeX needs its res/ tree (TeXFonts, syms, etc.) at runtime.
    # The build defaults to AIREADER_MICROTEX_RES_DIR — an absolute
    # path into the build tree — which works on the dev box but
    # vanishes on a packaged install. Mirror res/ next to ai-reader.exe
    # under a stable name so LatexRenderer can find it relative to
    # the binary; AiReader.iss bundles dist\* recursively, so the
    # installer captures it without extra config.
    add_custom_command(TARGET ai-reader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${microtex_SOURCE_DIR}/res"
                "${_aireader_dist_dir}/microtex_res"
        COMMENT "Staging MicroTeX res/ into ${_aireader_dist_dir}/microtex_res"
        VERBATIM
    )
endif()
