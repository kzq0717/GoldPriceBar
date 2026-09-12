#!/usr/bin/env python3
"""
导入往年黄金日线到 GoldPriceBar SQLite（daily_bars）

数据源（均无需 Key，优先可用者）：
  1) FreeGoldAPI  https://freegoldapi.com/data/latest.json
     覆盖极长历史，近现代为 USD/盎司（粒度从年到日不等）
  2) 可选 Stooq XAUUSD 日线 CSV（若网络可达）
     https://stooq.com/q/d/l/?s=xauusd&i=d

用法：
  python import_historical_gold.py [--db PATH] [--source gj] [--min-year 1990]

默认数据库：
  Windows: %APPDATA%/GoldPriceBarLite/gold_extremes.db
  或当前目录 ./gold_extremes.db
"""
from __future__ import annotations

import argparse
import csv
import io
import json
import os
import sqlite3
import sys
import urllib.request
from datetime import datetime
from pathlib import Path


def default_db_path() -> Path:
    if sys.platform.startswith("win"):
        base = os.environ.get("APPDATA") or str(Path.home())
        return Path(base) / "GoldPriceBarLite" / "gold_extremes.db"
    xdg = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(xdg) / "GoldPriceBarLite" / "gold_extremes.db"


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
    """仅当无行或不想覆盖时插入；已有行则更新 close（历史回填）"""
    now = datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
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
    with urllib.request.urlopen(url, timeout=120) as resp:
        data = json.loads(resp.read().decode("utf-8"))
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
        # 归一到日：年/月数据也写成该日 close
        if len(d) == 4:
            d = f"{d}-12-31"
        elif len(d) == 7:
            d = f"{d}-01"
        rows.append((d, price))
    print(f"  FreeGoldAPI rows >= {min_year}: {len(rows)}")
    return rows


def fetch_stooq_xauusd(min_year: int) -> list[tuple[str, float]]:
    url = "https://stooq.com/q/d/l/?s=xauusd&i=d"
    print(f"Downloading {url} ...")
    try:
        with urllib.request.urlopen(url, timeout=60) as resp:
            text = resp.read().decode("utf-8", errors="replace")
    except Exception as e:
        print(f"  Stooq failed: {e}")
        return []
    rows: list[tuple[str, float]] = []
    reader = csv.DictReader(io.StringIO(text))
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
    ap.add_argument("--source", default="gj", help="source tag (default gj = London)")
    ap.add_argument("--min-year", type=int, default=1990)
    ap.add_argument("--prefer", choices=("stooq", "freegold", "both"), default="both")
    args = ap.parse_args()

    db_path = args.db or default_db_path()
    db_path.parent.mkdir(parents=True, exist_ok=True)
    print(f"Database: {db_path}")

    series: dict[str, float] = {}
    if args.prefer in ("freegold", "both"):
        for d, p in fetch_freegoldapi(args.min_year):
            series[d] = p  # later sources can override
    if args.prefer in ("stooq", "both"):
        for d, p in fetch_stooq_xauusd(args.min_year):
            series[d] = p  # daily stooq overrides freegold if same day

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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
