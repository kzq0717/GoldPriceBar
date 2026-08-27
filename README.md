# GoldPriceBarLite

轻量级 Windows 黄金积存金价格条（Qt 6 / C++）

仓库：https://github.com/kzq0717/GoldPriceBar

## 功能

- 浮窗实时金价（浙商 / 民生 / 伦敦金）
- 全日分时曲线、最高/最低、侧栏当前价与预测
- 本地短时推演 / 可选 xAI Grok 大模型预测
- SQLite 记录当日高低点（目录可配置）

## 获取与更新

```bash
git clone https://github.com/kzq0717/GoldPriceBar.git
cd GoldPriceBar
git pull
```

## Windows 本地编译

1. 安装 Qt 6.7+（Charts、Sql/SQLite）与 VS 2022  
2. 修改 `build.bat` 中 `QT6_ROOT`  
3. 执行：

```bat
build.bat
deploy.bat
```

## 自动化打包（GitHub Actions）

推送到 `main` 或打 tag `v*` 时自动：

| 产物 | 说明 |
|------|------|
| `*-src.zip` | 源码包 |
| `*-win64.zip` | Windows Release + windeployqt 依赖 |

- 推送 `main`：Actions 中可下载 Artifact  
- 推送 tag（如 `v0.2.0`）：自动创建 GitHub Release 并附带上述 zip  

```bash
git tag v0.2.0
git push origin v0.2.0
```

## 配置

`%APPDATA%\GoldPriceBarLite\GoldPriceBarLite.ini`

| 键 | 说明 |
|----|------|
| `databaseDir` | 数据库目录，空=默认 AppData |
| `forecastOnline` | 是否大模型预测 |
| `xaiApiKey` | xAI API Key |
| `refreshIntervalMs` | 刷新间隔毫秒 |

数据库文件：`gold_extremes.db`
