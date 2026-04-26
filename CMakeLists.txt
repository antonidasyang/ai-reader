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
)

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
    src/SummaryService.h
    src/SummaryService.cpp
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
)
