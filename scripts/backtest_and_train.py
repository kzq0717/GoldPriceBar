#!/usr/bin/env python3
"""GoldPriceBarLite 量化回测与分布校准流水线 (Walk-Forward + HAR-RV + 共形分位数 + 跨市场领先信号)。

功能：
1. 读取 gold_extremes.db 中的 daily_bars / intraday_samples / secondary_quotes / forecast_logs；
2. 对 daily_bars 执行严格时间序列滚动 Walk-Forward 回测，对比：
   - 基准模型：静态 10 日 ATR 区间
   - 升级模型：HAR-RV + Garman-Klass 波动率 + Kaufman ER 状态识别 + 在线共形分位数校准
   - 可选 LightGBM：传入 --train-lgbm 且本机已安装 numpy/lightgbm 时，执行滚动 Quantile (0.10/0.90) 训练对比
3. 提取 48 桶半小时分时波动率曲线与国内/伦敦金跨市场领先 Z-score；
4. 审计 forecast_logs 历史已结算预测的 命中率 / PICP 覆盖率 / Winkler 区间评分 / MAE。

用法：
  python scripts/backtest_and_train.py --source zs
  python scripts/backtest_and_train.py --db /path/to/gold_extremes.db --source gj --fee-rate 0.0
"""

from __future__ import annotations

import argparse
import configparser
import json
import math
import os
import sqlite3
import statistics
import sys
from pathlib import Path
from typing import Any

ORG = "GoldPriceBarLite"
APP = "GoldPriceBarLite"
DB_NAME = "gold_extremes.db"


def windows_appdata() -> Path:
    base = os.environ.get("APPDATA") or str(Path.home() / "AppData" / "Roaming")
    return Path(base)


def default_db_path() -> Path:
    if sys.platform.startswith("win"):
        return windows_appdata() / ORG / APP / DB_NAME
    xdg = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(xdg) / ORG / APP / DB_NAME


def candidate_ini_paths() -> list[Path]:
    paths: list[Path] = []
    if sys.platform.startswith("win"):
        paths.append(windows_appdata() / f"{ORG}.ini")
        paths.append(windows_appdata() / ORG / f"{APP}.ini")
    else:
        xdg_cfg = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
        paths.append(Path(xdg_cfg) / ORG / f"{APP}.ini")
    return paths


def resolve_db_path(cli_db: str | None) -> Path:
    if cli_db:
        return Path(cli_db).expanduser().resolve()
    for ini_path in candidate_ini_paths():
        if ini_path.exists():
            cp = configparser.ConfigParser()
            try:
                cp.read(ini_path, encoding="utf-8")
                for section in cp.sections() + ["General"]:
                    if cp.has_option(section, "databaseDir"):
                        val = cp.get(section, "databaseDir").strip().strip('"')
                        if val:
                            return Path(val) / DB_NAME
            except Exception:
                pass
    return default_db_path()


def winkler_score(lower: float, upper: float, act_high: float, act_low: float, alpha: float = 0.20) -> float:
    width = max(0.0, upper - lower)
    miss_low = max(0.0, lower - act_low)
    miss_high = max(0.0, act_high - upper)
    return width + (2.0 / alpha) * (miss_low + miss_high)


def is_hit(pred: float, actual: float) -> bool:
    tol = max(0.30, pred * 0.0005)
    return abs(pred - actual) <= tol


def garman_klass_range(o: float, h: float, l: float, c: float, prev_c: float | None) -> tuple[float, float]:
    tr = max(0.0, h - l)
    jump_var = 0.0
    if prev_c and prev_c > 0.0:
        tr = max(tr, abs(h - prev_c), abs(l - prev_c))
        if o > 0.0:
            lg = math.log(o / prev_c)
            jump_var = lg * lg
    gk_var = 0.0
    if h > l and o > 0.0 and c > 0.0:
        hl = math.log(h / l)
        co = math.log(c / o)
        gk_var = max(0.0, 0.5 * hl * hl - (2.0 * math.log(2.0) - 1.0) * co * co)
    gk_range = 1.5958 * c * math.sqrt(gk_var + 0.5 * jump_var) if c > 0.0 else tr
    blended = 0.55 * tr + 0.45 * gk_range if gk_range > 0.0 else tr
    return tr, blended


