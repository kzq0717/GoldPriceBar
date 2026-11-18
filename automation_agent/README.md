# 🤖 动态多任务自动化 Agent 系统 (Automation Agent Engine)

> 支持**自定义自动化内容**、**定点推送（每天上午 10:00）30 天 C++ 阶梯式核心知识点**，并支持**动态无缝扩展多个自动化 Agent**。

---

## 🌟 系统核心特性

1. **定点递进推送（30 天 C++ 核心内功路线）**：
   - 严格规划 **从易到难、为期 1 个月（30天）** 的 C++ 核心知识点大纲。
   - 每日上午 10:00（或自定义时刻）自动触发。
   - **双轨交付**：系统右下角弹出 **Windows 10/11 原生 Toast 弹窗通知**，同时自动在 `learning_notes/` 目录生成排版高清的 Markdown 学习档案（含深度原理、典型代码、高频避坑与面试真题考点）。
   - **状态自持久化**：中断或重启不丢失进度，自动记录当前学习天数与下一次执行计划。

2. **支持自定义自动化内容**：
   - **递进学习类 (curriculum)**：支持接入任意主题题库或课程体系（如算法、Python、设计模式等）。
   - **定时提醒类 (reminder)**：工作站会、护眼喝水、日程备忘。
   - **系统/脚本执行类 (command)**：定时执行本地 Shell / Python / Bat 脚本，如 `git status`、数据库备份、行情爬虫等，并将输出即时汇报。
   - **接口健康监控类 (api_monitor)**：定时探测本地或远程 API。

3. **动态管理多个自动化 Agent**：
   - 提供完备的命令行控制台 (`cli.py`)，支持一键 `add-reminder`、`add-command`、`run-now`、`pause`、`resume`、`remove`。
   - 支持热加载与配置持久化 (`config.json` 与 `state.json`)。

4. **多通道通知分发**：
   - **Windows 原生 Toast 通知**（原生 WinRT API，零第三方环境依赖）。
   - **本地 Markdown 学习知识库归档**。
   - **主流 Webhook 自动适配**（飞书、企业微信、钉钉、通用 Webhook）。

---

## 📁 目录结构

```text
automation_agent/
├── cli.py                          # 交互式管理 CLI 命令行工具
├── main.py                         # 常驻后台主调度服务
├── config.json                     # Agent 声明配置文件
├── state.json                      # 各 Agent 动态执行状态与进度指针
├── start_agent.bat                 # 前台控制台启动脚本
├── start_agent_silent.vbs          # 静默无窗口后台常驻启动脚本
├── stop_agent.bat                  # 一键停止后台 Agent 服务
├── core/
│   ├── scheduler.py                # 定时调度解析器 (定点/间隔/Cron)
│   ├── notifier.py                 # 多通道通知分发中心
│   ├── agent_manager.py            # Agent 注册中心与状态机
│   └── toast.ps1                   # Windows 原生 Toast 弹窗脚本
├── agents/
│   ├── base_agent.py               # 抽象 Agent 基类
│   ├── curriculum_agent.py         # 30天递进学习推送 Agent
│   └── custom_task_agent.py        # 自定义命令/提醒/API Agent
├── data/
│   └── cpp_30days_curriculum.json  # 精心编写的 30 天 C++ 核心知识库 (从易到难)
├── learning_notes/                 # 自动生成的每日高清 Markdown 笔记
└── logs/                           # 服务运行日志与 Agent 触发历史
```

---

## 🗺️ 30 天 C++ 核心知识点全景大纲 (从易到难)

