# 历史数据导入

## 库路径（易错点）

客户端 SQLite 路径（Windows）：

```text
%APPDATA%\GoldPriceBarLite\GoldPriceBarLite\gold_extremes.db
```

旧版脚本曾写到少一层目录的路径，会出现「脚本显示写入成功、软件里查不到」。

若设置里自定义了数据库目录，请：

```bat
python scripts\import_historical_gold.py --db "D:\your\path\gold_extremes.db"
```

## 单位

| unit | 含义 | source |
|------|------|--------|
| USD/oz | 美元/盎司 | gj |
| CNY/g | 元/克 | zs / ms |

```bat
python scripts\import_historical_gold.py --min-year 1990 --source gj
python scripts\import_historical_gold.py --min-year 1990 --to-cny-g --fx 7.25 --source zs
```
