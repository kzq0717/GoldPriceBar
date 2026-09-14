#pragma once
#define GOLDSDK_VERSION_MAJOR 1
#define GOLDSDK_VERSION_MINOR 0
#define GOLDSDK_VERSION_PATCH 0
#define GOLDSDK_VERSION_STRING "1.0.0"

namespace goldsdk {
inline const char* version() { return GOLDSDK_VERSION_STRING; }
}
