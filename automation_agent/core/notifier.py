import os
import sys
import subprocess
import json
import logging
from datetime import datetime
from typing import Optional, Dict, Any
import urllib.request
import urllib.error

logger = logging.getLogger("AutomationAgent.Notifier")

class Notifier:
    """多通道通知管理器：支持 Windows Toast、Markdown 学习归档、Webhook 及控制台/日志输出"""

    def __init__(self, base_dir: str):
        self.base_dir = base_dir
        self.toast_script = os.path.join(base_dir, "core", "toast.ps1")
        self.logs_dir = os.path.join(base_dir, "logs")
        self.notes_dir = os.path.join(base_dir, "learning_notes")
        os.makedirs(self.logs_dir, exist_ok=True)
        os.makedirs(self.notes_dir, exist_ok=True)

    def send_toast(self, title: str, message: str, app_id: str = "AutomationAgent") -> bool:
        """发送 Windows 10/11 原生 Toast 弹窗通知"""
        if not os.path.exists(self.toast_script):
            logger.warning(f"Toast script not found at {self.toast_script}")
            return False

        try:
            cmd = [
                "powershell",
                "-NoProfile",
                "-ExecutionPolicy", "Bypass",
                "-File", self.toast_script,
                "-Title", title,
                "-Message", message,
                "-AppId", app_id
            ]
            res = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=10
            )
            if res.returncode == 0 and "SUCCESS" in res.stdout:
                logger.info(f"Toast delivered: {title}")
                return True
            else:
                logger.warning(f"Toast notification failed: {res.stderr.strip() or res.stdout.strip()}")
                return False
        except Exception as e:
            logger.error(f"Error executing toast notification: {e}")
            return False

    def save_markdown_note(self, filename: str, content: str) -> str:
        """保存结构化 Markdown 笔记（如每日 C++ 知识点推送）"""
        filepath = os.path.join(self.notes_dir, filename)
        try:
            with open(filepath, "w", encoding="utf-8") as f:
                f.write(content)
            logger.info(f"Saved Markdown note to: {filepath}")
            return filepath
        except Exception as e:
            logger.error(f"Failed to save Markdown note: {e}")
            return ""

    def send_webhook(self, webhook_url: str, title: str, content: str, msg_type: str = "auto") -> bool:
        """向企业微信、飞书、钉钉或自定义 Webhook 发送通知"""
        if not webhook_url:
            return False

        payload: Dict[str, Any] = {}
        # 针对飞书、钉钉、企微做自适应格式适配
        if "feishu" in webhook_url or "lark" in webhook_url:
            payload = {
                "msg_type": "post",
                "content": {
                    "post": {
                        "zh_cn": {
                            "title": title,
                            "content": [[{"tag": "text", "text": content}]]
                        }
                    }
                }
            }
        elif "dingtalk" in webhook_url:
            payload = {
                "msgtype": "markdown",
                "markdown": {
                    "title": title,
                    "text": f"### {title}\n\n{content}"
                }
            }
        elif "weixin" in webhook_url or "qyapi" in webhook_url:
            payload = {
                "msgtype": "markdown",
                "markdown": {
                    "content": f"### {title}\n{content}"
                }
            }
        else:
            # 通用 JSON Webhook
            payload = {
                "title": title,
                "content": content,
                "timestamp": datetime.now().isoformat()
            }

        try:
            data = json.dumps(payload).encode("utf-8")
            req = urllib.request.Request(
                webhook_url,
                data=data,
                headers={"Content-Type": "application/json; charset=utf-8"}
            )
            with urllib.request.urlopen(req, timeout=10) as resp:
                logger.info(f"Webhook response status: {resp.status}")
                return resp.status in (200, 204)
        except Exception as e:
            logger.error(f"Failed to post to webhook {webhook_url}: {e}")
            return False

    def log_activity(self, agent_id: str, message: str, level: str = "INFO"):
        """统一记录 Agent 活动日志文件"""
        log_file = os.path.join(self.logs_dir, "agent_activity.log")
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        entry = f"[{timestamp}] [{level}] [{agent_id}] {message}\n"
        try:
            with open(log_file, "a", encoding="utf-8") as f:
                f.write(entry)
        except Exception as e:
            logger.error(f"Failed to write activity log: {e}")
