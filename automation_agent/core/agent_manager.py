import os
import json
import logging
from typing import Dict, List, Optional, Any
from agents.base_agent import BaseAgent
from agents.curriculum_agent import CurriculumAgent
from agents.custom_task_agent import CustomTaskAgent
from core.notifier import Notifier

logger = logging.getLogger("AutomationAgent.AgentManager")

AGENT_TYPE_REGISTRY = {
    "curriculum": CurriculumAgent,
    "custom_task": CustomTaskAgent
}

class AgentManager:
    """自动化 Agent 注册与生命周期管理器：
    负责 Agent 的动态增删查改、配置持久化、执行调度分发与状态同步。
    """

    def __init__(self, base_dir: str):
        self.base_dir = base_dir
        self.config_path = os.path.join(base_dir, "config.json")
        self.state_path = os.path.join(base_dir, "state.json")
        self.notifier = Notifier(base_dir)
        self.agents: Dict[str, BaseAgent] = {}
        self.load()

    def _get_agent_class(self, agent_type: str):
        return AGENT_TYPE_REGISTRY.get(agent_type, CustomTaskAgent)

    def load(self):
        """从 config.json 与 state.json 加载并恢复所有 Agent"""
        if not os.path.exists(self.config_path):
            self._create_default_config()

        try:
            with open(self.config_path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except Exception as e:
            logger.error(f"Error loading config.json: {e}")
            data = {"agents": []}

        # 加载持久化运行状态
        state_data = {}
        if os.path.exists(self.state_path):
            try:
                with open(self.state_path, "r", encoding="utf-8") as f:
                    state_data = json.load(f)
            except Exception as e:
                logger.error(f"Error loading state.json: {e}")

        self.agents.clear()
        for item in data.get("agents", []):
            agent_id = item.get("agent_id")
            if not agent_id:
                continue

            # 合并 state 中的动态字段
            if agent_id in state_data:
                saved_state = state_data[agent_id]
                item["last_run"] = saved_state.get("last_run", item.get("last_run"))
                item["next_run"] = saved_state.get("next_run", item.get("next_run"))
                if "config" in saved_state and "config" in item:
                    item["config"].update(saved_state["config"])

            agent_type = item.get("agent_type", "custom_task")
            cls = self._get_agent_class(agent_type)
            agent_instance = cls.from_dict(item)
            self.agents[agent_id] = agent_instance

        logger.info(f"Loaded {len(self.agents)} agents successfully.")

    def save(self):
        """保存配置及各 Agent 的最新运行状态"""
        # 保存全局配置
        agents_data = [agent.to_dict() for agent in self.agents.values()]
        payload = {"version": "1.0", "agents": agents_data}
        try:
            with open(self.config_path, "w", encoding="utf-8") as f:
                json.dump(payload, f, ensure_ascii=False, indent=2)
        except Exception as e:
            logger.error(f"Failed to write config.json: {e}")

        # 保存动态状态
        state_payload = {}
        for aid, agent in self.agents.items():
            state_payload[aid] = {
                "last_run": agent.last_run,
                "next_run": agent.next_run,
                "config": agent.config
            }
        try:
            with open(self.state_path, "w", encoding="utf-8") as f:
                json.dump(state_payload, f, ensure_ascii=False, indent=2)
        except Exception as e:
            logger.error(f"Failed to write state.json: {e}")

    def _create_default_config(self):
        """默认内置定点每天10点推送 C++ 核心知识点的自动化 Agent"""
        default_payload = {
            "version": "1.0",
            "agents": [
                {
                    "agent_id": "cpp_daily_master",
                    "name": "C++ 30天核心知识阶梯推送",
                    "agent_type": "curriculum",
                    "schedule_rule": "10:00",
                    "enabled": True,
                    "config": {
                        "curriculum_file": "data/cpp_30days_curriculum.json",
                        "current_day": 1,
                        "repeat_when_finished": False,
                        "channels": ["toast", "markdown", "log"]
                    }
                }
            ]
        }
        with open(self.config_path, "w", encoding="utf-8") as f:
            json.dump(default_payload, f, ensure_ascii=False, indent=2)

    def add_agent(self, agent: BaseAgent) -> bool:
        """动态注册并持久化一个新 Agent"""
        self.agents[agent.agent_id] = agent
        self.save()
        self.notifier.log_activity("SYSTEM", f"新增自动化 Agent: [{agent.name}] (ID: {agent.agent_id}, 调度: {agent.schedule_rule})")
        return True

    def remove_agent(self, agent_id: str) -> bool:
        """根据 ID 移除指定 Agent"""
        if agent_id in self.agents:
            name = self.agents[agent_id].name
            del self.agents[agent_id]
            self.save()
            self.notifier.log_activity("SYSTEM", f"已移除 Agent: [{name}] (ID: {agent_id})")
            return True
        return False

    def toggle_agent(self, agent_id: str, enable: Optional[bool] = None) -> bool:
        """启用或暂停指定 Agent"""
        if agent_id in self.agents:
            target = not self.agents[agent_id].enabled if enable is None else enable
            self.agents[agent_id].enabled = target
            self.save()
            status_text = "已启用" if target else "已暂停"
            self.notifier.log_activity(agent_id, f"Agent 状态更新为: {status_text}")
            return True
        return False

    def get_agent(self, agent_id: str) -> Optional[BaseAgent]:
        return self.agents.get(agent_id)

    def list_agents(self) -> List[BaseAgent]:
        return list(self.agents.values())

    def run_agent_now(self, agent_id: str) -> Dict[str, Any]:
        """强制立即触发指定 Agent 执行"""
        agent = self.get_agent(agent_id)
        if not agent:
            return {"success": False, "error": f"Agent ID {agent_id} not found"}
        res = agent.run_cycle(self.notifier, forced=True)
        self.save()
        return res

    def tick(self) -> List[Dict[str, Any]]:
        """调度心跳：遍历检查所有 Agent 是否到期并执行"""
        results = []
        state_changed = False
        for agent in self.agents.values():
            if agent.should_run():
                logger.info(f"Triggering due agent: {agent.name} ({agent.agent_id})")
                res = agent.run_cycle(self.notifier, forced=False)
                results.append({"agent_id": agent.agent_id, "result": res})
                state_changed = True

        if state_changed:
            self.save()
        return results
