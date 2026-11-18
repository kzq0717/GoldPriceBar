with open(r"D:\Company\SK\logs\app-20261116.log", "r", encoding="utf-8", errors="ignore") as f:
    for line in f:
        if any(f"13:{m}:" in line for m in range(47, 51)):
            print(line.strip())
