# GoldPriceBar 架构（v1.0）

```
┌─────────────────────────────────────────┐
│  UI (Qt 6)                              │
│  PriceBarWindow / ChartWindow / Settings│
│  PriceService（定时器 + 信号槽适配）      │
└──────────────────┬──────────────────────┘
                   │ 调用
┌──────────────────▼──────────────────────┐
│  GoldSdk（无 Qt）                        │
│  - HttpClient / PriceClient             │
│  - ForecastEngine                       │
│  - TradingSession                       │
│  - Config（INI）                         │
│  标准库 C++17 + WinHTTP（Windows）       │
└─────────────────────────────────────────┘
```

## 原则

1. **SDK 禁止依赖 Qt**，便于 CLI、服务、其它 UI 复用。
2. UI 只负责展示、线程调度、系统托盘与图表。
3. 行情解析、本地预测、交易时段、配置解析优先放在 SDK。

## 目录

- `sdk/include/goldsdk/` 公共头文件
- `sdk/src/` 实现
- `src/` Qt 应用

## 版本

主版本 **1.x**：引入 SDK 边界；后续可将 SQLite 与更多解析迁入 SDK。
