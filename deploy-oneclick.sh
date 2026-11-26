#!/usr/bin/env bash
# 一键部署 CardputerZero Metronome 到真机：
#   构建 ARM64 deb -> scp -> sudo apt install -> SSH 运行并回传日志
#
# 用法：
#   ./deploy-oneclick.sh [user@ip] [--debug] [--install-only] [--no-build] [--dry-run]
#   未给 user@ip 时会交互询问；密码永不作为命令行参数，可用 DEVICE_PASSWORD 环境变量预置。
set -euo pipefail

usage() {
  cat <<'EOF'
用法: ./deploy-oneclick.sh [user@ip] [选项]

选项:
  --debug          安装后不直接运行，而是推 Debug 二进制并在设备起 gdbserver :1234
  --install-only   只安装，不启动 app
  --no-build       跳过 cmake 构建，复用 dist/ 里最新的 deb
  --dry-run        只打印要执行的远程命令，不真的连接（也不需要密码）
  -h, --help       显示帮助

示例:
  ./deploy-oneclick.sh pi@192.168.1.10
  ./deploy-oneclick.sh pi@192.168.1.10 --debug
  DEVICE_PASSWORD=xxxx ./deploy-oneclick.sh pi@192.168.1.10 --install-only

安全提示: 密码通过 SSH 加密通道发送，但在脚本进程环境中可见；
          日常使用更推荐配置 SSH key 后改用仓库自带的 deploy.sh。
EOF
}

die() { printf '错误: %s\n' "$*" >&2; exit 1; }
log() { printf '\n==> %s\n' "$*"; }

TARGET=""
DEBUG=0
INSTALL_ONLY=0
NO_BUILD=0
DRY_RUN=0

while (( $# > 0 )); do
  case "$1" in
    --debug) DEBUG=1 ;;
    --install-only) INSTALL_ONLY=1 ;;
    --no-build) NO_BUILD=1 ;;
    --dry-run) DRY_RUN=1 ;;
    -h|--help) usage; exit 0 ;;
    -*) die "未知选项: $1" ;;
    *)
      [[ -z "$TARGET" ]] || die "只能指定一个 user@ip 参数"
      TARGET="$1"
      ;;
  esac
  shift
done

if [[ -z "$TARGET" ]]; then
  read -r -p "设备地址 (user@ip): " TARGET
fi
if [[ "$TARGET" == *@* ]]; then
  REMOTE_USER="${TARGET%%@*}"
  REMOTE_HOST="${TARGET#*@}"
else
  REMOTE_HOST="$TARGET"
  read -r -p "设备用户名: " REMOTE_USER
fi
[[ -n "$REMOTE_USER" && -n "$REMOTE_HOST" ]] || die "用户名和 IP 都不能为空"

if (( DRY_RUN == 0 )); then
  if [[ -z "${DEVICE_PASSWORD:-}" ]]; then
    read -s -r -p "设备密码: " DEVICE_PASSWORD
    printf '\n'
  fi
  [[ -n "$DEVICE_PASSWORD" ]] || die "密码不能为空"
  export DEVICE_PASSWORD
  command -v expect >/dev/null 2>&1 || die "缺少 expect（macOS 自带 /usr/bin/expect）"
fi
command -v ssh >/dev/null 2>&1 || die "缺少 ssh"
command -v scp >/dev/null 2>&1 || die "缺少 scp"

# cmake/ninja 有时只装在 Python 用户目录而不在 PATH 中；找到就补进来
prepare_path() {
  if ! command -v cmake >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
    local py_base py_cmake_bin
    py_base="$(python3 -m site --user-base 2>/dev/null || true)"
    if [[ -n "$py_base" && -d "$py_base/bin" ]]; then
      export PATH="$py_base/bin:$PATH"
    fi
    py_cmake_bin="$(python3 -c 'import cmake,os; print(os.path.join(os.path.dirname(cmake.__file__),"data","bin"))' 2>/dev/null || true)"
    if [[ -n "$py_cmake_bin" && -x "$py_cmake_bin/cmake" ]]; then
      export PATH="$py_cmake_bin:$PATH"
    fi
  fi
}
prepare_path

can_build() {
  command -v cmake >/dev/null 2>&1 &&
  command -v ninja >/dev/null 2>&1 &&
  command -v aarch64-linux-gnu-gcc >/dev/null 2>&1
}

SSH_OPTS="-o StrictHostKeyChecking=accept-new -o PreferredAuthentications=password -o PubkeyAuthentication=no -o ConnectTimeout=10"

# 非交互远程命令：自动应答 ssh / sudo 的密码提示，等到命令结束
remote() {
  local cmd="$1"
  if (( DRY_RUN )); then printf '[dry-run] %s\n' "$cmd" >&2; return 0; fi
  CP0_CMD="$cmd" expect <<'EXPECT_EOF'
set timeout -1
log_user 1
spawn sh -c $env(CP0_CMD)
expect {
    -re {[Aa]re you sure you want to continue connecting.*\?} { send -- "yes\r"; exp_continue }
    -re {[Pp]assword( for [^:]*)?:} { send -- "$env(DEVICE_PASSWORD)\r"; exp_continue }
    eof
}
catch wait result
exit [lindex $result 3]
EXPECT_EOF
}

