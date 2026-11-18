with open(r"D:\Company\SK\logs\app-20261116.log", "r", encoding="utf-8", errors="ignore") as f:
    for line in f:
        if any(t in line for t in ["13:12:", "13:13:", "13:47:", "13:48:", "13:49:", "13:50:", "15:48:"]):
            if any(k in line for k in ["AutoTrade", "decision", "Decision", "Execute", "Trade", "trade", "Level", "level", "tag", "破位", "止损", "买入", "卖出"]):
                print(line.strip())
