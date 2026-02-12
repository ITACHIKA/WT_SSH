#!/usr/bin/env python3
"""Windows Terminal SSH 会话记录工具（纯文本交互）。"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Dict, List, Optional

APP_DIR_NAME = ".wt_ssh_manager"
HOSTS_FILE_NAME = "hosts.json"


@dataclass
class HostEntry:
    """单个 SSH 主机记录。"""

    name: str
    host: str
    user: Optional[str] = None
    port: int = 22
    key_file: Optional[str] = None
    note: Optional[str] = None

    @property
    def target(self) -> str:
        if self.user:
            return f"{self.user}@{self.host}"
        return self.host


class HostStore:
    """负责主机记录读写。"""

    def __init__(self) -> None:
        home = Path(os.path.expanduser("~"))
        self.data_dir = home / APP_DIR_NAME
        self.data_file = self.data_dir / HOSTS_FILE_NAME
        self.data_dir.mkdir(parents=True, exist_ok=True)

    def load(self) -> List[HostEntry]:
        if not self.data_file.exists():
            return []
        with self.data_file.open("r", encoding="utf-8") as f:
            raw = json.load(f)

        entries: List[HostEntry] = []
        for item in raw:
            entries.append(HostEntry(**item))
        return entries

    def save(self, entries: List[HostEntry]) -> None:
        ordered = sorted(entries, key=lambda x: x.name.lower())
        with self.data_file.open("w", encoding="utf-8") as f:
            json.dump([asdict(e) for e in ordered], f, ensure_ascii=False, indent=2)


def entries_to_map(entries: List[HostEntry]) -> Dict[str, HostEntry]:
    return {entry.name: entry for entry in entries}


def print_hosts(entries: List[HostEntry]) -> None:
    if not entries:
        print("\n当前没有已保存的服务器。\n")
        return

    print("\n已保存服务器：")
    for idx, entry in enumerate(sorted(entries, key=lambda x: x.name.lower()), start=1):
        details = [f"目标={entry.target}", f"端口={entry.port}"]
        if entry.key_file:
            details.append(f"密钥={entry.key_file}")
        if entry.note:
            details.append(f"备注={entry.note}")
        print(f"  {idx}. {entry.name:<15} " + " | ".join(details))
    print("")


def prompt(text: str, default: Optional[str] = None) -> str:
    suffix = f" [{default}]" if default else ""
    value = input(f"{text}{suffix}: ").strip()
    if not value and default is not None:
        return default
    return value


def add_host(store: HostStore, entries: List[HostEntry]) -> List[HostEntry]:
    print("\n== 添加服务器 ==")
    name = prompt("显示名称(唯一)")
    if not name:
        print("名称不能为空。")
        return entries

    host_map = entries_to_map(entries)
    if name in host_map:
        print(f"名称 '{name}' 已存在。")
        return entries

    host = prompt("主机/IP")
    if not host:
        print("主机不能为空。")
        return entries

    user = prompt("用户名(可空)") or None

    port_raw = prompt("端口", "22")
    try:
        port = int(port_raw)
    except ValueError:
        print("端口必须是数字。")
        return entries

    key_file = prompt("私钥路径(可空, 例如 C:/Users/you/.ssh/id_rsa)") or None
    note = prompt("备注(可空)") or None

    entries.append(HostEntry(name=name, host=host, user=user, port=port, key_file=key_file, note=note))
    store.save(entries)
    print(f"已保存服务器: {name}")
    return entries


def remove_host(store: HostStore, entries: List[HostEntry]) -> List[HostEntry]:
    print_hosts(entries)
    if not entries:
        return entries

    name = prompt("输入要删除的显示名称")
    new_entries = [e for e in entries if e.name != name]
    if len(new_entries) == len(entries):
        print(f"未找到服务器: {name}")
        return entries

    store.save(new_entries)
    print(f"已删除: {name}")
    return new_entries


def build_ssh_command(entry: HostEntry) -> List[str]:
    cmd = ["ssh", entry.target, "-p", str(entry.port)]
    if entry.key_file:
        cmd.extend(["-i", entry.key_file])
    return cmd


def connect_host(entries: List[HostEntry], name: Optional[str] = None) -> None:
    if not entries:
        print("没有可连接的服务器，请先添加。")
        return

    host_map = entries_to_map(entries)

    if not name:
        print_hosts(entries)
        name = prompt("输入要连接的显示名称")

    entry = host_map.get(name)
    if not entry:
        print(f"未找到服务器: {name}")
        return

    cmd = build_ssh_command(entry)
    print(f"\n正在连接: {entry.name} ({entry.target})")
    print("执行命令:", " ".join(cmd))
    print("提示：不会记录密码，认证由 ssh 自己处理。\n")

    try:
        subprocess.run(cmd, check=False)
    except FileNotFoundError:
        print("错误：未找到 ssh 命令。请确认已安装 OpenSSH Client。")


def interactive_menu(store: HostStore) -> None:
    entries = store.load()
    while True:
        print("=" * 45)
        print("Windows Terminal SSH 管理器")
        print("1) 查看服务器")
        print("2) 添加服务器")
        print("3) 删除服务器")
        print("4) 连接服务器")
        print("5) 退出")
        print("=" * 45)

        choice = input("请选择操作 [1-5]: ").strip()

        if choice == "1":
            print_hosts(entries)
        elif choice == "2":
            entries = add_host(store, entries)
        elif choice == "3":
            entries = remove_host(store, entries)
        elif choice == "4":
            connect_host(entries)
        elif choice == "5":
            print("再见。")
            return
        else:
            print("无效输入，请输入 1-5。")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="在 Windows Terminal 里记录和打开 SSH 服务器（不保存密码）"
    )
    sub = parser.add_subparsers(dest="command")

    sub.add_parser("list", help="列出所有服务器")

    add_p = sub.add_parser("add", help="添加服务器")
    add_p.add_argument("name", help="显示名称（唯一）")
    add_p.add_argument("host", help="主机名或 IP")
    add_p.add_argument("--user", help="用户名")
    add_p.add_argument("--port", type=int, default=22, help="端口，默认 22")
    add_p.add_argument("--key-file", help="私钥文件路径")
    add_p.add_argument("--note", help="备注")

    rm_p = sub.add_parser("remove", help="删除服务器")
    rm_p.add_argument("name", help="显示名称")

    conn_p = sub.add_parser("connect", help="连接服务器")
    conn_p.add_argument("name", help="显示名称")

    return parser


def run_non_interactive(args: argparse.Namespace, store: HostStore) -> int:
    entries = store.load()

    if args.command == "list":
        print_hosts(entries)
        return 0

    if args.command == "add":
        if args.name in entries_to_map(entries):
            print(f"名称 '{args.name}' 已存在。")
            return 1
        entries.append(
            HostEntry(
                name=args.name,
                host=args.host,
                user=args.user,
                port=args.port,
                key_file=args.key_file,
                note=args.note,
            )
        )
        store.save(entries)
        print(f"已添加: {args.name}")
        return 0

    if args.command == "remove":
        new_entries = [e for e in entries if e.name != args.name]
        if len(entries) == len(new_entries):
            print(f"未找到服务器: {args.name}")
            return 1
        store.save(new_entries)
        print(f"已删除: {args.name}")
        return 0

    if args.command == "connect":
        connect_host(entries, args.name)
        return 0

    return 1


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    store = HostStore()

    if args.command:
        return run_non_interactive(args, store)

    interactive_menu(store)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