# 交互远程命令：应答完密码后把终端交给用户（可以按键、Ctrl-C 退出）
remote_interactive() {
  local cmd="$1"
  if (( DRY_RUN )); then printf '[dry-run] %s\n' "$cmd" >&2; return 0; fi
  CP0_CMD="$cmd" expect <<'EXPECT_EOF'
set timeout 10
set eof_flag 0
log_user 1
spawn sh -c $env(CP0_CMD)
while {1} {
    expect {
        -re {[Aa]re you sure you want to continue connecting.*\?} { send -- "yes\r"; set timeout 1 }
        -re {[Pp]assword( for [^:]*)?:} { send -- "$env(DEVICE_PASSWORD)\r"; set timeout 1 }
        -re {[^\r\n]+} { set timeout 2 }
        timeout { break }
        eof { set eof_flag 1; break }
    }
}
if {!$eof_flag} { interact }
catch wait result
exit [lindex $result 3]
EXPECT_EOF
}

# 1) 构建并打包（工具链不全时直接用已有 deb）
if (( NO_BUILD == 0 && DRY_RUN == 0 )); then
  if can_build; then
    log "构建 ARM64 Debian 包 (cp0-cross-package)"
    cmake --workflow --preset cp0-cross-package
  else
    log "本机缺少 cmake/ninja/aarch64 交叉工具链，跳过构建，使用 dist/ 里已有的 deb"
  fi
fi

PACKAGE_GLOB="${PACKAGE_GLOB:-dist/metronome_*_arm64.deb}"
shopt -s nullglob
packages=( ${PACKAGE_GLOB} )
shopt -u nullglob
(( ${#packages[@]} > 0 )) || die "没有找到匹配的 deb 包: ${PACKAGE_GLOB}"
latest_package="$(ls -t "${packages[@]}" | head -n 1)"
package_name="$(basename "$latest_package")"
log "使用包: ${latest_package}"

# 2) 上传到设备
log "上传 ${package_name} -> ${REMOTE_USER}@${REMOTE_HOST}:/tmp/"
remote "scp ${SSH_OPTS} -q '${latest_package}' '${REMOTE_USER}@${REMOTE_HOST}:/tmp/${package_name}'"

# 3) 远程安装
log "在设备上安装 (sudo apt install)"
remote "ssh -tt ${SSH_OPTS} '${REMOTE_USER}@${REMOTE_HOST}' \"export DEBIAN_FRONTEND=noninteractive; sudo -S apt install --no-install-recommends -y /tmp/${package_name}\""

# 4) 调试模式：推 Debug 二进制并启动 gdbserver
if (( DEBUG )); then
  debug_binary="build/cp0-cross/Debug/metronome"
  debug_target="/usr/bin/metronome"
  if (( NO_BUILD == 0 && DRY_RUN == 0 )) && can_build; then
    log "构建 Debug 二进制"
    cmake --build --preset cp0-cross-dbg
  fi
  if [[ -f "$debug_binary" || $DRY_RUN -eq 1 ]]; then
    debug_target="/tmp/metronome-dbg"
  else
    log "没有 Debug 二进制，gdbserver 将调试已安装的 /usr/bin/metronome（Release）"
  fi

  log "安装 gdbserver"
  remote "ssh ${SSH_OPTS} '${REMOTE_USER}@${REMOTE_HOST}' \"export DEBIAN_FRONTEND=noninteractive; sudo -S apt install -y gdbserver\""
  if [[ "$debug_target" == "/tmp/metronome-dbg" ]]; then
    remote "scp ${SSH_OPTS} -q '${debug_binary}' '${REMOTE_USER}@${REMOTE_HOST}:/tmp/metronome-dbg'"
    remote "ssh ${SSH_OPTS} '${REMOTE_USER}@${REMOTE_HOST}' 'chmod +x /tmp/metronome-dbg'"
  fi

  log "启动 gdbserver :1234"
  remote "ssh ${SSH_OPTS} '${REMOTE_USER}@${REMOTE_HOST}' \"sudo -S pkill -x metronome 2>/dev/null || true; sudo -S pkill -x metronome-dbg 2>/dev/null || true; sudo -S pkill -x gdbserver 2>/dev/null || true; nohup gdbserver :1234 ${debug_target} >/tmp/metronome-gdbserver.log 2>&1 &\""
  printf '\n主机端连接:\n  lldb %s\n  (lldb) gdb-remote %s:1234\n' "$debug_target" "$REMOTE_HOST"
  printf '设备端日志: ssh %s@%s cat /tmp/metronome-gdbserver.log\n' "$REMOTE_USER" "$REMOTE_HOST"
  exit 0
fi

# 5) 直接运行，日志实时回传
if (( INSTALL_ONLY == 0 )); then
  log "停止已有实例（避免 framebuffer 冲突）"
  remote "ssh ${SSH_OPTS} '${REMOTE_USER}@${REMOTE_HOST}' \"sudo -S pkill -x metronome 2>/dev/null || true\""

  log "启动 app；日志实时打印，按键可操作，Ctrl-C 退出"
  remote_interactive "ssh -tt ${SSH_OPTS} '${REMOTE_USER}@${REMOTE_HOST}' '/usr/bin/metronome'"
else
  printf '\n安装完成。设备端运行: /usr/bin/metronome\n'
fi