def wilder_rsi(closes: list[float], period: int = 14) -> float:
    if len(closes) <= period:
        return 50.0
    gains = 0.0
    losses = 0.0
    for i in range(1, period + 1):
        d = closes[i] - closes[i - 1]
        if d >= 0.0:
            gains += d
        else:
            losses -= d
    avg_gain = gains / period
    avg_loss = losses / period
    for i in range(period + 1, len(closes)):
        d = closes[i] - closes[i - 1]
        g = d if d > 0.0 else 0.0
        lo = -d if d < 0.0 else 0.0
        avg_gain = (avg_gain * (period - 1) + g) / period
        avg_loss = (avg_loss * (period - 1) + lo) / period
    if avg_loss < 1e-12:
        return 50.0 if avg_gain < 1e-12 else 100.0
    rs = avg_gain / avg_loss
    return 100.0 - 100.0 / (1.0 + rs)


def kaufman_er(closes: list[float], window: int = 10) -> float:
    if len(closes) <= window:
        return 0.0
    net = abs(closes[-1] - closes[-1 - window])
    path = sum(abs(closes[i] - closes[i - 1]) for i in range(len(closes) - window, len(closes)))
    return min(1.0, net / path) if path > 1e-12 else 0.0


def conformal_scale(
    pred_highs: list[float],
    pred_lows: list[float],
    act_highs: list[float],
    act_lows: list[float],
    target_cov: float = 0.80,
) -> float:
    ratios: list[float] = []
    for ph, pl, ah, al in zip(pred_highs, pred_lows, act_highs, act_lows):
        if ph <= pl or ah < al:
            continue
        half = max(0.10, 0.5 * (ph - pl))
        mid = 0.5 * (ph + pl)
        req = max(ah - mid, mid - al)
        ratios.append(max(0.5, min(2.2, req / half)))
    if len(ratios) < 5:
        return 1.0
    ratios.sort()
    idx = min(len(ratios) - 1, max(0, math.ceil(target_cov * len(ratios)) - 1))
    emp_q = ratios[idx]
    alpha = min(1.0, len(ratios) / 20.0)
    return max(0.78, min(1.45, 1.0 + alpha * (emp_q - 1.0)))


