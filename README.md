# 杰睿 JeriBot

## 一、项目简介

**杰睿 JeriBot 是一款让大模型理解并操作 Windows 桌面软件的智能辅助工具。**
它的核心目标是让桌面软件界面变得可理解、可标注、可辅助操作。通过解析与识别分析 Windows 桌面窗口及控件元素，例如窗口、菜单、按钮、文本框等，JeriBot 可为桌面自动化、辅助操作和智能交互提供基础能力。

## 二、设计理念

“杰睿”二字代表杰出、睿智。杰睿 JeriBot 的设计理念是成为一个“智慧的鼠标”——协助智慧的大模型看懂桌面界面中到底有什么控件和窗口，继而完成用户给出的指令及任务。

## 三、技术原理

基于 Windows UI Automation 技术，通过读取桌面应用的 UIA 元素树，获取窗口、控件、文本、位置和可操作模式等信息，并结合本地级别的可视化标注能力，辅以视觉大模型的分析，将传统桌面软件界面转化为大模型可理解、可分析、可辅助操作的结构化信息。

## 四、项目结构

```
JeriBot/
├── README.md
├── LICENSE
└── .gitignore
```

## 五、编译说明

### 环境要求

- Windows 10 或更高版本
- [Visual Studio](https://visualstudio.microsoft.com/zh-hans/) 并安装「使用 C++ 的桌面开发」工作负载
- [CMake](https://cmake.org/download/) 3.16 或更高版本，并加入 PATH 环境变量
- [PowerShell 7](https://github.com/PowerShell/PowerShell/releases)（pwsh.exe），用于运行 PowerShell 编译脚本

### 编译步骤

运行脚本：

```
Build_PowerShell7.ps1
```

按提示输入 `1` 或 `2` 选择架构

### 输出位置

| 项目 | 输出路径 |
|------|----------|
| JeriBot.exe | `\Debug\Export\JeriBot_x64.exe` 或 `JeriBot_x86.exe` |

### 常见问题

- **构建失败** — 请确认 Visual Studio C++ 桌面开发环境和 CMake 均已正确安装并配置 PATH。
- **端口占用** — 主程序默认监听 `127.0.0.1:11111`，如有冲突请修改 `%LocalAppData%\JeriBot\JeriBot.json` 中的 `Server.Port`。

## 六、注意事项

- 本项目统一使用 UTF-8 编码和 LF 换行符。
- .rc 文件通过 `#pragma code_page(65001)` 在文件内声明编码。
- .cpp 文件通过 CMake 编译选项 `/utf-8` 指定 UTF-8 源码字符集与执行字符集。

## 七、协议与版权

本项目采用 MIT License，详见 [LICENSE](./LICENSE) 文件。

版权所有 © 2026 重庆沛雨霖科技有限公司

Chongqing Peiyulin Technology Co., Ltd. (PYLinTech)
