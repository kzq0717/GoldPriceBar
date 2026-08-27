# GoldPriceBarLite

轻量级 Windows 黄金积存金价格条（Qt 6 / C++），支持：

- 浮窗实时金价（浙商 / 民生 / 伦敦金）
- 全日分时曲线、最高最低标注
- 本地 / Grok 大模型短时预测（可选）
- SQLite 记录当日高低点（目录可配置）

## 获取源码（推荐用 Git）

```bash
# 首次
git clone <你的仓库地址> GoldPriceBarLite
cd GoldPriceBarLite

# 之后更新
git pull
```

将本仓库推到你自己的 GitHub / Gitee 后，把上面地址换成真实 URL 即可。

## Windows 编译

1. 安装 Qt 6.7+（含 Charts、Sql/SQLite）与 VS 2022  
2. 修改 `build.bat` 中 `QT6_ROOT`  
3. 运行：

```bat
build.bat
build.bat run
deploy.bat
```

或：

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DQT6_ROOT_DIR="D:/InstallDir/Qt6.7/6.7.3/msvc2022_64"
cmake --build build --config Release
```

## 配置

用户配置（QSettings Ini）：

`%APPDATA%\GoldPriceBarLite\GoldPriceBarLite.ini`

常用项：

| 键 | 说明 |
|----|------|
| `databaseDir` | 数据库目录，空则默认 AppData |
| `forecastOnline` | 是否使用大模型预测 |
| `xaiApiKey` | xAI API Key |
| `refreshIntervalMs` | 刷新间隔 |

数据库文件：`gold_extremes.db`

## 稳定性说明

- 网络请求防堆积、超时中止  
- SQLite 写库节流（≥5 秒）  
- 分时预测侧栏不每秒重算（≥30 秒）  
- 关闭分时窗口时取消未完成请求  

若仍闪退，请记录发生前操作（刷新间隔、是否打开曲线、是否开启大模型）以便排查。