def run_walk_forward(
    bars: list[tuple[str, float, float, float, float]],
    target_coverage: float,
    fee_rate: float,
    train_lgbm: bool,
) -> dict[str, Any]:
    if len(bars) < 35:
        return {"error": f"日线样本不足 ({len(bars)} 条，至少需要 35 条)"}

    trs: list[float] = []
    har_inputs: list[float] = []
    closes: list[float] = []

    rec_ph: list[float] = []
    rec_pl: list[float] = []
    rec_ah: list[float] = []
    rec_al: list[float] = []

    model_cov = 0
    naive_cov = 0
    model_winkler = 0.0
    naive_winkler = 0.0
    model_mae_h = 0.0
    model_mae_l = 0.0
    naive_mae_h = 0.0
    naive_mae_l = 0.0
    model_hits = 0
    naive_hits = 0
    trade_signals = 0
    trade_net_pnl = 0.0

    lgbm_features: list[list[float]] = []
    lgbm_targets_h: list[float] = []
    lgbm_targets_l: list[float] = []

    eval_count = 0
    for i, (_dt, o, h, l, c) in enumerate(bars):
        prev_c = closes[-1] if closes else None
        tr, blended = garman_klass_range(o, h, l, c, prev_c)

        if i >= 25 and prev_c and prev_c > 0.0:
            rv1 = har_inputs[-1]
            rv5 = sum(har_inputs[-5:]) / 5.0
            rv22 = sum(har_inputs[-22:]) / 22.0
            har_range = 0.25 * rv1 + 0.45 * rv5 + 0.30 * rv22

            atr10 = sum(trs[-10:]) / 10.0
            er10 = kaufman_er(closes, 10)
            rsi14 = wilder_rsi(closes, 14)

            ma5 = sum(closes[-5:]) / 5.0
            ma20 = sum(closes[-20:]) / 20.0
            drift = 0.0
            if ma5 > ma20:
                drift += 0.06
            else:
                drift -= 0.06
            if er10 < 0.25:
                drift *= 0.45

            c_scale = conformal_scale(rec_ph[-30:], rec_pl[-30:], rec_ah[-30:], rec_al[-30:], target_coverage)
            sigma_est = max(0.2, har_range / 1.52)
            q_half = 1.38 * sigma_est * c_scale

            anchor = o if o > 0.0 else prev_c
            up_ratio = max(0.35, min(0.65, 0.50 + drift))
            down_ratio = 1.0 - up_ratio
            m_high = anchor + q_half * (2.0 * up_ratio)
            m_low = anchor - q_half * (2.0 * down_ratio)

            n_high = anchor + atr10 * 0.55
            n_low = anchor - atr10 * 0.55

            tol = max(0.30, h * 0.0005)
            if h <= m_high + tol and l >= m_low - tol:
                model_cov += 1
            if h <= n_high + tol and l >= n_low - tol:
                naive_cov += 1

            model_winkler += winkler_score(m_low, m_high, h, l, alpha=1.0 - target_coverage)
            naive_winkler += winkler_score(n_low, n_high, h, l, alpha=1.0 - target_coverage)
            model_mae_h += abs(m_high - h)
            model_mae_l += abs(m_low - l)
            naive_mae_h += abs(n_high - h)
            naive_mae_l += abs(n_low - l)
            model_hits += int(is_hit(m_high, h)) + int(is_hit(m_low, l))
            naive_hits += int(is_hit(n_high, h)) + int(is_hit(n_low, l))
            eval_count += 1

            # Fee-aware swing filter check: enter at anchor if upside/risk covers 0.4% sell fee + RRR >= 1.4
            exp_gain = m_high - anchor
            exp_risk = max(0.10, anchor - m_low)
            net_gain = exp_gain - c * fee_rate
            if er10 >= 0.30 and rsi14 < 68.0 and net_gain >= 1.2 and (net_gain / exp_risk) >= 1.4:
                trade_signals += 1
                trade_net_pnl += (c - anchor) - c * fee_rate

            rec_ph.append(m_high)
            rec_pl.append(m_low)
            rec_ah.append(h)
            rec_al.append(l)

            lgbm_features.append([rv1, rv5, rv22, atr10, er10, rsi14, (closes[-1] / ma20 - 1.0) if ma20 > 0 else 0.0])
            lgbm_targets_h.append(h - anchor)
            lgbm_targets_l.append(l - anchor)

        trs.append(tr)
        har_inputs.append(blended)
        closes.append(c)

    out: dict[str, Any] = {
        "evaluated_days": eval_count,
        "har_conformal": {
            "picp_pct": round(100.0 * model_cov / eval_count, 2),
            "winkler_score": round(model_winkler / eval_count, 3),
            "hit_rate_pct": round(100.0 * model_hits / (2 * eval_count), 2),
            "mae_high": round(model_mae_h / eval_count, 3),
            "mae_low": round(model_mae_l / eval_count, 3),
        },
        "naive_atr10": {
            "picp_pct": round(100.0 * naive_cov / eval_count, 2),
            "winkler_score": round(naive_winkler / eval_count, 3),
            "hit_rate_pct": round(100.0 * naive_hits / (2 * eval_count), 2),
            "mae_high": round(naive_mae_h / eval_count, 3),
            "mae_low": round(naive_mae_l / eval_count, 3),
        },
        "fee_aware_trades": {
            "count": trade_signals,
            "cumulative_net_pnl_per_unit": round(trade_net_pnl, 3),
        },
    }

    if train_lgbm:
        try:
            import lightgbm as lgb  # type: ignore
            import numpy as np  # type: ignore

            if len(lgbm_features) >= 80:
                X = np.asarray(lgbm_features, dtype=float)
                yh = np.asarray(lgbm_targets_h, dtype=float)
                yl = np.asarray(lgbm_targets_l, dtype=float)
                split = max(40, int(len(X) * 0.65))
                q_lo_model = lgb.LGBMRegressor(objective="quantile", alpha=(1.0 - target_coverage) * 0.5, n_estimators=80, learning_rate=0.05, verbose=-1)
                q_hi_model = lgb.LGBMRegressor(objective="quantile", alpha=1.0 - (1.0 - target_coverage) * 0.5, n_estimators=80, learning_rate=0.05, verbose=-1)
                q_lo_model.fit(X[:split], yl[:split])
                q_hi_model.fit(X[:split], yh[:split])
                pred_lo = q_lo_model.predict(X[split:])
                pred_hi = q_hi_model.predict(X[split:])
                cov = 0
                wink = 0.0
                for pl_off, ph_off, al_off, ah_off in zip(pred_lo, pred_hi, yl[split:], yh[split:]):
                    if ah_off <= ph_off and al_off >= pl_off:
                        cov += 1
                    wink += winkler_score(float(pl_off), float(ph_off), float(ah_off), float(al_off), alpha=1.0 - target_coverage)
                n_test = len(X) - split
                out["lgbm_quantile_oos"] = {
                    "oos_days": n_test,
                    "picp_pct": round(100.0 * cov / n_test, 2),
                    "winkler_score": round(wink / n_test, 3),
                }
            else:
                out["lgbm_quantile_oos"] = {"skipped": f"样本 {len(lgbm_features)} < 80，暂跳过 GBDT"}
        except ImportError:
            out["lgbm_quantile_oos"] = {"skipped": "未安装 lightgbm/numpy，已自动使用纯标准库 HAR-RV + 共形引擎"}

    return out


