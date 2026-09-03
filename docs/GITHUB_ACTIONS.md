# GitHub Actions 自动构建与 Release

## 关于页面上的 Token 提示

> Upcoming change to GitHub App installation token format (ghs_...)

这是 **GitHub 平台公告**，与是否生成 Release **无关**，可忽略。

## 为何之前没有自动 Release？

查看 Actions 记录：`windows-build` 在 **Install Qt 6.7** 步骤失败 →  
`publish-release` 因依赖失败被 **skipped** → 仓库 Releases 为空。

已将 Qt 安装改为 `aqtinstall` + **Qt 6.5.3 / win64_msvc2019_64**（更稳定）。

## 你需要做的配置

1. **Settings → Actions → General**
   - Actions 权限：允许
   - Workflow permissions：**Read and write**
2. 把修复后的 `.github/workflows/release.yml` 推到仓库  
   - 若 PAT 无 `workflow` 权限，请在网页上直接编辑该文件并粘贴内容，或使用勾选了 **`workflow`** 的 Token 推送。

## 发版

```bat
git push origin main
git tag -a v0.4.2 -m "v0.4.2"
git push origin v0.4.2
```

成功后应看到：

1. Actions 全绿（含 `publish-release`）
2. https://github.com/kzq0717/GoldPriceBar/releases 出现附件 zip

也可对已有 tag 在 Actions 页 **Re-run all jobs**。

## 检查更新

客户端读取 `GET /repos/kzq0717/GoldPriceBar/releases/latest`，  
需要至少有一个 **Published**（非 draft）的 Release。
