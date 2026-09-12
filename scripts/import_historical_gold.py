#!/usr/bin/env python3
"""
导入往年黄金日线到 gold_extremes.db 的 daily_bars，并写入单位 unit。

【重要】默认库路径必须与客户端一致：
  Windows Qt AppDataLocation =
    %APPDATA%\\GoldPriceBarLite\\GoldPriceBarLite\\gold_extremes.db
  （组织名 + 应用名 均为 GoldPriceBarLite）

若在设置里自定义了「数据库目录」，请用：
  python import_historical_gold.py --db "你的目录\\gold_extremes.db"

换算元/克：
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

UA = "GoldPriceBarLite-HistoryImport/0.8.6"
OZ_TO_G = 31.1034768


def default_db_path() -> Path:
    """与 Qt QStandardPaths::AppDataLocation + gold_extremes.db 对齐。"""
    if sys.platform.startswith("win"):
        base = os.environ.get("APPDATA") or str(Path.home() / "AppData" / "Roaming")
        # OrganizationName / ApplicationName 均为 GoldPriceBarLite
        return Path(base) / "GoldPriceBarLite" / "GoldPriceBarLite" / "gold_extremes.db"
    xdg = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(xdg) / "GoldPriceBarLite" / "GoldPriceBarLite" / "gold_extremes.db"


def legacy_db_path() -> Path:
    """旧脚本曾用的路径（少一层目录），导入后可提示迁移。"""
    if sys.platform.startswith("win"):
        base = os.environ.get("APPDATA") or str(Path.home() / "AppData" / "Roaming")
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
    # 避免部分 SQLite 对 ON CONFLICT 中 MAX/MIN 支持差异，分两步
    conn.execute(
        """
        INSERT INTO daily_bars
          (trade_date, source, open_price, high_price, low_price, close_price, updated_at, unit)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(trade_date, source) DO UPDATE SET
          close_price=excluded.close_price,
          unit=excluded.unit,
          updated_at=excluded.updated_at
        """,
        (day, source, close, close, close, close, now, unit),
    )
    conn.execute(
        """
        UPDATE daily_bars SET
          high_price = CASE WHEN high_price < ? THEN ? ELSE high_price END,
          low_price  = CASE WHEN low_price <= 0 OR low_price > ? THEN ? ELSE low_price END
        WHERE trade_date=? AND source=?
        """,
        (close, close, close, close, day, source),
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
        return []
    except Exception as e:
        print(f"  Stooq failed: {e}")
        return []
    if raw.lstrip().startswith("<!") or "<html" in raw[:200].lower():
        print("  Stooq 返回 HTML，跳过")
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


def verify(conn: sqlite3.Connection, source: str) -> None:
    cur = conn.execute(
        "SELECT COUNT(*), MIN(trade_date), MAX(trade_date), unit FROM daily_bars WHERE source=? GROUP BY unit",
        (source,),
    )
    rows = cur.fetchall()
    if not rows:
        print(f"[校验] source={source} → 0 行（写入失败或查错库）")
        return
    for cnt, d0, d1, unit in rows:
        print(f"[校验] source={source} unit={unit or '(空)'} → {cnt} 行, {d0} ~ {d1}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Import historical gold into daily_bars with unit")
    ap.add_argument("--db", type=Path, default=None, help="完整路径到 gold_extremes.db")
    ap.add_argument("--source", default="gj")
    ap.add_argument("--min-year", type=int, default=1990)
    ap.add_argument("--prefer", choices=("stooq", "freegold", "both"), default="freegold")
    ap.add_argument("--stooq-apikey", default=None)
    ap.add_argument("--to-cny-g", action="store_true")
    ap.add_argument("--fx", type=float, default=7.25)
    ap.add_argument("--unit", default=None)
    args = ap.parse_args()

    db_path = args.db or default_db_path()
    db_path = db_path.resolve()
    db_path.parent.mkdir(parents=True, exist_ok=True)
    print(f"Database: {db_path}")
    print(f"  (客户端默认也是此路径；若设置里改过「数据库目录」，请用 --db 指定)")

    leg = legacy_db_path()
    if leg.exists() and leg.resolve() != db_path and not args.db:
        try:
            n_legacy = sqlite3.connect(str(leg)).execute(
                "SELECT COUNT(*) FROM daily_bars"
            ).fetchone()[0]
        except Exception:
            n_legacy = "?"
        print(
            f"注意: 发现旧路径库 {leg}（约 {n_legacy} 行 daily_bars）。\n"
            f"      旧脚本写在这里，客户端读的是新路径，所以界面里像「没数据」。\n"
            f"      本次将写入正确路径；需要的话可自行合并两库。"
        )

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
        print(f"Converting USD/oz → CNY/g with fx={args.fx}")
        series = {
            d: (usd * args.fx / OZ_TO_G)
            for d, usd in series.items()
            if usd * args.fx / OZ_TO_G > 0
        }
        sample = sorted(series.items())[-1]
        print(f"  sample: {sample[0]} → {sample[1]:.4f} {unit}")
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
            print(f"  ... committed {n}")
    conn.commit()
    print(f"Upserted {n} rows source={args.source} unit={unit}")
    verify(conn, args.source)
    conn.close()
    print("Done. 重启 GoldPriceBarLite 后查看分时均线/月份曲线。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