| 阶段 | 涵盖天数 | 阶段主题 | 核心代表知识点 |
| :--- | :--- | :--- | :--- |
| **第一阶段** | Day 01 - Day 07 | 底层内存与指针引用基石 | 栈堆虚拟内存模型、指针 vs 引用底层、const 与 constexpr、内存对齐填充、RAII 哲学、四种 cast 转型、inline 与 ODR |
| **第二阶段** | Day 08 - Day 14 | 面向对象本质与资源管理 | 深浅拷贝与三法则、虚函数与 vtable 对象模型、虚析构与纯抽象接口、菱形虚继承内存偏移、运算符重载、异常安全保证、`std::unique_ptr` 零开销独占语义 |
| **第三阶段** | Day 15 - Day 21 | 现代 C++ 核心：移动语义与智能指针 | 左值/纯右值/将亡值 (Value Categories)、移动语义与 `std::move` 源码剖析、万能引用与 `std::forward` 完美转发、`std::shared_ptr` 控制块双计数器、`std::weak_ptr` 破除循环引用、Lambda 闭包与 `std::function` 类型擦除、C++17 工具三剑客 (`optional`/`variant`/`any`) |
| **第四阶段** | Day 22 - Day 26 | 泛型编程与 STL 底层机制 | 模板两阶段编译与偏特化、SFINAE 与 `std::enable_if` 重载决议、`std::vector` 几何倍增与迭代器失效、哈希表与红黑树 (`unordered_map` vs `map`)、短字符串优化 (SSO) 与 `string_view` |
| **第五阶段** | Day 27 - Day 30 | 并发编程、C++20 新特性与极致性能调优 | `std::thread`、RAII 锁与死锁规避、无锁编程与原子操作六大内存序 (Memory Order)、C++20 Concepts 概念约束与 Ranges 管道流、CPU Cache 缓存行亲和性与伪共享 (False Sharing) 消减 |

---

## 🚀 快速开始与使用教程

### 1. 立即测试触发 C++ 今日知识点 (验证效果)
```powershell
python automation_agent\cli.py run-now cpp_daily_master
```
> 执行后，Windows 屏幕右下角将弹出知识点 Toast 弹窗，并在 `automation_agent\learning_notes\` 目录下生成当天的 Markdown 笔记文件。

### 2. 查看已注册的全部 Agent 状态
```powershell
python automation_agent\cli.py list
```

### 3. 在终端即时查阅任意天数的知识点
```powershell
# 查看今天该学的知识点
python automation_agent\cli.py inspect-curriculum

# 指定查看第 15 天 (现代 C++ 移动语义与值类别体系)
python automation_agent\cli.py inspect-curriculum --day 15
```

### 4. 重置或跳转学习天数
```powershell
# 将进度调回第 1 天
python automation_agent\cli.py reset-progress cpp_daily_master --day 1
```

---

## ➕ 动态添加更多自定义 Agent

系统原生支持在运行时动态注册新的 Agent：

### 示例 A：添加一个定时提醒 Agent（如健康喝水）
```powershell
python automation_agent\cli.py add-reminder --id drink_water --name "健康喝水提醒" --schedule "interval@1h" --message "工作忙碌之余，记得喝一杯温水，活动活动肩颈！"
```

### 示例 B：添加一个定时命令/脚本监控 Agent（如每日下班前代码提交检查）
```powershell
python automation_agent\cli.py add-command --id git_check --name "下班前 Git 提交检查" --schedule "18:00" --cmd "git status --short"
```

### 调度规则说明：
- **每日定点**：`"10:00"` 或 `"daily@10:00"`、`"18:30"`
- **时间间隔**：`"interval@30m"`（每30分钟）、`"interval@1h"`（每小时）、`"every_10s"`（每10秒）
- **标准 Cron**：`"0 10 * * *"`（每天10:00）

---

## ⚙️ 后台常驻运行与开机启动

### 方式 1：控制台模式运行
双击运行 `automation_agent\start_agent.bat`，或在命令行中运行：
```powershell
python automation_agent\main.py
```

### 方式 2：静默无黑框后台常驻运行
双击运行 `automation_agent\start_agent_silent.vbs`：
- 进程将在后台隐蔽执行，没有任何黑色 CMD 弹窗干扰。
- 若需停止，双击运行 `automation_agent\stop_agent.bat` 即可。

### 方式 3：Windows 开机自启
按 `Win + R` 输入 `shell:startup` 打开启动文件夹，将 `start_agent_silent.vbs` 的快捷方式放入该文件夹即可实现开机自动就绪！
