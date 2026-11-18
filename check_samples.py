import sqlite3
conn = sqlite3.connect(r'D:\Company\SK\gold_extremes.db')
c = conn.cursor()
c.execute("PRAGMA table_info(intraday_samples)")
print("intraday_samples schema:", c.fetchall())
c.execute("SELECT sample_time, price FROM intraday_samples WHERE source = 'zs' AND sample_time >= '2026-11-16 13:45:00' AND sample_time <= '2026-11-16 16:00:00' ORDER BY sample_time ASC")
rows = c.fetchall()
print(f"Sample count: {len(rows)}")
if rows:
    min_row = min(rows, key=lambda x: x[1])
    max_row = max(rows, key=lambda x: x[1])
    print("Lowest price:", min_row)
    print("Highest price:", max_row)
    print("Every 10 mins:")
    for i in range(0, len(rows), 60):
        print(rows[i])
