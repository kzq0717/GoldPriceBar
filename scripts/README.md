# 历史数据导入

## 数据库路径（自动）

脚本**优先读客户端配置**，无需手写 `--db`：

1. `%APPDATA%\GoldPriceBarLite\GoldPriceBarLite.ini` 中的 `databaseDir=`
2. 否则默认：`%APPDATA%\GoldPriceBarLite\GoldPriceBarLite\gold_extremes.db`

你在设置里填的「数据库目录」（例如 `D:\Company\SK`）会写入上述 ini，脚本会导入到：

`D:\Company\SK\gold_extremes.db`

## 用法

```bat
python scripts\import_historical_gold.py --min-year 1990 --source gj
python scripts\import_historical_gold.py --to-cny-g --fx 7.25 --source zs
```

仅在特殊情况下才需要：

```bat
python scripts\import_historical_gold.py --db "D:\Company\SK\gold_extremes.db"
```
