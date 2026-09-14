# 构建与部署（Windows）

## 1. 依赖

- Qt 6.7+（msvc2022_64 或本机对应套件），含 **Charts、Sql**  
- Visual Studio 2022/2026 C++ 桌面开发  
- CMake ≥ 3.21  
- 可选：Ninja  

## 2. 推荐流程

在 **x64 Native Tools Command Prompt for VS** 中：

```bat
cd /d D:\Project\Tools\GoldPriceBar
set QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64

build.bat clean vs
deploy.bat
```

### 生成器说明

| 场景 | 命令 |
|------|------|
| 已有 VS 缓存 | `build.bat`（自动沿用） |
| 改用 Ninja | `build.bat clean ninja` |
| 改用 VS | `build.bat clean vs` |
| 生成器混用报错 | 必须 `clean` 后再编 |

**不要**在未清理时在 Ninja 与 Visual Studio 生成器之间切换。

## 3. 缺 Qt DLL

现象：提示缺少 `Qt6Core.dll` 等。

```bat
deploy.bat
```

脚本使用 `windeployqt`，向 exe 同目录复制：

- Qt6Core / Gui / Widgets / Network / Charts / Sql  
- `platforms\qwindows.dll`  
- MSVC 运行库（`--compiler-runtime`）  

仅调试可：

```bat
run_with_qt.bat
```

## 4. 手动 CMake

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 ^
  -DQT6_ROOT_DIR=%QT6_ROOT% -DCMAKE_PREFIX_PATH=%QT6_ROOT%
cmake --build build --config Release --parallel
```

Ninja：

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DQT6_ROOT_DIR=%QT6_ROOT% -DCMAKE_PREFIX_PATH=%QT6_ROOT%
cmake --build build --parallel
```

## 5. 产物路径

| 生成器 | 可执行文件 |
|--------|------------|
| Visual Studio | `build\Release\GoldPriceBarLite.exe` |
| Ninja | `build\GoldPriceBarLite.exe` |

## 6. 常见问题

**C2001 newline in string literal**  
源文件 UTF-8 中文 + 系统代码页 936。工程已加 `/utf-8`；请 `git pull` 后 `build.bat clean`。

**CMake generator does not match**  
`build.bat clean` 后重试。

**windeployqt 找不到**  
检查 `QT6_ROOT%\bin\windeployqt.exe`。


## C1083: Cannot open include file: 'type_traits'

原因：使用 **Ninja** 时 `cl.exe` 在 PATH 中，但 **MSVC 标准库目录未加入 INCLUDE**（未执行 vcvars）。

处理：

```bat
build.bat clean vs
```

或在本仓库最新 `build.bat` 中会自动尝试调用 `vcvars64.bat`，再：

```bat
build.bat clean ninja
```
