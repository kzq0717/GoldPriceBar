# 架构说明（v1.0）

## 分层

```
┌─────────────────────────────────────────────────────┐
│  UI 层 (Qt 6)                                        │
│  PriceBarWindow / ChartWindow / SettingsDialog       │
│  PriceService（QTimer + QNetworkAccessManager）       │
│  ExtremeDatabase（Qt Sql）/ AppSettings（QSettings）   │
└──────────────────────────┬──────────────────────────┘
                           │ 调用
┌──────────────────────────▼──────────────────────────┐
│  GoldSdk（C++17，禁止依赖 Qt）                         │
│  HttpClient · PriceClient · ForecastEngine           │
│  TradingSession · Config                             │
│  Windows: WinHTTP；其它平台: curl 命令兜底             │
└─────────────────────────────────────────────────────┘
```

## 原则

1. **SDK 不链接 Qt**，便于 CLI、服务端或其它 UI 复用。  
2. UI 负责展示、托盘、图表、线程与信号槽。  
3. 行情解析、本地预测、交易时段、INI 解析优先放在 SDK。  
4. 用户可见中文文案主要在 UI；SDK 状态码用英文，由 UI 映射。

## 模块

| SDK 模块 | 路径 | 职责 |
|----------|------|------|
| types | `sdk/include/goldsdk/types.hpp` | 报价、日线、分时点 |
| HttpClient | `http_client.*` | 同步 GET |
| PriceClient | `price_client.*` | 主/备 URL 与 JSON 解析 |
| ForecastEngine | `forecast.*` | 当日高低本地预测 |
| TradingSession | `trading_session.*` | 是否可交易 / 状态码 |
| Config | `config.*` | INI 读写、`databaseDir` |

## UI 适配示例

- `ChartWindow::computeDayRangeForecast` → `goldsdk::ForecastEngine::dayRange`  
- `TradingSession`（Qt）→ `goldsdk::TradingSession` + 中文映射  

## 后续可迁入 SDK

- SQLite 访问（现用 Qt Sql）  
- 完整备份轮询状态机  
- 统一日志接口  

## 构建关系

根 `CMakeLists.txt`：

```cmake
add_subdirectory(sdk)          # 静态库 goldsdk
qt_add_executable(...)         # 链接 goldsdk + Qt6::*
```

MSVC 下 SDK 与 UI 均使用 `/utf-8`，避免代码页 936 下中文源文件编译失败。
