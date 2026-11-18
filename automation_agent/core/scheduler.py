import re
from datetime import datetime, timedelta
from typing import Optional, Tuple

class ScheduleParser:
    """解析多样化调度规则并计算下一次执行时间：
    - 每日定点: "10:00" 或 "daily@10:00"
    - 周期循环: "interval@30m", "interval@1h", "every_10s"
    - Cron 简易规则: "0 10 * * *" (每天10:00)
    """

    @staticmethod
    def parse_daily_time(rule_str: str) -> Optional[Tuple[int, int]]:
        """提取每日时刻 (hour, minute)"""
        m = re.match(r"^(?:daily@)?(\d{1,2}):(\d{2})$", rule_str.strip())
        if m:
            h, minute = int(m.group(1)), int(m.group(2))
            if 0 <= h <= 23 and 0 <= minute <= 59:
                return h, minute
        return None

    @staticmethod
    def parse_interval(rule_str: str) -> Optional[timedelta]:
        """提取时间间隔"""
        m = re.match(r"^(?:interval@|every_)?(\d+)([smhd])$", rule_str.strip())
        if m:
            val, unit = int(m.group(1)), m.group(2)
            if unit == "s":
                return timedelta(seconds=val)
            elif unit == "m":
                return timedelta(minutes=val)
            elif unit == "h":
                return timedelta(hours=val)
            elif unit == "d":
                return timedelta(days=val)
        return None

    @staticmethod
    def parse_cron_5fields(rule_str: str) -> Optional[Tuple[str, str, str, str, str]]:
        """验证 5 位 cron 规则 (分 时 日 月 周)"""
        parts = rule_str.strip().split()
        if len(parts) == 5:
            return tuple(parts)
        return None

    @classmethod
    def get_next_run(cls, rule_str: str, from_dt: Optional[datetime] = None) -> Optional[datetime]:
        """计算距离 from_dt 的下一个执行时刻"""
        now = from_dt or datetime.now()

        # 1. 每日定点 (例如 "10:00")
        daily = cls.parse_daily_time(rule_str)
        if daily:
            hour, minute = daily
            target_today = now.replace(hour=hour, minute=minute, second=0, microsecond=0)
            if target_today > now:
                return target_today
            else:
                return target_today + timedelta(days=1)

        # 2. 间隔周期 (例如 "interval@1h" 或 "every_30m")
        interval = cls.parse_interval(rule_str)
        if interval:
            return now + interval

        # 3. 经典 Cron "0 10 * * *" -> 每天 10:00
        cron = cls.parse_cron_5fields(rule_str)
        if cron:
            min_field, hour_field, dom_field, mon_field, dow_field = cron
            # 常见形式: "0 10 * * *"
            if min_field.isdigit() and hour_field.isdigit() and dom_field == "*" and mon_field == "*" and dow_field == "*":
                hour, minute = int(hour_field), int(min_field)
                target_today = now.replace(hour=hour, minute=minute, second=0, microsecond=0)
                if target_today > now:
                    return target_today
                else:
                    return target_today + timedelta(days=1)

        # 默认兜底：如果无法识别，默认 24 小时后
        return now + timedelta(days=1)

    @classmethod
    def is_due(cls, next_run: Optional[datetime], now_dt: Optional[datetime] = None) -> bool:
        """检查任务当前是否已到期"""
        if not next_run:
            return True
        now = now_dt or datetime.now()
        return now >= next_run
