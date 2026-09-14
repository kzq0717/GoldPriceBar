# 脚本说明

## import_historical_gold.py

导入公开日线到 `daily_bars`（带 `unit` 字段）。

**库路径优先级：**

1. `--db` 完整路径  
2. 客户端 ini 中 `databaseDir`（`%APPDATA%\GoldPriceBarLite\GoldPriceBarLite.ini`）  
3. 默认 `%APPDATA%\GoldPriceBarLite\GoldPriceBarLite\gold_extremes.db`  

```bat
python scripts\import_historical_gold.py --min-year 1990 --source gj
python scripts\import_historical_gold.py --to-cny-g --fx 7.25 --source zs
```

- `gj` + 默认：`unit=USD/oz`  
- `--to-cny-g`：换算为约元/克，`unit=CNY/g`  

Stooq 常需 apikey（2026+）：`--prefer both --stooq-apikey KEY`

## deploy_deps.bat

调用仓库根目录 `deploy.bat`，向 exe 旁部署 Qt DLL。