def analyze_intraday_profile(conn: sqlite3.Connection, source: str) -> dict[str, Any]:
    rows = conn.execute(
        "SELECT trade_date, minute_key, price FROM intraday_samples WHERE source = ? ORDER BY trade_date ASC, minute_key ASC",
        (source,),
    ).fetchall()
    if len(rows) < 20:
        return {"days": 0, "top_30min_buckets": []}

    bucket_sq = [0.0] * 48
    bucket_cnt = [0] * 48
    days = set()
    prev_day = None
    prev_px = 0.0
    for day, mk, px in rows:
        days.add(day)
        if px is None or px <= 0.0:
            continue
        if day == prev_day and prev_px > 0.0 and mk and len(mk) >= 5:
            try:
                hh, mm = int(mk[:2]), int(mk[3:5])
                b = max(0, min(47, (hh * 60 + mm) // 30))
                d = px - prev_px
                bucket_sq[b] += d * d
                bucket_cnt[b] += 1
            except ValueError:
                pass
        prev_day = day
        prev_px = float(px)

    per_min_var = [(bucket_sq[i] / bucket_cnt[i]) if bucket_cnt[i] >= 3 else 0.0 for i in range(48)]
    total = sum(per_min_var)
    if total <= 0.0:
        return {"days": len(days), "top_30min_buckets": []}
    ranked = sorted(
        [
            (
                f"{(i * 30) // 60:02d}:{(i * 30) % 60:02d}-{((i + 1) * 30) // 60:02d}:{((i + 1) * 30) % 60:02d}",
                round(100.0 * per_min_var[i] / total, 2),
                bucket_cnt[i],
            )
            for i in range(48)
            if per_min_var[i] > 0.0
        ],
        key=lambda x: x[1],
        reverse=True,
    )
    return {"days": len(days), "top_30min_buckets": ranked[:8]}


def analyze_cross_market_lead(conn: sqlite3.Connection, source: str) -> dict[str, Any]:
    rows = conn.execute(
        "SELECT ts, ratio FROM secondary_quotes WHERE primary_source = ? AND ratio > 0 ORDER BY ts DESC LIMIT 60",
        (source,),
    ).fetchall()
    if len(rows) < 10:
        return {"samples": len(rows), "latest_z": None}
    ratios = [float(r[1]) for r in rows]
    mean_r = statistics.fmean(ratios)
    stdev_r = statistics.pstdev(ratios)
    z = (ratios[0] - mean_r) / stdev_r if stdev_r > 1e-9 else 0.0
    return {
        "samples": len(ratios),
        "latest_ratio": round(ratios[0], 4),
        "mean_ratio": round(mean_r, 4),
        "latest_z": round(z, 3),
    }


def audit_forecast_logs(conn: sqlite3.Connection, source: str) -> dict[str, Any]:
    rows = conn.execute(
        """SELECT pred_high, pred_low, actual_high, actual_low
           FROM forecast_logs
           WHERE settled = 1 AND source = ? AND actual_high > 0 AND actual_low > 0
           ORDER BY made_at DESC LIMIT 200""",
        (source,),
    ).fetchall()
    if not rows:
        return {"settled_count": 0}
    cov = 0
    hits = 0
    wink = 0.0
    mae = 0.0
    for ph, pl, ah, al in rows:
        tol = max(0.30, ph * 0.0005)
        if ah <= ph + tol and al >= pl - tol:
            cov += 1
        hits += int(is_hit(ph, ah)) + int(is_hit(pl, al))
        wink += winkler_score(pl, ph, ah, al, alpha=0.20)
        mae += 0.5 * (abs(ph - ah) + abs(pl - al))
    n = len(rows)
    return {
        "settled_count": n,
        "picp_pct": round(100.0 * cov / n, 2),
        "hit_rate_pct": round(100.0 * hits / (2 * n), 2),
        "mean_winkler": round(wink / n, 3),
        "mae": round(mae / n, 3),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="GoldPriceBarLite 量化回测与模型审计脚本")
    parser.add_argument("--db", help="gold_extremes.db 路径（可选，默认自动读取软件配置）")
    parser.add_argument("--source", default="zs", help="品种数据源: zs / ms / gj (默认 zs)")
    parser.add_argument("--fee-rate", type=float, default=None, help="卖出费率（zs/ms默认0.004, gj默认0.0）")
    parser.add_argument("--target-coverage", type=float, default=0.80, help="共形目标区间覆盖率 (默认 0.80)")
    parser.add_argument("--train-lgbm", action="store_true", help="尝试运行 LightGBM Quantile 对比训练")
    parser.add_argument("--json-out", help="将结果输出为 JSON 文件")
    args = parser.parse_args()

    db_path = resolve_db_path(args.db)
    if not db_path.exists():
        print(f"未找到数据库文件: {db_path}", file=sys.stderr)
        return 1

    fee_rate = args.fee_rate
    if fee_rate is None:
        fee_rate = 0.0 if args.source in ("gj", "xau") else 0.004

    conn = sqlite3.connect(str(db_path))
    try:
        bars = conn.execute(
            """SELECT trade_date, open_price, high_price, low_price, close_price
               FROM daily_bars WHERE source = ? AND close_price > 0
               ORDER BY trade_date ASC""",
            (args.source,),
        ).fetchall()

        report = {
            "db_path": str(db_path),
            "source": args.source,
            "fee_rate": fee_rate,
            "walk_forward": run_walk_forward(bars, args.target_coverage, fee_rate, args.train_lgbm),
            "intraday_variance_profile": analyze_intraday_profile(conn, args.source),
            "cross_market_lead": analyze_cross_market_lead(conn, args.source),
            "forecast_logs_audit": audit_forecast_logs(conn, args.source),
        }
    finally:
        conn.close()

    print(json.dumps(report, ensure_ascii=False, indent=2))
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
