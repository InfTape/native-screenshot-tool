# 原生 Win32 截图及录屏工具

一个使用原生 Win32 界面和 Windows Graphics Capture (WGC) 采集的桌面截图、录屏程序，项目结构按职责拆分。

## 当前能力

- 支持全屏截图、选区截图、窗口截图
- 支持多显示器虚拟桌面截图
- 全屏、选区和窗口截图统一使用 WGC，不使用 GDI 采集或回退；截图不包含鼠标指针，PNG/BMP 保留原始尺寸，包括奇数宽高
- 窗口截图直接采集目标 HWND，其他窗口遮挡不影响目标内容；PNG 保留窗口透明圆角
- 进程已启用 DPI 感知，截图按显示器真实像素输出
- 选区截图使用遮罩层框选，未选区域会被半透明覆盖
- 选区遮罩层采用双缓冲与局部失效重绘，拖动框选时更平滑
- 矩形、箭头、画笔和马赛克支持最近 20 步撤销；历史只保存标注操作与一张基准图，撤销时按顺序重绘，不再为每一步保留整张截图
- 窗口截图支持悬停高亮并点选目标窗口
- 选中窗口后的截图编辑覆盖整个桌面，窗口外显示暗色遮罩，操作提示保持可见；默认导出目标窗口，遮罩和提示不会进入图片
- 三种截图模式都支持独立的全局快捷键，并持久化保存
- 支持系统托盘常驻、托盘菜单触发截图、双击托盘恢复主窗口
- 支持 PNG / BMP 保存，三种截图模式都可自动保存并复制到剪贴板
- 选区、窗口截图的编辑浮条支持 OCR，离线识别当前选区文字；可直接在截图上跨行拖选、双击选词、Ctrl+A 全选和 Ctrl+C 复制
- OCR 模式支持谷歌在线翻译，默认英文转简体中文；选中文字时翻译选中部分，未选择时翻译全部，在原文/译文窗口中查看和复制译文
- 支持全屏、选区和窗口录屏，可选择 H.264 MP4 或循环播放的 GIF，包含鼠标指针
- 录屏统一使用 Windows Graphics Capture (WGC)，通过 D3D11 按原始像素裁剪、拼接跨屏选区；不使用 GDI 或 DXGI Desktop Duplication 回退
- 录制时显示红色范围边框、范围外淡色遮罩及计时浮条，可直接停止并保存；遮罩不阻挡鼠标操作
- 窗口录制基于显示器画面动态跟随目标窗口范围裁剪，完整捕获弹窗、对话框、下拉菜单与右键菜单，并跟随目标窗口移动；窗口尺寸改变、最小化、隐藏或关闭后自动保存并结束
- 录屏保留原始分辨率，不再缩小到 1080p；MP4 奇数宽高只在右侧或底部补 1 像素黑边，GIF 无需补边
- GIF 使用系统 WIC 编码器、自适应 256 色调色板，目标 15 FPS；合并相同画面的停留时间，只编码发生变化的矩形区域，内存占用不随录制时长增长
- H.264 High Profile、质量优先 VBR 编码，目标帧率 30 FPS；允许文件更大以保留文字细节，优先使用可用的系统硬件编码器
- 录屏在独立线程中采集和编码，停止时自动保存并将 MP4/GIF 文件复制到剪贴板，托盘退出时也会保存并复制
- 全屏、选区、窗口录制分别支持可自定义、持久化的全局快捷键；截图设置在左列，录制设置在右列
- 主窗口、标题栏、托盘菜单、悬停提示、截图/录屏遮罩说明、尺寸标签和编辑浮条统一使用 9pt 正常字重的微软雅黑 UI，随窗口所在显示器 DPI 缩放
- 主窗口操作按钮统一宽高，同行说明文字垂直居中；开机启动选项的点击范围限定为勾选框和文字

