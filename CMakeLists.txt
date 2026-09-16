cmake_minimum_required(VERSION 3.21)
project(GoldPriceBarLite VERSION 1.0.0 LANGUAGES CXX)

# ============================================================
# Qt6 路径预留
# ============================================================
set(QT6_ROOT_DIR "D:/InstallDir/Qt6.7/6.7.3/msvc2022_64" CACHE PATH "Path to Qt 6.7 installation")
if(QT6_ROOT_DIR)
    list(PREPEND CMAKE_PREFIX_PATH "${QT6_ROOT_DIR}")
    message(STATUS "Using Qt6 from QT6_ROOT_DIR: ${QT6_ROOT_DIR}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---------- SDK（无 Qt）----------
add_subdirectory(sdk)

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets Network Charts Sql
)
qt_standard_project_setup()

set(PROJECT_SOURCES
    src/main.cpp
    src/PriceBarWindow.h src/PriceBarWindow.cpp
    src/PriceService.h src/PriceService.cpp
    src/SettingsDialog.h src/SettingsDialog.cpp
    src/ChartWindow.h src/ChartWindow.cpp
    src/HistoryCache.h src/HistoryCache.cpp
    src/AppSettings.h src/AppSettings.cpp
    src/ExtremeDatabase.h src/ExtremeDatabase.cpp
    src/Logger.h src/Logger.cpp
    src/CrashHandler.h src/CrashHandler.cpp
    src/ForecastTracker.h src/ForecastTracker.cpp
    src/SingleInstance.h src/SingleInstance.cpp
    src/UpdateChecker.h src/UpdateChecker.cpp
    src/GlobalHotkey.h src/GlobalHotkey.cpp
    src/EventCalendar.h src/EventCalendar.cpp
    src/TradingSession.h src/TradingSession.cpp
    src/SentimentService.h src/SentimentService.cpp
    src/SentimentDialog.h src/SentimentDialog.cpp
)

qt_add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCES}
    resources/resources.qrc
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    goldsdk
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Charts Qt6::Sql
)

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(${PROJECT_NAME} PRIVATE dbghelp)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()


include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION} (UI+GoldSdk)")
message(STATUS "Qt6 version: ${Qt6_VERSION}")
