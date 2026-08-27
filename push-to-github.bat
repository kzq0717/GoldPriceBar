@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo ========================================
echo  推送到 https://github.com/kzq0717/GoldPriceBar
echo ========================================
echo.
echo 请先在 GitHub 创建 Personal Access Token (classic)
echo 权限勾选 repo，然后设置环境变量：
echo   set GH_TOKEN=你的token
echo 或在下面提示时输入用户名与 token（密码处粘贴 token）
echo.

git remote remove origin 2>nul
git remote add origin https://github.com/kzq0717/GoldPriceBar.git
git branch -M main

if defined GH_TOKEN (
  git push -u https://%GH_TOKEN%@github.com/kzq0717/GoldPriceBar.git main
) else (
  git push -u origin main
)

if errorlevel 1 (
  echo.
  echo 推送失败。请确认：
  echo  1. 仓库 https://github.com/kzq0717/GoldPriceBar 已创建且为空
  echo  2. 使用有权限的账号 / Token
  exit /b 1
)

echo.
echo 推送成功。可打 tag 触发 Release 打包：
echo   git tag v0.1.0
echo   git push origin v0.1.0
exit /b 0
