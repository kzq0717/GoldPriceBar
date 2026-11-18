# GoldSdk — no Qt dependency (C++17 + platform HTTP)
add_library(goldsdk STATIC
    src/json_util.cpp
    src/http_client.cpp
    src/price_client.cpp
    src/forecast.cpp
    src/trading_session.cpp
    src/config.cpp
)
add_library(GoldSdk::goldsdk ALIAS goldsdk)

# Parent project may enable GLOBAL AUTOMOC; keep this target free of Qt.
set_target_properties(goldsdk PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
    SKIP_AUTOGEN ON
)

target_include_directories(goldsdk
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(goldsdk PUBLIC cxx_std_17)

if(WIN32)
    target_link_libraries(goldsdk PUBLIC winhttp)
endif()

if(MSVC)
    # Required on Chinese Windows (code page 936): UTF-8 source without BOM
    target_compile_options(goldsdk PRIVATE /W3 /EHsc /utf-8 /FS)
else()
    target_compile_options(goldsdk PRIVATE -Wall -Wextra)
endif()

option(GOLDSDK_BUILD_TESTS "Build goldsdk C++ unit tests" OFF)
if(GOLDSDK_BUILD_TESTS)
    enable_testing()
    add_executable(goldsdk_test_forecast tests/test_forecast.cpp)
    target_link_libraries(goldsdk_test_forecast PRIVATE goldsdk)
    target_compile_features(goldsdk_test_forecast PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(goldsdk_test_forecast PRIVATE /W3 /EHsc /utf-8)
    else()
        target_compile_options(goldsdk_test_forecast PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME goldsdk_test_forecast COMMAND goldsdk_test_forecast)
endif()
