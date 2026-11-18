import os
import sys
import time
import signal
import logging
from datetime import datetime

# 保证 Windows 控制台 UTF-8 输出
if hasattr(sys.stdout, 'reconfigure'):
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
        sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

from core.agent_manager import AgentManager

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] [%(name)s] %(message)s",
    handlers=[
        logging.StreamHandler(sys.stdout),
        logging.FileHandler(os.path.join(current_dir, "logs", "service.log"), encoding="utf-8")
    ]
)
logger = logging.getLogger("AutomationDaemon")

running = True

def handle_exit(signum, frame):
    global running
    logger.info("Received termination signal. Gracefully stopping daemon...")
    running = False

def run_daemon(poll_interval: int = 5):
    """常驻后台调度循环"""
    manager = AgentManager(current_dir)
    logger.info("=" * 60)
    logger.info("🤖 自动化 Agent 调度引擎已启动")
    logger.info(f"📁 工作目录: {current_dir}")
    logger.info(f"⏰ 轮询检查周期: {poll_interval} 秒")
    logger.info(f"📊 已就绪 Agent 数量: {len(manager.list_agents())}")
    for ag in manager.list_agents():
        logger.info(f"   - [{ag.name}] ID: {ag.agent_id} | 调度: {ag.schedule_rule} | 下次: {ag.next_run}")
    logger.info("=" * 60)

    signal.signal(signal.SIGINT, handle_exit)
    signal.signal(signal.SIGTERM, handle_exit)

    while running:
        try:
            results = manager.tick()
            if results:
                logger.info(f"Tick cycle completed, executed {len(results)} task(s).")
        except Exception as e:
            logger.error(f"Error in scheduler tick: {e}", exc_info=True)

        # 挂起 poll_interval 秒
        for _ in range(poll_interval * 2):
            if not running:
                break
            time.sleep(0.5)

    manager.save()
    logger.info("Automation Daemon stopped cleanly. State saved.")

def main():
    import argparse
    parser = argparse.ArgumentParser(description="自动化 Agent 主调度服务")
    parser.add_argument("--interval", type=int, default=5, help="心跳检查轮询间隔 (秒)")
    parser.add_argument("--test", action="store_true", help="单次测试：立即触发一次 C++ 知识点推送验证")
    parser.add_argument("--once", action="store_true", help="单次检查：仅执行当前已到期的任务后立即退出")
    args = parser.parse_args()

    if args.test:
        manager = AgentManager(current_dir)
        print("🚀 执行全链路测试: 立即推送今日 C++ 核心知识点...")
        res = manager.run_agent_now("cpp_daily_master")
        print(f"测试执行完毕: {res}")
        return

    if args.once:
        manager = AgentManager(current_dir)
        res = manager.tick()
        print(f"单次检查完成，共触发 {len(res)} 项任务。")
        return

    run_daemon(poll_interval=args.interval)

if __name__ == "__main__":
    main()
