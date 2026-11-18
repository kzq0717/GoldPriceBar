import os
import json
import re
from datetime import datetime
from typing import Dict, Any, Optional
from agents.base_agent import BaseAgent
from core.notifier import Notifier

class CurriculumAgent(BaseAgent):
    """知识体系阶梯递进推送 Agent：
    支持按天推进课程/知识体系（如 30 天 C++ 核心知识），自动记录当前学习天数，
    每日定点推送 Windows Toast 弹窗并自动在本地生成精排版 Markdown 笔记。
    """

    def __init__(
        self,
        agent_id: str,
        name: str,
        schedule_rule: str = "10:00",
        enabled: bool = True,
        created_at: Optional[str] = None,
        last_run: Optional[str] = None,
        next_run: Optional[str] = None,
        config: Optional[Dict[str, Any]] = None
    ):
        super().__init__(
            agent_id=agent_id,
            name=name,
            agent_type="curriculum",
            schedule_rule=schedule_rule,
            enabled=enabled,
            created_at=created_at,
            last_run=last_run,
            next_run=next_run,
            config=config or {}
        )
        self.curriculum_file = self.config.get("curriculum_file", "data/cpp_30days_curriculum.json")
        self.current_day = self.config.get("current_day", 1)
        self.repeat_when_finished = self.config.get("repeat_when_finished", False)
        self.channels = self.config.get("channels", ["toast", "markdown", "log"])
        self.webhook_url = self.config.get("webhook_url", "")

    def _load_curriculum(self, base_dir: str) -> Optional[Dict[str, Any]]:
        """安全加载知识库 JSON 文件"""
        target_path = self.curriculum_file
        if not os.path.isabs(target_path):
            target_path = os.path.join(base_dir, target_path)

        if not os.path.exists(target_path):
            return None

        try:
            with open(target_path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return None

    def execute(self, notifier: Notifier) -> Dict[str, Any]:
        """执行单次推送并推进进度"""
        curriculum = self._load_curriculum(notifier.base_dir)
        if not curriculum:
            msg = f"找不到知识库文件: {self.curriculum_file}"
            notifier.log_activity(self.agent_id, msg, "ERROR")
            return {"success": False, "error": msg}

        topics = curriculum.get("topics", [])
        total_days = curriculum.get("total_days", len(topics))

        # 检查是否已完成全部推送
        if self.current_day > total_days:
            congrats_title = "🎉 C++ 核心知识点打卡全部完成！"
            congrats_msg = f"恭喜！您已成功推算完成全部 {total_days} 天的 C++ 核心知识体系通关！"
            if "toast" in self.channels:
                notifier.send_toast(congrats_title, congrats_msg)
            if "markdown" in self.channels:
                notifier.save_markdown_note(
                    "CONGRATULATIONS_COMPLETED.md",
                    f"# 🎉 恭喜毕业！\n\n您已完成 `{curriculum.get('course_name')}` 全部 {total_days} 天知识点学习。\n完成时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n"
                )
            notifier.log_activity(self.agent_id, congrats_msg, "INFO")

            if self.repeat_when_finished:
                self.current_day = 1
                self.config["current_day"] = 1
            else:
                self.enabled = False
            return {"success": True, "completed": True, "message": congrats_msg}

        # 获取当天知识点
        topic = None
        for item in topics:
            if item.get("day") == self.current_day:
                topic = item
                break

        if not topic:
            # 索引容错
            topic_idx = self.current_day - 1
            if 0 <= topic_idx < len(topics):
                topic = topics[topic_idx]

        if not topic:
            err = f"未找到 Day {self.current_day} 的对应知识点"
            notifier.log_activity(self.agent_id, err, "ERROR")
            return {"success": False, "error": err}

        # 格式化知识点内容
        day_num = topic.get("day", self.current_day)
        title = topic.get("title", "未命名知识点")
        difficulty = topic.get("difficulty", 1)
        stars = "⭐" * difficulty
        stage = topic.get("stage", "核心进阶")
        summary = topic.get("summary", "")
        key_points = topic.get("key_points", [])
        code_example = topic.get("code_example", "")
        pitfall = topic.get("pitfall_or_interview", "")

        # 1. 弹出 Windows Toast 提醒
        toast_title = f"[C++ 核心 | Day {day_num:02d}/30] {title}"
        toast_body = f"难度: {stars}\n{summary}\n(详细代码与要点已生成至本地学习笔记)"
        if "toast" in self.channels:
            notifier.send_toast(toast_title, toast_body)

        # 2. 生成本地高清排版 Markdown 笔记
        safe_title = re.sub(r'[\\/*?:"<>| ]', "_", title)
        filename = f"Day_{day_num:02d}_{safe_title}.md"
        
        md_points = "\n".join([f"- **{pt}**" for pt in key_points])
        md_content = f"""# Day {day_num:02d}: {title}

- **所属阶段**：{stage}
- **难度等级**：{stars} ({difficulty}/5)
- **推送时间**：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
- **学习进度**：Day {day_num} / 共 {total_days} 天

---

## 📌 核心概述
> {summary}

---

## 💡 深度技术要点
{md_points}

---

## 💻 经典精辟代码示例
```cpp
{code_example}
```

---

## ⚠️ 高频踩坑与大厂面试考点
> {pitfall}

---
*💡 由自动化学习 Agent 定时推送 · 掌握 C++ 底层内功*
"""
        note_path = ""
        if "markdown" in self.channels:
            note_path = notifier.save_markdown_note(filename, md_content)

        # 3. Webhook 外部推送 (如有)
        if self.webhook_url:
            webhook_text = f"【Day {day_num}/30 C++核心知识点】\n标题：{title}\n难度：{stars}\n\n要点：\n{summary}\n\n典型代码：\n{code_example}\n\n避坑/面试点：\n{pitfall}"
            notifier.send_webhook(self.webhook_url, toast_title, webhook_text)

        # 4. 记录日志
        log_msg = f"推送成功: Day {day_num} [{title}], 笔记已归档到 {filename}"
        notifier.log_activity(self.agent_id, log_msg, "INFO")

        # 推进到下一天并保存状态
        old_day = self.current_day
        self.current_day += 1
        self.config["current_day"] = self.current_day

        return {
            "success": True,
            "day": old_day,
            "title": title,
            "next_day": self.current_day,
            "note_path": note_path
        }

    def reset_progress(self, target_day: int = 1):
        """重置学习进度到指定天数"""
        self.current_day = max(1, target_day)
        self.config["current_day"] = self.current_day
        self.enabled = True

    def to_dict(self) -> Dict[str, Any]:
        d = super().to_dict()
        d["config"]["curriculum_file"] = self.curriculum_file
        d["config"]["current_day"] = self.current_day
        d["config"]["repeat_when_finished"] = self.repeat_when_finished
        d["config"]["channels"] = self.channels
        d["config"]["webhook_url"] = self.webhook_url
        return d

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "CurriculumAgent":
        return cls(
            agent_id=data.get("agent_id", "curriculum_agent"),
            name=data.get("name", "知识点推送助手"),
            schedule_rule=data.get("schedule_rule", "10:00"),
            enabled=data.get("enabled", True),
            created_at=data.get("created_at"),
            last_run=data.get("last_run"),
            next_run=data.get("next_run"),
            config=data.get("config", {})
        )
