# GoldSdk — 无 Qt，仅 C++17 标准库 + 平台 HTTP
add_library(goldsdk STATIC
    src/json_util.cpp
    src/http_client.cpp
    src/price_client.cpp
    src/forecast.cpp
    src/trading_session.cpp
    src/config.cpp
)
add_library(GoldSdk::goldsdk ALIAS goldsdk)

target_include_directories(goldsdk
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(goldsdk PUBLIC cxx_std_17)

if(WIN32)
    target_link_libraries(goldsdk PUBLIC winhttp)
endif()

# 可选：禁止异常外的警告
if(MSVC)
    target_compile_options(goldsdk PRIVATE /W3 /EHsc)
else()
    target_compile_options(goldsdk PRIVATE -Wall -Wextra)
endif()
