import sqlite3
conn = sqlite3.connect(r'D:\Company\SK\gold_extremes.db')
c = conn.cursor()
c.execute("SELECT id, made_at, pred_high, pred_low, pred_high_time, pred_low_time, catalyst, brief FROM forecast_logs WHERE made_at >= '2026-11-16T12:00:00' AND made_at <= '2026-11-16T14:30:00' ORDER BY id ASC")
for r in c.fetchall():
    print(r)
