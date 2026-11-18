import os
import sys
import argparse
import json
from datetime import datetime

# 保证控制台支持 UTF-8 输出以避免 GBK 编码异常
if hasattr(sys.stdout, 'reconfigure'):
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
        sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

# 保证父路径可被导入
current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

from core.agent_manager import AgentManager
from agents.curriculum_agent import CurriculumAgent
from agents.custom_task_agent import CustomTaskAgent

def print_separator(title=""):
    width = 75
    if title:
        padding = max(2, (width - len(title.encode('gbk', errors='ignore')) - 4) // 2)
        print("=" * padding + f"  {title}  " + "=" * padding)
    else:
        print("=" * width)

def handle_list(manager: AgentManager, args):
    """展示已注册的所有自动化 Agent 状态"""
    agents = manager.list_agents()
    print_separator("已注册自动化 Agent 列表")
    if not agents:
        print("当前没有任何已注册的 Agent。使用 'python cli.py add-...' 快速添加！")
        return

    for idx, ag in enumerate(agents, 1):
        status_icon = "[启用中]" if ag.enabled else "[已暂停]"
        print(f"\n{idx}. {status_icon} 【{ag.name}】 (ID: {ag.agent_id})")
        print(f"   ├─ 类型: {ag.agent_type}")
        print(f"   ├─ 调度规则: {ag.schedule_rule}")
        print(f"   ├─ 上次执行: {ag.last_run or '尚未执行'}")
        print(f"   ├─ 下次计划: {ag.next_run or '未计算'}")
        
        if ag.agent_type == "curriculum":
            curr_day = ag.config.get("current_day", 1)
            print(f"   ├─ 学习进度: 第 {curr_day} 天 / 共 30 天")
            print(f"   └─ 推送通道: {', '.join(ag.config.get('channels', []))}")
        elif ag.agent_type == "custom_task":
            mode = ag.config.get("task_mode", "reminder")
            print(f"   ├─ 任务模式: {mode}")
            if mode == "command":
                print(f"   ├─ 执行指令: {ag.config.get('command')}")
            else:
                print(f"   ├─ 提醒内容: {ag.config.get('message')}")
            print(f"   └─ 推送通道: {', '.join(ag.config.get('channels', []))}")

    print("\n" + "=" * 75)

def handle_run_now(manager: AgentManager, args):
    """立即触发指定 Agent"""
    agent_id = args.agent_id
    print(f"正在立即触发 Agent: {agent_id} ...")
    res = manager.run_agent_now(agent_id)
    if res.get("success"):
        print(f"[成功] 执行成功！结果详情:")
        for k, v in res.items():
            print(f"   - {k}: {v}")
    else:
        print(f"[失败] 执行失败: {res.get('error', '未知错误')}")

def handle_pause(manager: AgentManager, args):
    if manager.toggle_agent(args.agent_id, enable=False):
        print(f"[已暂停] Agent [{args.agent_id}] 已成功暂停。")
    else:
        print(f"[错误] 未找到 ID 为 [{args.agent_id}] 的 Agent。")

def handle_resume(manager: AgentManager, args):
    if manager.toggle_agent(args.agent_id, enable=True):
        print(f"[已恢复] Agent [{args.agent_id}] 已成功恢复启用。")
    else:
        print(f"[错误] 未找到 ID 为 [{args.agent_id}] 的 Agent。")

def handle_remove(manager: AgentManager, args):
    if manager.remove_agent(args.agent_id):
        print(f"[已删除] Agent [{args.agent_id}] 已成功删除。")
    else:
        print(f"[错误] 未找到 ID 为 [{args.agent_id}] 的 Agent。")

def handle_reset_progress(manager: AgentManager, args):
    ag = manager.get_agent(args.agent_id)
    if not ag or ag.agent_type != "curriculum":
        print(f"[错误] Agent [{args.agent_id}] 不是递进学习类 Agent。")
        return
    day = args.day or 1
    ag.reset_progress(day)
    manager.save()
    print(f"[重置成功] Agent [{args.agent_id}] 进度已重置为: 第 {day} 天。")

def handle_inspect_curriculum(manager: AgentManager, args):
    """在终端直接预览 C++ 知识点库"""
    curriculum_path = os.path.join(current_dir, "data", "cpp_30days_curriculum.json")
    if not os.path.exists(curriculum_path):
        print("未找到知识库文件！")
        return
    with open(curriculum_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    topics = data.get("topics", [])
    target_day = args.day
    if not target_day:
        # 默认取 cpp_daily_master 当前正在学习的天数
        ag = manager.get_agent("cpp_daily_master")
        target_day = ag.config.get("current_day", 1) if ag else 1

    topic = next((t for t in topics if t.get("day") == target_day), None)
    if not topic:
        print(f"未找到第 {target_day} 天的知识点 (有效范围 1 - {len(topics)})。")
        return

    print_separator(f"Day {topic['day']} / {data['total_days']}: {topic['title']}")
    print(f"【阶段】: {topic['stage']}")
    diff_stars = "*" * topic['difficulty']
    print(f"【难度】: {diff_stars} ({topic['difficulty']}/5)")
    print(f"\n【核心概述】:\n{topic['summary']}\n")
    print("【深度解析要点】:")
    for pt in topic.get("key_points", []):
        print(f" - {pt}")
    print("\n【经典代码示例】:")
    print("-" * 50)
    print(topic.get("code_example", ""))
    print("-" * 50)
    print(f"\n【避坑与面试重点】:\n{topic.get('pitfall_or_interview', '')}\n")
    print("=" * 75)

def handle_add_reminder(manager: AgentManager, args):
    """添加自定义提醒 Agent"""
    agent = CustomTaskAgent(
        agent_id=args.id,
        name=args.name,
        schedule_rule=args.schedule,
        enabled=True,
        config={
            "task_mode": "reminder",
            "message": args.message,
            "channels": ["toast", "log"]
        }
    )
    manager.add_agent(agent)
    print(f"[成功] 添加提醒 Agent: [{args.name}] (ID: {args.id}, 调度: {args.schedule})")

def handle_add_command(manager: AgentManager, args):
    """添加自定义脚本/命令执行 Agent"""
    agent = CustomTaskAgent(
        agent_id=args.id,
        name=args.name,
        schedule_rule=args.schedule,
        enabled=True,
        config={
            "task_mode": "command",
            "command": args.command,
            "channels": ["toast", "log"]
        }
    )
    manager.add_agent(agent)
    print(f"[成功] 添加命令执行 Agent: [{args.name}] (ID: {args.id}, 调度: {args.schedule})")

def handle_logs(manager: AgentManager, args):
    """查看近期执行日志"""
    log_file = os.path.join(current_dir, "logs", "agent_activity.log")
    if not os.path.exists(log_file):
        print("暂无日志记录。")
        return
    with open(log_file, "r", encoding="utf-8") as f:
        lines = f.readlines()
    num = args.lines or 20
    recent = lines[-num:]
    print_separator(f"最近 {len(recent)} 条活动日志")
    for l in recent:
        print(l.strip())
    print("=" * 75)

def main():
    parser = argparse.ArgumentParser(description="自动化 Agent 管理与调度控制中心")
    subparsers = parser.add_subparsers(dest="subcommand", help="操作指令")

    # list
    subparsers.add_parser("list", help="查看所有已注册的自动化 Agent 状态")

    # run-now
    p_run = subparsers.add_parser("run-now", help="立即执行指定 Agent (测试/手动触发)")
    p_run.add_argument("agent_id", help="Agent 唯一标识符")

    # pause & resume
    p_pause = subparsers.add_parser("pause", help="暂停指定 Agent")
    p_pause.add_argument("agent_id", help="Agent 唯一标识符")
    p_resume = subparsers.add_parser("resume", help="恢复指定 Agent")
    p_resume.add_argument("agent_id", help="Agent 唯一标识符")

    # remove
    p_rm = subparsers.add_parser("remove", help="删除指定 Agent")
    p_rm.add_argument("agent_id", help="Agent 唯一标识符")

    # reset-progress
    p_reset = subparsers.add_parser("reset-progress", help="重置学习递推 Agent 的当前天数")
    p_reset.add_argument("agent_id", help="Agent ID")
    p_reset.add_argument("--day", type=int, default=1, help="目标天数 (默认 1)")

    # inspect-curriculum
    p_insp = subparsers.add_parser("inspect-curriculum", help="在终端查看 C++ 知识点详情")
    p_insp.add_argument("--day", type=int, default=None, help="查看第 N 天 (默认当前天数)")

    # add-reminder
    p_rem = subparsers.add_parser("add-reminder", help="添加一个自定义定时提醒 Agent")
    p_rem.add_argument("--id", required=True, help="Agent 唯一 ID")
    p_rem.add_argument("--name", required=True, help="Agent 名称")
    p_rem.add_argument("--schedule", required=True, help="调度规则 (例如 '15:30', 'interval@1h')")
    p_rem.add_argument("--message", required=True, help="提醒内容")

    # add-command
    p_cmd = subparsers.add_parser("add-command", help="添加一个自定义命令执行 Agent")
    p_cmd.add_argument("--id", required=True, help="Agent 唯一 ID")
    p_cmd.add_argument("--name", required=True, help="Agent 名称")
    p_cmd.add_argument("--schedule", required=True, help="调度规则 (例如 '09:00', 'interval@30m')")
    p_cmd.add_argument("--cmd", "--command", dest="command", required=True, help="执行的系统命令或脚本")

    # logs
    p_log = subparsers.add_parser("logs", help="查看最近活动日志")
    p_log.add_argument("-n", "--lines", type=int, default=20, help="行数")

    args = parser.parse_args()
    if not args.subcommand:
        parser.print_help()
        return

    manager = AgentManager(current_dir)

    handlers = {
        "list": handle_list,
        "run-now": handle_run_now,
        "pause": handle_pause,
        "resume": handle_resume,
        "remove": handle_remove,
        "reset-progress": handle_reset_progress,
        "inspect-curriculum": handle_inspect_curriculum,
        "add-reminder": handle_add_reminder,
        "add-command": handle_add_command,
        "logs": handle_logs
    }

    handler = handlers.get(args.subcommand)
    if handler:
        handler(manager, args)

if __name__ == "__main__":
    main()
