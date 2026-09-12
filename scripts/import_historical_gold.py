#!/usr/bin/env python3
"""
导入往年黄金日线到 gold_extremes.db 的 daily_bars，并写入单位 unit。

默认 FreeGoldAPI → source=gj, unit=USD/oz

换算为积存金常用「元/克」：
  python import_historical_gold.py --to-cny-g --fx 7.25 --source zs
  公式: CNY/g = USD/oz * fx / 31.1034768

也可同时保留两种：
  python import_historical_gold.py --prefer freegold
  python import_historical_gold.py --to-cny-g --fx 7.25 --source zs
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

UA = "GoldPriceBarLite-HistoryImport/0.8.3"
OZ_TO_G = 31.1034768


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
          unit TEXT,
          PRIMARY KEY(trade_date, source)
        );
        """
    )
    try:
        conn.execute("ALTER TABLE daily_bars ADD COLUMN unit TEXT")
    except sqlite3.OperationalError:
        pass
    conn.commit()


def upsert_bar(
    conn: sqlite3.Connection,
    day: str,
    source: str,
    close: float,
    unit: str,
) -> None:
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    conn.execute(
        """
        INSERT INTO daily_bars
          (trade_date, source, open_price, high_price, low_price, close_price, updated_at, unit)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(trade_date, source) DO UPDATE SET
          close_price=excluded.close_price,
          unit=excluded.unit,
          high_price=MAX(daily_bars.high_price, excluded.close_price),
          low_price=MIN(daily_bars.low_price, excluded.close_price),
          updated_at=excluded.updated_at
        """,
        (day, source, close, close, close, close, now, unit),
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
    base = "https://stooq.com/q/d/l/?s=xauusd&i=d"
    url = f"{base}&apikey={urllib.parse.quote(apikey)}" if apikey else base
    print("Downloading Stooq XAUUSD ...")
    try:
        raw = http_get(url, timeout=60).decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        print(f"  Stooq HTTP {e.code}: {e.reason}")
        if e.code == 404:
            print("  需要 --stooq-apikey（Stooq 2026 起常要求密钥）")
        return []
    except Exception as e:
        print(f"  Stooq failed: {e}")
        return []
    if raw.lstrip().startswith("<!") or "<html" in raw[:200].lower():
        print("  Stooq 返回 HTML 而非 CSV，跳过")
        return []
    rows: list[tuple[str, float]] = []
    for r in csv.DictReader(io.StringIO(raw)):
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
    print(f"  Stooq rows: {len(rows)}")
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description="Import historical gold into daily_bars with unit")
    ap.add_argument("--db", type=Path, default=None)
    ap.add_argument("--source", default="gj", help="写入 source 标签（默认 gj）")
    ap.add_argument("--min-year", type=int, default=1990)
    ap.add_argument("--prefer", choices=("stooq", "freegold", "both"), default="freegold")
    ap.add_argument("--stooq-apikey", default=None)
    ap.add_argument(
        "--to-cny-g",
        action="store_true",
        help="将 USD/oz 换算为约元/克后写入（需 --fx）",
    )
    ap.add_argument(
        "--fx",
        type=float,
        default=7.25,
        help="美元兑人民币中间价近似，用于 --to-cny-g（默认 7.25）",
    )
    ap.add_argument(
        "--unit",
        default=None,
        help="强制 unit 字段；默认 USD/oz 或 CNY/g",
    )
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
            series[d] = p

    if not series:
        print("No data fetched.")
        return 1

    if args.to_cny_g:
        unit = args.unit or "CNY/g"
        print(f"Converting USD/oz → CNY/g with fx={args.fx}, unit={unit}")
        converted: dict[str, float] = {}
        for d, usd_oz in series.items():
            cny_g = usd_oz * args.fx / OZ_TO_G
            if cny_g > 0:
                converted[d] = cny_g
        series = converted
        print(f"  sample last: {sorted(series.items())[-1]}")
    else:
        unit = args.unit or "USD/oz"

    conn = sqlite3.connect(str(db_path))
    ensure_schema(conn)
    n = 0
    for d in sorted(series.keys()):
        upsert_bar(conn, d, args.source, series[d], unit)
        n += 1
        if n % 500 == 0:
            conn.commit()
    conn.commit()
    conn.close()
    print(f"Upserted {n} rows source={args.source} unit={unit}")
    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
