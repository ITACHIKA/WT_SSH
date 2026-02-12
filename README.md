# WT SSH Manager

一个适合 **Windows Terminal 纯文字界面** 使用的小工具：
- 记录你常用的 SSH 服务器（像 PuTTY 保存会话一样）
- 一键从列表中打开 SSH 连接
- **不保存密码**，认证完全交给 `ssh`（密码输入 / 私钥 / agent）

## 功能

- 交互式菜单（默认启动）
- 命令行模式（方便脚本或快速操作）
- 主机信息本地保存到 `~/.wt_ssh_manager/hosts.json`

## 运行环境

- Python 3.9+
- Windows 建议已安装 OpenSSH Client（`ssh` 命令可用）

## 快速开始

```bash
python wt_ssh_manager.py
```

进入后可看到菜单：
1. 查看服务器
2. 添加服务器
3. 删除服务器
4. 连接服务器
5. 退出

## 命令行模式

### 列出所有服务器

```bash
python wt_ssh_manager.py list
```

### 添加服务器

```bash
python wt_ssh_manager.py add office 10.0.0.8 --user admin --port 22 --note "办公室机器"
```

### 删除服务器

```bash
python wt_ssh_manager.py remove office
```

### 连接服务器

```bash
python wt_ssh_manager.py connect office
```

## 数据示例

`~/.wt_ssh_manager/hosts.json` 大概长这样：

```json
[
  {
    "name": "office",
    "host": "10.0.0.8",
    "user": "admin",
    "port": 22,
    "key_file": null,
    "note": "办公室机器"
  }
]
```

## 安全说明

- 本工具不会记录 SSH 密码。
- 如果你设置了 `key_file`，记录的是密钥路径，不是密钥内容。

