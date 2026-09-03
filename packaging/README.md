# 打包说明

## 便携版（zip）

CI 已生成 `GoldPriceBarLite-*-win64.zip`，解压即用。

## 安装包（Inno Setup）

1. 将 zip 解压内容复制到 `packaging/app/`（需含 `GoldPriceBarLite.exe` 与 Qt DLL）。
2. 安装 [Inno Setup 6](https://jrsoftware.org/isinfo.php)。
3. 编译：

```bat
ISCC.exe packaging\GoldPriceBarLite.iss
```

4. 得到 `packaging\Output\GoldPriceBarLite-Setup-x64.exe`。

可选：在 CI 中安装 Inno 并调用 ISCC（需额外步骤）。
