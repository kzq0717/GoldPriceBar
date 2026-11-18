import sqlite3
conn = sqlite3.connect(r'D:\Company\SK\gold_extremes.db')
c = conn.cursor()
c.execute("PRAGMA table_info(forecast_logs)")
print("forecast_logs schema:", c.fetchall())
c.execute("SELECT * FROM forecast_logs ORDER BY id DESC LIMIT 5")
print("recent forecast_logs:", c.fetchall())
