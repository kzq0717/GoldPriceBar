# GitHub Actions 自动构建与 Release

## 已启用的工作流

文件：`.github/workflows/release.yml`

| 触发条件 | 行为 |
|----------|------|
| `push` 到 `main` | Windows 编译 + 上传 Artifacts（不发 Release） |
| `push` tag `v*`（如 `v0.4.1`） | 编译完成后**自动创建 GitHub Release** 并挂上 zip |
| 手动 | Actions 页 → Build and Package → Run workflow |

产物：

- `GoldPriceBarLite-<version>-src.zip` 源码
- `GoldPriceBarLite-<version>-win64.zip` Windows 可执行包（含 windeployqt 依赖）

## 你需要配置什么？

**一般不需要额外 Secret。**

- `permissions: contents: write` + 默认 `GITHUB_TOKEN` 即可创建 Release。
- 仓库为 **public** 时，Actions 按 GitHub 免费额度运行。

建议确认：

1. https://github.com/kzq0717/GoldPriceBar/settings/actions  
   - 允许 Actions  
   - Workflow permissions：**Read and write**（创建 Release 失败时检查此项）

2. https://github.com/kzq0717/GoldPriceBar/actions  
   - “Build and Package” 是否成功

## 发版标准流程

```bat
cd D:\Project\Tools\GoldPriceBar
git checkout main
git pull

git add .
git commit -m "Release v0.x.y"
git push origin main

git tag -a v0.x.y -m "GoldPriceBarLite v0.x.y"
git push origin v0.x.y
```

推送 `v*` tag 后 Actions 会自动：

1. 编译 Windows 包  
2. 创建 GitHub Release 并上传 zip  
3. 客户端「检查更新」可读 `releases/latest`

## 常见问题

| 现象 | 处理 |
|------|------|
| publish-release 跳过 | tag 必须以 `v` 开头（`v0.4.1`） |
| 403 创建 Release | Actions → Workflow permissions → Read and write |
| 检查更新仍旧 | 确认 Release 非 draft，且为最新正式版 |

