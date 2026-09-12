# 历史数据导入

## 单位约定

| unit | 含义 | 典型 source |
|------|------|-------------|
| `USD/oz` | 美元/金衡盎司 | `gj`（伦敦金 / FreeGoldAPI） |
| `CNY/g` | 人民币元/克 | `zs` / `ms`（积存金） |

换算：`CNY/g ≈ USD/oz × 美元兑人民币 / 31.1034768`

## 用法

```bash
# 1) 原样写入伦敦金（USD/oz）
python scripts/import_historical_gold.py --min-year 1990 --source gj

# 2) 换算为元/克，写入 zs，便于与浙商分时同尺度做 MA
python scripts/import_historical_gold.py --min-year 1990 --to-cny-g --fx 7.25 --source zs
```

`--fx` 请按近期中间价自行调整。

## Stooq

2026 起常需 apikey：`--prefer both --stooq-apikey KEY`
