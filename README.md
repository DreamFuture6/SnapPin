# SnapPin

一个面向 Windows 的轻量级桌面截图、贴图与 OCR 工具，强调启动快、交互轻、编辑直接，适合日常开发与办公截图场景。

## 项目简介

SnapPin 围绕“快速截图 -> 快速编辑 -> 快速输出”设计，不追求复杂图像处理能力，而是聚焦高频桌面截图工作流。

当前版本主要覆盖以下场景：

- 区域截图与全屏截图
- 截图后直接标注
- 复制、保存、贴图
- 长截图与白板模式
- OCR 识别
- 多屏幕、DPI 感知的桌面环境

## 功能特性

### 截图与标注

- 支持区域截图
- 支持全屏截图
- 支持截图后立即进入编辑模式
- 支持以下标注工具：
  - 矩形
  - 椭圆
  - 直线
  - 箭头
  - 自由画笔
  - 马赛克
  - 文本
  - 序号标记
- 支持撤销、重做
- 支持颜色、线宽、填充、文字样式调整

### 输出能力

- 复制到剪贴板
- 保存到文件
- 贴图到桌面悬浮窗

### 扩展能力

- 长截图模式
- 白板模式
- OCR 文字识别

## OCR 说明

当前版本使用 PaddleOCR 在线 API。

在软件中进入“设置 -> OCR”后，需要配置：

- `PaddleOCR 服务 URL`
- `Access Token`

接口获取入口可参考：

- [https://aistudio.baidu.com/paddleocr](https://aistudio.baidu.com/paddleocr)

说明：

- 项目不会内置你的 OCR 凭据
- 请不要把你自己的 OCR URL 和 Token 提交到 GitHub
- 上传公开仓库前，请先确认本地配置中不包含敏感信息

## 技术栈

- C++17
- Win32 API
- GDI / GDI+
- CMake
- Visual Studio / MSVC

## 目录结构

```text
src/
  app/        应用编排、托盘、热键、主流程控制
  core/       截图、导出、OCR、设置、日志、基础能力
  ui/         窗口、工具栏、设置页、贴图窗口、UI 公共组件
```

## 构建环境

建议环境：

- Windows 10 / Windows 11
- Visual Studio 2022
- CMake 3.20+
- MSVC v143 工具链
- Windows SDK

### 录频导出（FFmpeg）依赖

录频“非原生画质”导出使用 FFmpeg（库方式，不依赖系统安装）。

项目会在 CMake 配置阶段自动下载并集成 FFmpeg SDK（Windows x64 shared）：

说明：

- 首次构建时，自动下载压缩包并解压到 `build/_deps/ffmpeg`
- 首次下载成功后，自动复制一份到项目目录 `third_party/ffmpeg`
- 后续构建若检测到 `third_party/ffmpeg` 完整存在，则直接使用本地文件，不再联网下载
- 自动链接 `import library`（`*.lib`）
- 构建后自动拷贝 `avcodec/avformat/avutil/swscale` 相关 DLL 到可执行文件目录
- 目标机器无需安装 FFmpeg，只需随程序分发 `SnapPin.exe` 与同目录下的 FFmpeg DLL
- 如需固定版本或使用镜像源，可设置 `-DSNAPPIN_FFMPEG_DOWNLOAD_URL=<url>`

可选：生成裁剪版 FFmpeg（显著减小 DLL 体积）：

```powershell
powershell -ExecutionPolicy Bypass -File tools/ffmpeg/build_minimal_msvc.ps1
```

执行后会将最小化构建结果写入 `third_party/ffmpeg`，后续 CMake 配置将优先使用本地裁剪版。
默认需要本机存在 `D:\Software\msys64`，可通过 `-MsysRoot` 传入你的 MSYS2 路径。

## 构建方式

### 方式一：使用 Visual Studio

1. 用 Visual Studio 2022 打开项目根目录
2. 等待 CMake 配置完成
3. 选择 `Release` 或 `Debug`
4. 构建 `SnapPin`

### 方式二：使用 CMake

```powershell
cmake -S . -B build
cmake --build build --config Release --target SnapPin
```

## 运行与配置

首次运行后，建议优先检查：

- 常规设置
- OCR 设置
- 与截图交互相关的个性化选项

如果需要 OCR，请先确保服务 URL 与 Access Token 已正确填写。

## 注意事项

- 本项目仅面向 Windows 平台
- OCR 依赖在线服务，不属于完全离线能力
- 本项目主体内容由AI生成，人工仅对UI细节进行了调整与优化。项目内容仅供学习与研究参考，不构成任何专业建议或商业保证。