应用图标采用与 LanBridge 一致的浅蓝底（`#DBE4FF`）和深蓝取景框、剪刀（`#3455AD`），程序文件、标题栏、任务栏及托盘共用嵌入的多尺寸 ICO。可编辑 SVG 与预览 PNG 位于 `assets`；图标资源随 EXE 编译，运行时无需外部图标文件。

## 目录结构

```text
.
|-- include/
|   |-- app/        # 应用启动与消息循环
|   |-- capture/    # 屏幕/窗口采集、图像数据、文件写入、录屏编码
|   |-- common/     # 通用 Win32 工具
|   |-- hotkey/     # 热键定义与注册
|   |-- settings/   # 设置加载与持久化
|   `-- ui/         # 主窗口、托盘、选区/窗口选择遮罩层
|-- src/
|   |-- app/
|   |-- capture/
|   |-- common/
|   |-- hotkey/
|   |-- settings/
|   `-- ui/
`-- CMakeLists.txt
```

## 构建

如果本机安装了 Visual Studio，可按已安装版本选择生成器，例如：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

生成的可执行文件位于：

```text
build\Release\NativeScreenshot.exe
```

需要 Visual Studio C++ 构建工具及带 C++/WinRT 的 Windows SDK。程序要求 Windows 10 2004 或更新版本（推荐 Windows 11）及可用的 D3D11 显卡，使用 Windows 自带的 WGC 和
[Media Foundation Sink Writer](https://learn.microsoft.com/en-us/windows/win32/medfound/using-the-sink-writer)，
无需安装 FFmpeg；Windows N/KN 版本需要先安装媒体功能包。

构建后可执行以下集成测试。测试会短暂显示一个颜色测试窗口，验证实际录制、
H.264 解码、文字与单像素细节、画面方向、时长、中文路径、立即停止、重复录制、原始分辨率以及快捷键持久化：
另外还会验证不同 DPI 下的字体、标题栏命中区域、托盘提示和菜单绘制，以及窗口截图编辑中的桌面遮罩、操作提示和导出像素。
截图测试验证 WGC 全屏、选区、被遮挡窗口、单像素细线、奇数尺寸、PNG/BMP 无损保存、重复采集及失效目标报错。

```powershell
ctest --test-dir build -C Release --output-on-failure
```

仅构建程序时，可在配置命令后追加 `-DBUILD_TESTING=OFF`。

## OCR 使用

1. 进行选区截图或窗口截图，调整到需要识别的区域，在浮动编辑条上点击 **OCR**。
2. 在当前截图上后台识别，不会打开单独结果窗口。完成后直接拖动选中文字，蓝色高亮显示选择范围。
3. 支持跨行拖选、双击选词、Ctrl+A 全选；按 Ctrl+C、点击浮条“复制”或右键菜单复制选中文字。
   复制后仍保留截图，可继续选择。浮条“返回”恢复编辑，“关闭” / Enter / Esc 退出文字选择。
   没有文字或缺少组件时在截图内显示提示。高亮不会写入截图像素。
4. 点击 OCR 浮条“翻译”、按 Ctrl+T，或使用右键菜单“翻译为简体中文”。有选中文字时只翻译选中部分，否则翻译全部识别文字。
   默认源语言为英文、目标语言为简体中文。结果窗口显示原文与译文，可选中复制，也可点击“复制译文”；关闭结果窗口后可继续 OCR 拖选。
   翻译前根据文字行间距、大小、对齐和句末标点合并英文的排版换行，避免将 `have` 与下一行的 `arrived` 分开翻译；段落、标题和列表边界尽量保留。
   原文显示、拖选及复制保留原来的换行，仅发送给翻译接口的文本会进行合并。这是启发式判断，复杂排版仍可能出现误判。
   翻译在后台进行，关闭结果窗口、返回编辑或退出截图会取消请求；网络失败时保留原文和 OCR 结果，并显示错误，可再次点击“翻译”重试。

OCR 识别仍然完全离线；只有主动触发翻译时，待翻译的文字才直接发送到谷歌服务器，不上传截图。
翻译使用 Google 翻译网页版的 `MkEWBc` RPC，参考
[GTranslate.GoogleTranslator2](https://github.com/d4n3436/GTranslate/blob/master/src/GTranslate/Translators/GoogleTranslator2.cs)，
以 Windows 自带 WinHTTP 和 JSON API 实现，无需 Python、.NET、额外运行时或 Google Cloud API Key。
超过接口单次 5000 UTF-16 字符限制的文字会按换行/空白分段，避免拆开 Unicode 代理对。
这是非官方网页接口，可能限流或发生变化；程序不会绕过限流，失败时需稍后重试。
`GoogleTranslationIntegration` 默认只做离线协议和窗口生命周期测试；实际联网验证可运行
`build\Release\GoogleTranslationIntegration.exe --live`。
OCR 按钮到谷歌接口的完整链路可运行 `build\Release\OneOcrIntegration.exe --live-translation`，
验证整段/选中文字翻译、关闭译文后继续 OCR 复制；两项联网验证不加入默认 CTest。

OCR 使用本机 Windows 照片 / 截图工具附带的 OneOCR 引擎。程序优先查找已安装的照片包，
也支持截图工具中的组件；首次使用会将 `oneocr.dll`、`oneocr.onemodel`、`onnxruntime.dll`
复制到 `%LOCALAPPDATA%\NativeScreenshot\OneOCR\包完整名称\` 缓存，约 120 MB，
无需修改 WindowsApps 权限。包更新后会使用对应新版本缓存，旧缓存可在程序退出后删除。
也可在程序旁的 `ocr\` 目录提供匹配的三个文件。
不需要 Python、联网识别或 NPU；本仓库不附带微软 DLL 和模型文件。

每次识别按需启动一个隐藏的 OCR 子进程，由同一个 EXE 提供，无需额外安装辅助程序。
截图通过匿名共享内存传递，结果只返回文字与坐标；识别完成后子进程退出，模型及推理缓存随之释放。
取消识别、返回编辑或关闭截图会终止正在识别的子进程；主程序退出时也会清理子进程。
主进程不加载 OCR DLL 和模型。上述约 120 MB 是磁盘缓存，每次识别仍需重新初始化模型。

OneOCR 是应用内部接口，后续更新可能改变接口或模型兼容性。
调用参数参考 [AuroraWright/oneocr](https://github.com/AuroraWright/oneocr/blob/main/oneocr.py)
和 [b1tg/win11-oneocr](https://github.com/b1tg/win11-oneocr/blob/master/ocr.cpp)。
文字与坐标对齐、拖选及高亮的思路参考 [xland/ImageReader](https://github.com/xland/ImageReader)，
由 [xland/ScreenCapture](https://github.com/xland/ScreenCapture) 调用。当前实现直接在原生截图浮层内交互。
`OneOcrIntegration` 测试需要本机安装包含 OneOCR 的照片 / 截图工具，
验证中文英文识别、进程隔离及取消、文字坐标、跨行及反向拖选、Unicode 复制和异步关闭；使用独立窗口站，
不会覆盖用户的剪贴板。

## 录屏使用

1. 在录制按钮右侧的格式选单选择 MP4 或 GIF，选择会持久化保存。
   点击“全屏录制”立即录制整个虚拟桌面，或点击“选区录制”拖动框选范围；
   “窗口录制”通过点击选择目标窗口。
   选区可以移动和缩放，按 Enter 或点击“开始录制”确认，Esc / 右键取消。
2. 开始后主窗口会收至托盘，屏幕显示范围提示和 `时:分:秒` 计时浮条。
   可通过浮条“停止并保存”、托盘菜单、主窗口“停止录制”按钮，
   或再次按录屏快捷键停止；双击托盘图标可查看录制时长。
3. 录屏自动保存到现有“保存目录”，文件名为 `Recording_日期_时间_唯一编号.mp4` 或 `.gif`，
   不受 PNG / BMP 截图格式选项影响。已有文件不会被覆盖。
   保存成功后自动将文件复制到剪贴板，可在文件夹或支持文件粘贴的聊天软件中按 Ctrl + V 粘贴；
   GIF 保留动画，复制的是文件本身。如果复制失败，会提示错误，已保存的录屏文件仍然保留。
4. 在右列“设置全屏录制键”“设置选区录制键”“设置窗口录制键”中分别设置快捷键，
   设置会保存到 `settings.ini`。未录制时启动对应模式，录制时任一已设置的录制快捷键可停止并保存。

当前录屏为无音频视频，不录制系统声音或麦克风。录制期间暂停截图与快捷键修改，
以避免选择遮罩层进入视频。主窗口最小化或关闭后录屏继续；在托盘选择“退出”会保存后退出。
MP4 分辨率会对齐到偶数像素，GIF 保留包括奇数边长在内的原始尺寸。
实际帧率取决于机器采集和编码速度；GIF 根据实际经过的时间设置帧停留时长。

GIF 编码参考 [ScreenToGif 的调色板、帧区域与动画时序设计](https://github.com/NickeManarin/ScreenToGif/blob/master/ScreenToGif.Util/Codification/Gif/Encoder/GifFile.cs)，
本项目以 C++ / Windows 原生 WIC 实现，无需 .NET 或外部 FFmpeg。
GIF 每帧最多 256 色，复杂渐变会有色阶损失，较长录制也可能产生较大文件；需要更完整的颜色和更高帧率时选择 MP4。

录屏提示层使用 Windows 10 2004 及以上的
[WDA_EXCLUDEFROMCAPTURE](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)
排除在视频之外。浮条可拖动；选区和窗口录制的范围外会显示可穿透点击的淡色遮罩。
窗口录制通过 WGC 直接采集目标 HWND，遮挡窗口不会改变录制目标。
WGC 不支持、采集失败或显卡设备失效时会明确报错，不回退到 GDI。
系统可能显示 WGC 捕获边框。截图也使用同一 WGC 采集层；GDI 仅用于部分 Win32 界面和标注绘制，不再用于屏幕或窗口采集。
当前采集使用 8 位 BGRA 的 SDR 输出，尚未实现 HDR 色调映射。

## 默认快捷键

```text
全屏截图：Ctrl + Shift + F
选区截图：Ctrl + Shift + A
窗口截图：Ctrl + Shift + W
全屏录屏开始 / 当前录屏停止：Ctrl + Shift + R
选区录制：未设置
窗口录制：未设置
```

设置文件默认保存在：

```text
%LOCALAPPDATA%\NativeScreenshot\settings.ini
```

对应的键名为：

```text
[hotkeys]
full_capture_modifiers=
full_capture_virtual_key=
region_capture_modifiers=
region_capture_virtual_key=
window_capture_modifiers=
window_capture_virtual_key=
screen_recording_modifiers=
screen_recording_virtual_key=
region_recording_modifiers=
region_recording_virtual_key=
window_recording_modifiers=
window_recording_virtual_key=
```

## 托盘行为

```text
双击托盘图标：恢复主窗口
右键托盘图标：显示主窗口 / 截图 / 全屏录制 / 选区录制 / 窗口录制 / 停止录制并保存 / 退出
最小化或关闭主窗口：隐藏到托盘，不直接退出程序
```

主窗口空白处右键或按 Shift+F10 也可打开同一菜单。标题栏由 Win32/GDI 绘制，
保留拖动、双击最大化/还原、边缘缩放及 Alt+Space 系统菜单。

## 后续扩展建议

- 在 `capture/` 下继续拆分区域截图、延时截图和滚动截图服务
- 增加统一的 `ImageEncoder`，扩展 PNG / JPEG 输出
- 在 `ui/` 下加入标注工具栏、历史记录面板和设置对话框
- 在 `settings/` 下继续扩展更多用户偏好，例如保存目录、文件命名规则、是否开机启动
