# 历史数据导入

## 公开接口（无需 Key）

| 源 | 地址 | 说明 |
|----|------|------|
| FreeGoldAPI | https://freegoldapi.com/data/latest.json | 长历史 USD 金价，年/月/日混合粒度 |
| Stooq | https://stooq.com/q/d/l/?s=xauusd&i=d | XAUUSD 日线 CSV（若可访问） |

goldprice.dev 日线深度受套餐限制（免费约 30 天），本脚本默认用上两者。

## 用法

```bash
# 写入默认库路径（与客户端一致）
python scripts/import_historical_gold.py --min-year 1990 --source gj

# 指定库
python scripts/import_historical_gold.py --db /path/to/gold_extremes.db
```

导入写入表 `daily_bars`，`source=gj`，可供 MA5/MA20、月份曲线与后续建模使用。

## 运行时建模表（客户端自动写）

| 表 | 内容 |
|----|------|
| quote_samples | 主源降采样报价 ~30s |
| secondary_quotes | 对照价与比值 |
| forecast_logs | 预测与结算 |
| alert_events | 预警事件 |
| session_marks | 交易时段状态变化 |
| intraday_samples / daily_bars / daily_extremes | 原有分时与日线 |

下阶段再拆无 Qt 的 SDK；本阶段仅扩库与导入脚本。
