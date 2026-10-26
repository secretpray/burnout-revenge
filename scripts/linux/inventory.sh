#!/usr/bin/env bash
# Read-only guest/hardware inventory. Software Vulkan is not hardware evidence.
set -u
export LC_ALL=C
uname -a
cat /etc/os-release
getconf GNU_LIBC_VERSION
getconf PAGESIZE
lscpu
free -h
df -h .
for cmd in clang clang++ cmake ninja pkg-config; do
    command -v "$cmd" >/dev/null && "$cmd" --version | head -1
done
if [[ $(uname -m) == x86_64 ]]; then
    /lib64/ld-linux-x86-64.so.2 --help | sed -n '/Subdirectories of glibc-hwcaps/,$p'
fi
if command -v vulkaninfo >/dev/null; then
    vulkaninfo --summary
    vulkaninfo 2>/dev/null | grep -E 'VK_EXT_external_memory_host|maxStorageBufferRange|shaderInt64|timelineSemaphore|hostQueryReset|bufferDeviceAddress'
else
    echo 'Vulkan capabilities UNKNOWN: install vulkan-tools and the system ICD.'
fi
printf 'Session: %s; display: %s; wayland: %s\n' "${XDG_SESSION_TYPE:-unknown}" "${DISPLAY:-unset}" "${WAYLAND_DISPLAY:-unset}"
