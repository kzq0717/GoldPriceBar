cmake_minimum_required(VERSION 3.21)

project(GoldPriceBarLite
    VERSION 0.4.0
    DESCRIPTION "Lightweight Windows gold savings price bar with intraday chart"
    LANGUAGES CXX
)

# ============================================================
# Qt6 路径预留配置
# 使用方式（任选其一）：
#   1. 命令行: cmake -DQT6_ROOT_DIR="D:/InstallDir/Qt6.7/6.7.3/msvc2022_64" ..
#   2. 环境变量: 设置 CMAKE_PREFIX_PATH 指向 Qt6 安装目录
#   3. 修改下方默认值
# ============================================================
# 注意：CACHE 的第 3 个参数是说明文字，默认值是第 2 个参数
set(QT6_ROOT_DIR "D:/InstallDir/Qt6.7/6.7.3/msvc2022_64" CACHE PATH "Path to Qt 6.7 installation (e.g. D:/InstallDir/Qt6.7/6.7.3/msvc2022_64)")

if(QT6_ROOT_DIR)
    list(PREPEND CMAKE_PREFIX_PATH "${QT6_ROOT_DIR}")
    message(STATUS "Using Qt6 from QT6_ROOT_DIR: ${QT6_ROOT_DIR}")
endif()

# 也可通过 -DCMAKE_PREFIX_PATH=... 直接指定
# ============================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 查找 Qt6 组件（开发环境可为 6.4+，正式建议 6.7）
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Charts
    Sql
)

qt_standard_project_setup()

set(PROJECT_SOURCES
    src/main.cpp
    src/PriceBarWindow.h
    src/PriceBarWindow.cpp
    src/PriceService.h
    src/PriceService.cpp
    src/SettingsDialog.h
    src/SettingsDialog.cpp
    src/ChartWindow.h
    src/ChartWindow.cpp
    src/HistoryCache.h
    src/HistoryCache.cpp
    src/AppSettings.h
    src/AppSettings.cpp
    src/ExtremeDatabase.h
    src/ExtremeDatabase.cpp
    src/Logger.h
    src/Logger.cpp
    src/CrashHandler.h
    src/CrashHandler.cpp
    src/ForecastTracker.h
    src/ForecastTracker.cpp
    src/SingleInstance.h
    src/SingleInstance.cpp
)

qt_add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Charts
    Qt6::Sql
)

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE dbghelp)
endif()

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Qt6 version: ${Qt6_VERSION}")
message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
