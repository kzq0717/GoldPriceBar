' VBScript: 静默无黑框后台启动自动化 Agent 引擎
Set WshShell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
currentDir = fso.GetParentFolderName(WScript.ScriptFullName)

cmd = "python """ & currentDir & "\main.py"""
' 0 代表隐藏窗口运行，False 代表不阻塞继续执行
WshShell.Run cmd, 0, False
