# WT SSH Manager

一个适合 **Windows Terminal 纯文字界面** 使用的小工具：
- 记录常用 SSH 服务器（像 PuTTY 保存会话）
- 在 Text UI 里选择并打开 SSH 连接
- **不保存密码**，认证完全交给 `ssh`

## 你现在有两个版本

- `wtssh`（C++，Text UI，可编译成二进制，推荐）
- `wt_ssh_manager.py`（旧版 Python，保留）

---

## C++ Text UI 版本（推荐）

### 功能

- 全屏文本界面（上下键选择）
- 按键操作：
  - `A` 新增
  - `D` 删除
  - `C` 连接
  - `Q` 退出
- 数据保存在：`~/.wt_ssh_manager/hosts.db`

### 构建（本地）

```bash
cmake -S . -B build
cmake --build build --config Release
```

构建后可执行文件：
- Linux/macOS: `build/wtssh`
- Windows: `build/Release/wtssh.exe`（或 `build/wtssh.exe`，取决于生成器）

### 使用（不需要 python 前缀）

```bash
./build/wtssh
```

Windows 可直接运行：

```powershell
.\build\Release\wtssh.exe
```

如果你把 `wtssh.exe` 放进 PATH 目录，就可以直接输入：

```powershell
wtssh
```

---

## Python 版本（保留）

```bash
python wt_ssh_manager.py
```

> Python 版依旧可用，但如果你希望“直接运行二进制 + Text UI”，请使用 C++ 版。

## 安全说明

- 不记录 SSH 密码。
- 若配置私钥，只记录私钥路径，不保存密钥内容。
