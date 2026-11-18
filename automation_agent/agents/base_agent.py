from abc import ABC, abstractmethod
from datetime import datetime
from typing import Dict, Any, Optional
from core.scheduler import ScheduleParser
from core.notifier import Notifier

class BaseAgent(ABC):
    """自动化 Agent 抽象基类：规范生命周期、调度逻辑与状态管理"""

    def __init__(
        self,
        agent_id: str,
        name: str,
        agent_type: str,
        schedule_rule: str,
        enabled: bool = True,
        created_at: Optional[str] = None,
        last_run: Optional[str] = None,
        next_run: Optional[str] = None,
        config: Optional[Dict[str, Any]] = None
    ):
        self.agent_id = agent_id
        self.name = name
        self.agent_type = agent_type
        self.schedule_rule = schedule_rule
        self.enabled = enabled
        self.created_at = created_at or datetime.now().isoformat()
        self.last_run = last_run
        self.next_run = next_run
        self.config = config or {}

        # 若未指定 next_run，初始化计算下一次时间
        if not self.next_run:
            self.compute_next_run()

    def compute_next_run(self, from_dt: Optional[datetime] = None):
        """重新计算下一次执行时间"""
        nxt = ScheduleParser.get_next_run(self.schedule_rule, from_dt or datetime.now())
        self.next_run = nxt.strftime("%Y-%m-%d %H:%M:%S") if nxt else None

    def should_run(self, now_dt: Optional[datetime] = None) -> bool:
        """判断当前是否达到触发条件"""
        if not self.enabled:
            return False
        if not self.next_run:
            return True
        now = now_dt or datetime.now()
        try:
            target = datetime.strptime(self.next_run, "%Y-%m-%d %H:%M:%S")
            return now >= target
        except Exception:
            return True

    @abstractmethod
    def execute(self, notifier: Notifier) -> Dict[str, Any]:
        """核心业务执行逻辑，由各子类具体实现"""
        pass

    def run_cycle(self, notifier: Notifier, forced: bool = False) -> Dict[str, Any]:
        """完整生命周期调用，含前后状态更新与持久化信息维护"""
        now_dt = datetime.now()
        if not forced and not self.should_run(now_dt):
            return {"skipped": True, "reason": "Not due yet"}

        # 记录本次执行时间
        self.last_run = now_dt.strftime("%Y-%m-%d %H:%M:%S")
        result = self.execute(notifier)

        # 周期更新下一次触发时间
        self.compute_next_run(now_dt)
        return result

    def to_dict(self) -> Dict[str, Any]:
        """序列化为 JSON 字典"""
        return {
            "agent_id": self.agent_id,
            "name": self.name,
            "agent_type": self.agent_type,
            "schedule_rule": self.schedule_rule,
            "enabled": self.enabled,
            "created_at": self.created_at,
            "last_run": self.last_run,
            "next_run": self.next_run,
            "config": self.config
        }

    @classmethod
    @abstractmethod
    def from_dict(cls, data: Dict[str, Any]) -> "BaseAgent":
        """反序列化构建实例"""
        pass
