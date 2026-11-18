import sqlite3
conn = sqlite3.connect(r'D:\Company\SK\gold_extremes.db')
c = conn.cursor()
c.execute("SELECT datetime(time_bucket * 60, 'unixepoch', 'localtime'), open, high, low, close FROM minute_bars WHERE source = 'zs' AND time_bucket >= strftime('%s', '2026-11-16 13:45:00') ORDER BY time_bucket ASC")
rows = c.fetchall()
print(f"Minute bars count: {len(rows)}")
for r in rows[:15]:
    print(r)
print("...")
for r in rows[-10:]:
    print(r)
