import os
import subprocess
import urllib.request
import json
from datetime import datetime
from typing import Dict, Any, Optional
from agents.base_agent import BaseAgent
from core.notifier import Notifier

class CustomTaskAgent(BaseAgent):
    """支持用户自定义自动化内容的通用 Agent：
    支持以下模式：
    1. reminder: 文本提醒（例如每日站会、喝水、下班检查）
    2. command: 运行本地 Shell/Python/Bat 脚本并收集输出
    3. api_monitor: 定时请求外部/本地 API 并进行健康检查或状态推送
    """

    def __init__(
        self,
        agent_id: str,
        name: str,
        schedule_rule: str,
        enabled: bool = True,
        created_at: Optional[str] = None,
        last_run: Optional[str] = None,
        next_run: Optional[str] = None,
        config: Optional[Dict[str, Any]] = None
    ):
        super().__init__(
            agent_id=agent_id,
            name=name,
            agent_type="custom_task",
            schedule_rule=schedule_rule,
            enabled=enabled,
            created_at=created_at,
            last_run=last_run,
            next_run=next_run,
            config=config or {}
        )
        self.task_mode = self.config.get("task_mode", "reminder")
        self.message = self.config.get("message", "提醒：自动化任务执行完毕")
        self.command = self.config.get("command", "")
        self.api_url = self.config.get("api_url", "")
        self.channels = self.config.get("channels", ["toast", "log"])
        self.webhook_url = self.config.get("webhook_url", "")

    def execute(self, notifier: Notifier) -> Dict[str, Any]:
        """按指定模式执行自定义自动化内容"""
        out_title = f"[{self.name}] 自动化执行"
        out_msg = ""
        success = True

        try:
            if self.task_mode == "reminder":
                out_msg = self.message

            elif self.task_mode == "command":
                if not self.command:
                    out_msg = "未配置执行命令"
                    success = False
                else:
                    # 在子进程中执行用户自定义命令
                    res = subprocess.run(
                        self.command,
                        shell=True,
                        capture_output=True,
                        text=True,
                        encoding="utf-8",
                        errors="replace",
                        timeout=60
                    )
                    stdout = res.stdout.strip()
                    stderr = res.stderr.strip()
                    if res.returncode == 0:
                        out_msg = f"命令执行成功：\n{stdout[:150]}" if stdout else "命令执行成功 (无输出)"
                    else:
                        out_msg = f"命令执行失败 (code {res.returncode})：\n{stderr[:150]}"
                        success = False

            elif self.task_mode == "api_monitor":
                if not self.api_url:
                    out_msg = "未配置 API URL"
                    success = False
                else:
                    req = urllib.request.Request(
                        self.api_url,
                        headers={"User-Agent": "AutomationAgent/1.0"}
                    )
                    with urllib.request.urlopen(req, timeout=15) as resp:
                        body = resp.read().decode("utf-8", errors="replace")
                        out_msg = f"API 状态码: {resp.status}\n响应摘要: {body[:100]}..."
                        success = (resp.status == 200)

            # 分发通知
            if "toast" in self.channels:
                notifier.send_toast(out_title, out_msg[:200])

            if self.webhook_url:
                notifier.send_webhook(self.webhook_url, out_title, out_msg)

            notifier.log_activity(self.agent_id, f"执行完成 ({'成功' if success else '失败'}): {out_msg[:100]}", "INFO" if success else "WARN")

            return {
                "success": success,
                "mode": self.task_mode,
                "title": out_title,
                "message": out_msg
            }
        except Exception as e:
            err_msg = f"任务执行异常: {str(e)}"
            notifier.log_activity(self.agent_id, err_msg, "ERROR")
            if "toast" in self.channels:
                notifier.send_toast(f"[{self.name}] 错误", err_msg)
            return {"success": False, "error": err_msg}

    def to_dict(self) -> Dict[str, Any]:
        d = super().to_dict()
        d["config"]["task_mode"] = self.task_mode
        d["config"]["message"] = self.message
        d["config"]["command"] = self.command
        d["config"]["api_url"] = self.api_url
        d["config"]["channels"] = self.channels
        d["config"]["webhook_url"] = self.webhook_url
        return d

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "CustomTaskAgent":
        return cls(
            agent_id=data.get("agent_id", "custom_agent"),
            name=data.get("name", "自定义任务"),
            schedule_rule=data.get("schedule_rule", "10:00"),
            enabled=data.get("enabled", True),
            created_at=data.get("created_at"),
            last_run=data.get("last_run"),
            next_run=data.get("next_run"),
            config=data.get("config", {})
        )
