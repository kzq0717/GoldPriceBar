import sqlite3
conn = sqlite3.connect(r'D:\Company\SK\gold_extremes.db')
c = conn.cursor()
c.execute("SELECT minute_key, ts, price FROM intraday_samples WHERE trade_date = '2026-11-16' AND source = 'zs' AND minute_key >= '13:10' AND minute_key <= '16:00' ORDER BY ts ASC")
rows = c.fetchall()
print(f"Sample count: {len(rows)}")
if rows:
    min_row = min(rows, key=lambda x: x[2])
    max_row = max(rows, key=lambda x: x[2])
    print("Lowest price:", min_row)
    print("Highest price:", max_row)
    for i in range(0, len(rows), max(1, len(rows)//15)):
        print(rows[i])
