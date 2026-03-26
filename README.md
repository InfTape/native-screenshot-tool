# 原生 Win32 截图工具

一个基于原生 Win32 API 和 GDI 的桌面截图程序，项目结构按职责拆分，方便后续继续扩展截图模式、编码格式、标注能力和更多交互。

## 当前能力

- 支持全屏截图、选区截图、窗口截图
- 支持多显示器虚拟桌面截图
- 进程已启用 DPI 感知，截图按显示器真实像素输出
- 选区截图使用遮罩层框选，未选区域会被半透明覆盖
- 选区遮罩层采用双缓冲与局部失效重绘，拖动框选时更平滑
- 窗口截图支持悬停高亮并点选目标窗口
- 三种截图模式都支持独立的全局快捷键，并持久化保存
- 支持系统托盘常驻、托盘菜单触发截图、双击托盘恢复主窗口
- 主窗口内置截图预览与 BMP 保存

## 目录结构

```text
.
|-- include/
|   |-- app/        # 应用启动与消息循环
|   |-- capture/    # 屏幕/窗口采集、图像数据、文件写入
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
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

生成的可执行文件位于：

```text
build\Release\NativeScreenshot.exe
```

## 默认快捷键

```text
全屏截图：Ctrl + Shift + F
选区截图：Ctrl + Shift + A
窗口截图：Ctrl + Shift + W
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
```

## 托盘行为

```text
双击托盘图标：恢复主窗口
右键托盘图标：显示主窗口 / 全屏截图 / 选区截图 / 窗口截图 / 退出
最小化或关闭主窗口：隐藏到托盘，不直接退出程序
```

## 后续扩展建议

- 在 `capture/` 下继续拆分区域截图、延时截图和滚动截图服务
- 增加统一的 `ImageEncoder`，扩展 PNG / JPEG 输出
- 在 `ui/` 下加入标注工具栏、历史记录面板和设置对话框
- 在 `settings/` 下继续扩展更多用户偏好，例如保存目录、文件命名规则、是否开机启动
