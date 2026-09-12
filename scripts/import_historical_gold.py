#!/usr/bin/env python3
"""
导入往年黄金日线到 GoldPriceBar SQLite（daily_bars）

数据源：
  1) FreeGoldAPI  https://freegoldapi.com/data/latest.json  （免 Key，默认）
  2) Stooq XAUUSD 日线（2026 起通常需要 apikey，否则会 404/返回 HTML）
     获取 Key: 打开 https://stooq.com/q/d/?s=xauusd 按页面说明 get_apikey
     然后: --stooq-apikey YOUR_KEY

用法：
  python import_historical_gold.py --min-year 1990 --source gj
  python import_historical_gold.py --prefer both --stooq-apikey xxx
"""
from __future__ import annotations

import argparse
import csv
import io
import json
import os
import sqlite3
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

UA = "GoldPriceBarLite-HistoryImport/0.8.2 (+https://github.com/kzq0717/GoldPriceBar)"


def default_db_path() -> Path:
    if sys.platform.startswith("win"):
        base = os.environ.get("APPDATA") or str(Path.home())
        return Path(base) / "GoldPriceBarLite" / "gold_extremes.db"
    xdg = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(xdg) / "GoldPriceBarLite" / "gold_extremes.db"


def http_get(url: str, timeout: int = 120) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "*/*"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def ensure_schema(conn: sqlite3.Connection) -> None:
    conn.executescript(
        """
        CREATE TABLE IF NOT EXISTS daily_bars (
          trade_date TEXT NOT NULL,
          source TEXT NOT NULL,
          open_price REAL NOT NULL,
          high_price REAL NOT NULL,
          low_price REAL NOT NULL,
          close_price REAL NOT NULL,
          updated_at TEXT NOT NULL,
          PRIMARY KEY(trade_date, source)
        );
        """
    )
    conn.commit()


def upsert_close(conn: sqlite3.Connection, day: str, source: str, close: float) -> None:
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    conn.execute(
        """
        INSERT INTO daily_bars (trade_date, source, open_price, high_price, low_price, close_price, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(trade_date, source) DO UPDATE SET
          close_price=excluded.close_price,
          high_price=MAX(daily_bars.high_price, excluded.close_price),
          low_price=MIN(daily_bars.low_price, excluded.close_price),
          updated_at=excluded.updated_at
        """,
        (day, source, close, close, close, close, now),
    )


def fetch_freegoldapi(min_year: int) -> list[tuple[str, float]]:
    url = "https://freegoldapi.com/data/latest.json"
    print(f"Downloading {url} ...")
    data = json.loads(http_get(url).decode("utf-8"))
    rows: list[tuple[str, float]] = []
    for item in data:
        d = str(item.get("date", ""))[:10]
        try:
            price = float(item.get("price"))
        except (TypeError, ValueError):
            continue
        if len(d) < 4:
            continue
        try:
            y = int(d[:4])
        except ValueError:
            continue
        if y < min_year or price <= 0:
            continue
        if len(d) == 4:
            d = f"{d}-12-31"
        elif len(d) == 7:
            d = f"{d}-01"
        rows.append((d, price))
    print(f"  FreeGoldAPI rows >= {min_year}: {len(rows)}")
    return rows


def fetch_stooq_xauusd(min_year: int, apikey: str | None) -> list[tuple[str, float]]:
    """
    Stooq 自约 2026 起对 CSV 下载常要求 apikey；无 Key 时可能 404 或返回 HTML 登录页。
    文档: https://stooq.com/q/d/?s=xauusd  申请: ...&get_apikey
    """
    base = "https://stooq.com/q/d/l/?s=xauusd&i=d"
    if apikey:
        url = f"{base}&apikey={urllib.parse.quote(apikey)}"
    else:
        url = base
    print(f"Downloading Stooq XAUUSD ...")
    try:
        raw = http_get(url, timeout=60).decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        print(f"  Stooq HTTP {e.code}: {e.reason}")
        if e.code == 404:
            print(
                "  原因说明: Stooq 已不再对匿名 CSV 直链稳定开放（2026 起多要求 apikey）。"
                " 请打开 https://stooq.com/q/d/?s=xauusd 获取 apikey 后使用 --stooq-apikey。"
            )
        return []
    except Exception as e:
        print(f"  Stooq failed: {e}")
        return []

    if raw.lstrip().startswith("<!") or "<html" in raw[:200].lower():
        print(
            "  Stooq 返回了 HTML 而非 CSV（通常需要登录/apikey）。"
            " 跳过 Stooq；FreeGoldAPI 数据仍会写入。"
        )
        return []

    rows: list[tuple[str, float]] = []
    reader = csv.DictReader(io.StringIO(raw))
    if not reader.fieldnames:
        print("  Stooq CSV 无表头，跳过")
        return []
    for r in reader:
        d = (r.get("Date") or r.get("date") or "").strip()
        c = r.get("Close") or r.get("close")
        if not d or c is None:
            continue
        try:
            y = int(d[:4])
            price = float(c)
        except ValueError:
            continue
        if y < min_year or price <= 0:
            continue
        rows.append((d, price))
    print(f"  Stooq XAUUSD rows: {len(rows)}")
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description="Import historical gold closes into gold_extremes.db")
    ap.add_argument("--db", type=Path, default=None, help="SQLite path")
    ap.add_argument("--source", default="gj", help="source tag (default gj)")
    ap.add_argument("--min-year", type=int, default=1990)
    ap.add_argument(
        "--prefer",
        choices=("stooq", "freegold", "both"),
        default="freegold",
        help="默认仅 FreeGoldAPI；both/stooq 可再试 Stooq",
    )
    ap.add_argument("--stooq-apikey", default=None, help="Stooq CSV apikey（可选）")
    args = ap.parse_args()

    db_path = args.db or default_db_path()
    db_path.parent.mkdir(parents=True, exist_ok=True)
    print(f"Database: {db_path}")

    series: dict[str, float] = {}
    if args.prefer in ("freegold", "both"):
        for d, p in fetch_freegoldapi(args.min_year):
            series[d] = p
    if args.prefer in ("stooq", "both"):
        for d, p in fetch_stooq_xauusd(args.min_year, args.stooq_apikey):
            series[d] = p  # 同日以 Stooq 覆盖

    if not series:
        print("No data fetched.")
        return 1

    conn = sqlite3.connect(str(db_path))
    ensure_schema(conn)
    n = 0
    for d in sorted(series.keys()):
        upsert_close(conn, d, args.source, series[d])
        n += 1
        if n % 500 == 0:
            conn.commit()
    conn.commit()
    conn.close()
    print(f"Upserted {n} daily closes for source={args.source}")
    print("Done. Restart GoldPriceBarLite to use MA / month charts with history.")
    if args.prefer in ("stooq", "both") and not args.stooq_apikey:
        print(
            "提示: 未提供 --stooq-apikey 时 Stooq 常失败；仅 FreeGoldAPI 已足够做中长期日线建模。"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
