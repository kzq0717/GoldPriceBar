# GoldPriceBarLite

Windows 浮窗黄金价格监控（C++ / **Qt 6** UI + **GoldSdk** 无 Qt 核心库）

仓库：https://github.com/kzq0717/GoldPriceBar

当前版本：**1.0.x**（UI / SDK 分离）

---

## 功能概览

| 类别 | 内容 |
|------|------|
| 行情 | 浙商积存金 / 民生积存金 / 伦敦金（XAU/USD），主备数据源 |
| 浮窗 | 置顶、透明度、拖拽、托盘、对照价、持仓盈亏 |
| 分时 | 今日分时、月份曲线、均线（MA5/10/20 工具栏勾选） |
| 预测 | 本地当日高低推演；可选 xAI Grok / Gemini |
| 预警 | 高低价阈值、智能均线/分位、溢价、宏观日程 |
| 数据 | SQLite（`daily_bars` 含单位、抽样、预测日志等） |
| 其它 | 深色主题、检查更新、代理、开机自启、全局热键 |

---

## 架构

```
src/     Qt6 界面（浮窗、分时、设置、托盘、定时网络）
sdk/     GoldSdk（C++17，无 Qt：HTTP / 解析 / 预测 / 时段 / INI）
scripts/ 历史导入、部署辅助
```

详见：[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

---

## 环境要求

- Windows 10/11 x64  
- **Qt 6.7+**（组件：Core、Gui、Widgets、Network、**Charts**、**Sql**）  
- Visual Studio 2022 或 2026（MSVC），或 Ninja +「x64 Native Tools」  
- CMake 3.21+  

---

## 快速编译

```bat
git clone https://github.com/kzq0717/GoldPriceBar.git
cd GoldPriceBar

REM 可选：指定本机 Qt
set QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64

REM 在「x64 Native Tools Command Prompt for VS」中：
build.bat clean vs
REM 或 Ninja：
build.bat clean ninja
```

### 部署 Qt DLL（解决缺 DLL）

```bat
deploy.bat
```

或一步：

```bat
build.bat deploy
```

临时不拷贝 DLL、靠 PATH 运行：

```bat
run_with_qt.bat
```

### 常用参数

| 命令 | 说明 |
|------|------|
| `build.bat` | 配置并编译（沿用已有生成器） |
| `build.bat clean` | 删除 `build` 后重配编译 |
| `build.bat clean vs` | 强制 Visual Studio 生成器 |
| `build.bat clean ninja` | 强制 Ninja |
| `build.bat deploy` | 编译后执行 `deploy.bat` |
| `build.bat start` | 编译后启动程序 |

更多构建说明：[docs/BUILD.md](docs/BUILD.md)

---

## 历史数据导入

脚本会从客户端配置读取 `databaseDir`（无需手写路径）：

```bat
python scripts\import_historical_gold.py --min-year 1990 --source gj
python scripts\import_historical_gold.py --to-cny-g --fx 7.25 --source zs
```

说明：[scripts/README.md](scripts/README.md)

---

## 配置文件

`%APPDATA%\GoldPriceBarLite\GoldPriceBarLite.ini`

| 键 | 说明 |
|----|------|
| `databaseDir` | 数据库目录；空则用默认 AppData |
| `refreshIntervalMs` | 刷新间隔（毫秒） |
| `dataSource` | `zs` / `ms` / `gj` |
| `forecastOnline` | 是否大模型预测 |
| `xaiApiKey` / `llmProvider` | 大模型 Key 与提供方 |
| `darkTheme` | 深色主题 |

默认数据库：

`%APPDATA%\GoldPriceBarLite\GoldPriceBarLite\gold_extremes.db`

若设置了 `databaseDir=D:\Company\SK`，则为 `D:\Company\SK\gold_extremes.db`。

---

## GitHub Actions

推送 `main` 或 tag `v*` 可构建产物。说明：[docs/GITHUB_ACTIONS.md](docs/GITHUB_ACTIONS.md)

```bat
git tag v1.0.1
git push origin v1.0.1
```

---

## 目录结构

```
GoldPriceBar/
  CMakeLists.txt
  build.bat / deploy.bat / run_with_qt.bat
  sdk/                 # GoldSdk（无 Qt）
  src/                 # Qt 应用
  scripts/             # 导入与辅助脚本
  docs/                # 文档
  resources/           # 图标与资源
  .github/workflows/   # CI
```

---

## 版本

| 版本 | 说明 |
|------|------|
| **1.0.x** | UI / SDK 分离；GoldSdk；部署脚本完善 |
| 0.8.x | 均线、建模表、导入脚本、仪表盘 UI |
| 0.7.x | 预测、Gemini、设置分类前的迭代 |

---

## 许可与声明

个人/学习用途。行情数据来自公开接口，仅供参考，不构成投资建议。交易时段为示意规则。
