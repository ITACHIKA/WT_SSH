# WT SSH/SCP Managers (`wtssh` and `scpm`)

Cross-platform C++17 text UI tools for OpenSSH connections and SCP uploads.

## Features

- Save SSH hosts, ports, usernames, key paths, and notes.
- Store optional passwords in the operating system's native credential vault.
- Configure one or more per-host OpenSSH local (`-L`) forwarding rules.
- Per-host X11 forwarding: `off`, untrusted `-X`, or trusted `-Y`.
- Set `DISPLAY` only in the spawned SSH process.
- Edit existing hosts and automatically migrate the original six-column database format.
- Launch OpenSSH without an intermediate command shell (`CreateProcessW` on Windows and `posix_spawnp` on POSIX).
- Upload a local file with `scpm` by selecting any host already stored by `wtssh`.
- Reuse the same host database, private-key settings, and native-vault passwords in both programs.

Passwords are never written to `hosts.db`, command-line arguments, or child environment variables. The SSH child receives only a credential record ID and obtains the password through OpenSSH's `SSH_ASKPASS` protocol.

## Credential backends

`WTSSH_CREDENTIAL_BACKEND` is a CMake cache setting with these values:

| Value | Platform and behavior |
| --- | --- |
| `auto` / `native` | Windows Credential Manager on Windows, Keychain on macOS, libsecret on Linux when available |
| `windows` | Force Windows Credential Manager |
| `keychain` | Force macOS Keychain |
| `libsecret` | Force Linux Secret Service through libsecret |
| `none` | Build without password storage; key and interactive authentication still work |

The default is `auto`. An explicitly selected backend fails configuration if its platform or dependency is unavailable. On Linux, `auto` falls back to `none` with a CMake warning when libsecret is missing.

Typical Linux dependencies:

```bash
# Debian / Ubuntu
sudo apt install cmake ninja-build g++ pkg-config libsecret-1-dev

# Fedora
sudo dnf install cmake ninja-build gcc-c++ pkgconf-pkg-config libsecret-devel
```

The native vault records are local to each operating system and are not copied with `hosts.db`:

- Windows: generic credential `wtssh/<entry-id>`
- macOS: generic-password Keychain item with service `wtssh`
- Linux: Secret Service item using schema `org.wtssh.Password`

## Build

### Platform build scripts

Windows PowerShell:

```powershell
.\scripts\build-windows.ps1
```

Linux:

```bash
bash scripts/build-linux.sh
```

macOS:

```bash
bash scripts/build-macos.sh
```

All scripts default to `Release`, the `auto` credential backend, Ninja, and running tests. Common examples:

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -CredentialBackend windows
.\scripts\build-windows.ps1 -Clean
.\scripts\build-windows.ps1 -SkipTests
```

```bash
bash scripts/build-linux.sh --config Debug --backend libsecret
bash scripts/build-linux.sh --clean
bash scripts/build-macos.sh --backend keychain --no-tests
```

`--clean`/`-Clean` only removes build directories beneath the repository's `build` directory.

### Manual CMake build

Auto-select the native backend:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DWTSSH_CREDENTIAL_BACKEND=auto
cmake --build build
```

PowerShell uses a backtick for line continuation, or put the command on one line:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWTSSH_CREDENTIAL_BACKEND=auto
cmake --build build
```

Examples of explicit selection:

```bash
cmake -S . -B build -DWTSSH_CREDENTIAL_BACKEND=libsecret
cmake -S . -B build-no-passwords -DWTSSH_CREDENTIAL_BACKEND=none
```

Show the backend compiled into the executable:

```bash
wtssh --credential-backend
scpm --credential-backend
```

## Run

```bash
wtssh
wtssh --list
wtssh --connect my-server
```

Host configuration is stored in `~/.wt_ssh_manager/hosts.db` (`%USERPROFILE%\.wt_ssh_manager\hosts.db` on Windows).

### Upload files with `scpm`

Start the SCP TUI, select a saved server, and press `C` or `Enter`:

```bash
scpm
```

The program asks for:

1. One or more local file paths. You can select multiple files in Explorer and drag them into the terminal; quoted paths separated by spaces are parsed as separate files.
2. The remote destination path, such as `~/uploads/` or `/tmp/report.csv`.

The destination is built from the selected host, for example:

```text
scp -P 2222 -- "report one.csv" "report two.csv" alice@example.com:~/uploads/
```

For multiple files, use a remote directory as the destination. A single existing path containing spaces is also accepted without quotes when entered manually. `scpm` uploads regular files only; directory upload is intentionally not enabled. It reads the same `hosts.db` and credential record IDs as `wtssh`, but does not modify them. Add, edit, or delete hosts in the SSH manager. X11 and local-forwarding settings are ignored during SCP transfers.

For scripts or direct invocation:

```bash
scpm --list
scpm --send my-server "local report.csv" "~/uploads/report.csv"
scpm --send my-server "report one.csv" "report two.csv" "~/uploads/"
```

SCP uses uppercase `-P` for the SSH server port. As with ordinary `scp`, specifying an existing remote filename may overwrite that remote file.

## Keyboard controls

- `Up` / `Down`: move selection
- `A`: add server
- `E`: edit server, password, local forwarding, and X11 settings
- `D`: delete server and its saved credential
- `C` / `Enter`: connect
- `Q`: quit

## X11 requirements

On Linux and macOS, an existing `DISPLAY` is used as the default. On Windows, start an X server such as VcXsrv or Xming; `localhost:0.0` is a common value. The manager supplies this value only to the local SSH client. OpenSSH sets the remote `DISPLAY` automatically.

The remote SSH server must permit X11 forwarding and normally needs `xauth`. Prefer `-X`; use `-Y` only for trusted remote systems.

## Local SSH forwarding

When adding or editing a host, enter one or more TCP forwarding rules in OpenSSH's `-L` form:

```text
[bind_address:]local_port:destination_host:destination_port
```

Separate multiple rules with a semicolon. Examples:

```text
127.0.0.1:5433:db.internal:5432;8080:127.0.0.1:80
```

The first rule exposes the remote-side `db.internal:5432` at local `127.0.0.1:5433`. The second exposes the SSH server's own port 80 at local port 8080. Each rule is passed as a separate `-L` argument, and `ExitOnForwardFailure=yes` makes the connection fail if SSH cannot establish the requested forwarding. Omit the bind address to use OpenSSH's default local loopback binding; avoid `0.0.0.0` or `*` unless other machines should be able to reach the forwarded port.

## Password behavior and limitations

Saved-password mode restricts OpenSSH to the `password` authentication method with one attempt. It intentionally does not automatically answer keyboard-interactive/MFA challenges. Use `auto` mode for interactive MFA or key authentication where possible.

Native credential vaults protect passwords at rest for the logged-in user, but software already running as that user may be able to request the same credentials. Secret buffers are cleared after use.

## Tests and diagnostics

```bash
ctest --test-dir build --output-on-failure
wtssh --self-test-credential
```

The Windows test suite covers both SSH launching and SCP uploads using fake client executables, so it does not contact real servers.

The credential self-test creates, reads, and immediately removes a randomly named credential. Test-only environment overrides are:

- `WTSSH_DATA_DIR`: configuration directory
- `WTSSH_SSH_PATH`: SSH executable path
