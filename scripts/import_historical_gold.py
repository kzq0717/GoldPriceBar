#!/usr/bin/env python3
"""
导入往年黄金日线到 gold_extremes.db（daily_bars + unit）。

数据库路径优先级（无需手动 --db）：
  1) 命令行 --db（可选覆盖）
  2) 客户端配置文件中的 databaseDir
     Windows: %APPDATA%\\GoldPriceBarLite\\GoldPriceBarLite.ini
     键名: databaseDir=（目录，文件名为 gold_extremes.db）
  3) Qt 默认 AppData：
     %APPDATA%\\GoldPriceBarLite\\GoldPriceBarLite\\gold_extremes.db

示例：
  python import_historical_gold.py --min-year 1990 --source gj
  python import_historical_gold.py --to-cny-g --fx 7.25 --source zs
"""
from __future__ import annotations

import argparse
import configparser
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
ORG = "GoldPriceBarLite"
APP = "GoldPriceBarLite"
DB_NAME = "gold_extremes.db"


def windows_appdata() -> Path:
    base = os.environ.get("APPDATA") or str(Path.home() / "AppData" / "Roaming")
    return Path(base)


def default_db_path() -> Path:
    """与 Qt QStandardPaths::AppDataLocation 对齐。"""
    if sys.platform.startswith("win"):
        return windows_appdata() / ORG / APP / DB_NAME
    xdg = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(xdg) / ORG / APP / DB_NAME


def candidate_ini_paths() -> list[Path]:
    """Qt IniFormat + UserScope 常见位置。"""
    paths: list[Path] = []
    if sys.platform.startswith("win"):
        ad = windows_appdata()
        paths += [
            ad / ORG / f"{APP}.ini",
            ad / ORG / APP / f"{APP}.ini",
            ad / f"{ORG}.ini",
        ]
    else:
        conf = Path.home() / ".config"
        paths += [
            conf / ORG / f"{APP}.conf",
            conf / ORG / f"{APP}.ini",
            conf / f"{ORG}.conf",
        ]
    # 项目内可选本地配置（若用户拷贝）
    here = Path(__file__).resolve().parent.parent
    paths += [
        here / f"{APP}.ini",
        here / "config.ini",
        here / "GoldPriceBar.ini",
    ]
    return paths


def read_database_dir_from_ini(ini: Path) -> str | None:
    """
    读取 databaseDir。兼容：
      databaseDir=D:/Company/SK
      [General] databaseDir=...
      %General]\\ndatabaseDir=...  (Qt 有时用)
    """
    try:
        text = ini.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None

    # 快速扫描：databaseDir=
    for line in text.splitlines():
        s = line.strip()
        if s.startswith(";") or s.startswith("#"):
            continue
        if s.lower().startswith("databasedir"):
            # databaseDir=value 或 databaseDir = value
            if "=" in s:
                val = s.split("=", 1)[1].strip().strip('"').strip("'")
                if val:
                    return val

    # configparser（可能丢 Qt 特殊段名，作补充）
    cp = configparser.ConfigParser()
    try:
        cp.read_string("[General]\n" + text if not text.lstrip().startswith("[") else text)
    except configparser.Error:
        return None
    for section in cp.sections():
        for key, val in cp.items(section):
            if key.lower() == "databasedir" and val.strip():
                return val.strip().strip('"').strip("'")
    return None


def resolve_db_path(cli_db: Path | None) -> Path:
    if cli_db is not None:
        p = cli_db.expanduser().resolve()
        print(f"使用命令行 --db: {p}")
        return p

    for ini in candidate_ini_paths():
        if not ini.is_file():
            continue
        raw = read_database_dir_from_ini(ini)
        if not raw:
            continue
        raw_path = Path(raw).expanduser()
        # 配置可能是目录或完整 db 文件
        if raw_path.suffix.lower() == ".db":
            db = raw_path.resolve()
        else:
            db = (raw_path / DB_NAME).resolve()
        print(f"从配置读取 databaseDir: {ini}")
        print(f"  databaseDir = {raw}")
        print(f"  → 数据库文件: {db}")
        return db

    db = default_db_path().resolve()
    print(f"未找到配置中的 databaseDir，使用默认: {db}")
    print(f"  已扫描: {', '.join(str(p) for p in candidate_ini_paths() if p.parent.exists() or p.exists())}")
    return db


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


def upsert_bar(conn: sqlite3.Connection, day: str, source: str, close: float, unit: str) -> None:
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
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
        "SELECT COUNT(*), MIN(trade_date), MAX(trade_date), IFNULL(unit,'') "
        "FROM daily_bars WHERE source=? GROUP BY unit",
        (source,),
    )
    rows = cur.fetchall()
    if not rows:
        print(f"[校验] source={source} → 0 行")
        return
    for cnt, d0, d1, unit in rows:
        print(f"[校验] source={source} unit={unit or '(空)'} → {cnt} 行, {d0} ~ {d1}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Import historical gold; DB path from app config")
    ap.add_argument("--db", type=Path, default=None, help="可选：覆盖配置中的库路径")
    ap.add_argument("--source", default="gj")
    ap.add_argument("--min-year", type=int, default=1990)
    ap.add_argument("--prefer", choices=("stooq", "freegold", "both"), default="freegold")
    ap.add_argument("--stooq-apikey", default=None)
    ap.add_argument("--to-cny-g", action="store_true")
    ap.add_argument("--fx", type=float, default=7.25)
    ap.add_argument("--unit", default=None)
    args = ap.parse_args()

    db_path = resolve_db_path(args.db)
    db_path.parent.mkdir(parents=True, exist_ok=True)

    series: dict[str, float] = {}
    if args.prefer in ("freegold", "both"):
        for d, px in fetch_freegoldapi(args.min_year):
            series[d] = px
    if args.prefer in ("stooq", "both"):
        for d, px in fetch_stooq_xauusd(args.min_year, args.stooq_apikey):
            series[d] = px

    if not series:
        print("No data fetched.")
        return 1

    if args.to_cny_g:
        unit = args.unit or "CNY/g"
        print(f"Converting USD/oz → CNY/g with fx={args.fx}")
        series = {d: usd * args.fx / OZ_TO_G for d, usd in series.items() if usd > 0}
        last = sorted(series.items())[-1]
        print(f"  sample: {last[0]} → {last[1]:.4f} {unit}")
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
    print(f"Upserted {n} rows → {db_path}")
    print(f"  source={args.source} unit={unit}")
    verify(conn, args.source)
    conn.close()
    print("Done. 重启客户端后生效。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
