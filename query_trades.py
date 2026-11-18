import sqlite3
conn = sqlite3.connect(r'D:\Company\SK\gold_extremes.db')
c = conn.cursor()
c.execute("SELECT id, trade_time, source, action, decision_tag, price, grams, amount, fee, realized_pnl, remaining_grams, remaining_cost, note FROM simulated_trades WHERE trade_time >= '2026-11-16' ORDER BY id ASC")
rows = c.fetchall()
print(f"Total rows: {len(rows)}")
for r in rows:
    print(r)
