#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

echo "推送到 https://github.com/kzq0717/GoldPriceBar"
git remote remove origin 2>/dev/null || true
git remote add origin https://github.com/kzq0717/GoldPriceBar.git
git branch -M main

if [[ -n "${GH_TOKEN:-}" ]]; then
  git push -u "https://${GH_TOKEN}@github.com/kzq0717/GoldPriceBar.git" main
else
  echo "未设置 GH_TOKEN，将交互输入凭据（密码处粘贴 PAT）"
  git push -u origin main
fi

echo "完成。打 tag 发布： git tag v0.1.0 && git push origin v0.1.0"
