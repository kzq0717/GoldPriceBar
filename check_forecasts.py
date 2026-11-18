import sqlite3
conn = sqlite3.connect(r'D:\Company\SK\gold_extremes.db')
c = conn.cursor()
c.execute("SELECT id, created_at, pred_high, pred_low, high_window, low_window, catalyst, bias FROM forecast_logs WHERE created_at >= '2026-11-16' ORDER BY id ASC")
for r in c.fetchall():
    print(r)
