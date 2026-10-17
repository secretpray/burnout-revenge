#!/bin/bash
# The SteamOS launcher's hybrid-graphics part (scripts/steamos/bundle/
# burnout-revenge.sh: nvidia_icd and prime_offload, run alone) on made-up /sys,
# /proc and Vulkan driver folders. Registered by tests/CMakeLists.txt, or:
#   bash tests/launcher_prime_test.sh scripts/steamos/bundle/burnout-revenge.sh
# Each case: the PCI display devices (vendor, device, class, driver), whether
# NVIDIA's kernel driver is loaded (/proc/driver/nvidia), where NVIDIA's Vulkan
# driver manifest is, and what the user set; then whether
# __NV_PRIME_RENDER_OFFLOAD reaches the game and what the log says. The
# launcher never sets VK_DRIVER_FILES or __GLX_VENDOR_LIBRARY_NAME, and leaves
# a value the user set as it is.
set -u
LAUNCHER=${1:?usage: launcher_prime_test.sh <burnout-revenge.sh>}
[ -f "$LAUNCHER" ] || { echo "no $LAUNCHER"; exit 1; }
# The two functions, as the launcher has them (each from "name() {" to its "}").
FUNCS="$(awk '/^(nvidia_icd|prime_offload)\(\) \{$/ { on = 1 } on { print } on && /^\}$/ { on = 0 }' "$LAUNCHER")"
case "$FUNCS" in *"nvidia_icd() {"*"prime_offload() {"*) ;; *) echo "FAIL: nvidia_icd and prime_offload not found in $LAUNCHER"; exit 1 ;; esac

T="$(mktemp -d "${TMPDIR:-/tmp}/launcher_prime_test.XXXXXX")" || exit 1
trap 'rm -rf "$T"' EXIT
failures=0
cases=0

# gpu <root> <slot> <vendor> <device> <class> <driver or ->
gpu() {
    local d="$1/sys/bus/pci/devices/$2"
    mkdir -p "$d" "$1/sys/bus/pci/drivers"
    echo "$5" > "$d/class"; echo "$3" > "$d/vendor"; echo "$4" > "$d/device"
    if [ "$6" != - ]; then
        mkdir -p "$1/sys/bus/pci/drivers/$6"
        ln -s "../../../../bus/pci/drivers/$6" "$d/driver"
    fi
}
nvidia_loaded() {
    mkdir -p "$1/proc/driver/nvidia"
    printf 'NVRM version: NVIDIA UNIX Open Kernel Module for x86_64  580.82.07  Release Build  (dvs-builder@U16-I3-B17-1-1)  Wed Aug 27 23:23:00 UTC 2025\nGCC version:  gcc version 14.2.0\n' \
        > "$1/proc/driver/nvidia/version"
}
icd() {  # icd <root> <dir> <name>
    mkdir -p "$1$2/vulkan/icd.d"
    echo '{ "file_format_version": "1.0.1", "ICD": { "library_path": "libGLX_nvidia.so.0", "api_version": "1.4.312" } }' > "$1$2/vulkan/icd.d/$3"
}
# PR #6's laptop: Intel Raptor Lake-P iGPU + RTX 3050 Laptop GPU (a "3D controller"), NVIDIA's HDMI audio beside it.
laptop() {
    gpu "$1" 0000:00:02.0 0x8086 0xa7a0 0x030000 i915
    gpu "$1" 0000:01:00.0 0x10de 0x25a2 0x030200 nvidia
    gpu "$1" 0000:01:00.1 0x10de 0x228e 0x040300 snd_hda_intel
    gpu "$1" 0000:00:1f.3 0x8086 0x51ca 0x040380 snd_hda_intel
}

# run <name> <root> [VAR=value ...]: the functions in a clean environment (HOME=/home, inside the root; no XDG_*, no
# NVIDIA or Vulkan variables but those given; uname -m says x86_64, as on the x86-64 bundle's machines).
# Sets OUT (the log) and ENVS (the variables the game would get).
run() {
    local name=$1 root=$2; shift 2
    mkdir -p "$root/home"
    OUT="$(env -i PATH="$PATH" HOME=/home "$@" "$BASH" -c '
        say() { echo "[launcher] $*"; }
        uname() { [ "${1:-}" = -m ] && echo x86_64 || command uname "$@"; }
        eval "$1"
        prime_offload "$2"
        echo "ENV __NV_PRIME_RENDER_OFFLOAD=${__NV_PRIME_RENDER_OFFLOAD-<unset>} VK_DRIVER_FILES=${VK_DRIVER_FILES-<unset>}" \
            "VK_ICD_FILENAMES=${VK_ICD_FILENAMES-<unset>} __GLX_VENDOR_LIBRARY_NAME=${__GLX_VENDOR_LIBRARY_NAME-<unset>}"
    ' bash "$FUNCS" "$root" 2>&1)"
    ENVS="$(echo "$OUT" | sed -n 's/^ENV //p')"
    OUT="$(echo "$OUT" | grep -v '^ENV ')"
    cases=$((cases + 1))
    printf '%s\n  log: %s\n  env: %s\n' "$name" "${OUT:-(none)}" "$ENVS"
}
check() {  # check <description> <shell test...>
    local what=$1; shift
    if ! "$@"; then failures=$((failures + 1)); echo "  FAIL: $what"; fi
}
has() { case "$1" in *"$2"*) return 0 ;; *) return 1 ;; esac; }
offload() { has "$ENVS" "__NV_PRIME_RENDER_OFFLOAD=$1 "; }
untouched() { has "$ENVS" "VK_DRIVER_FILES=${1:-<unset>} " && has "$ENVS" "__GLX_VENDOR_LIBRARY_NAME=<unset>" && has "$ENVS" "VK_ICD_FILENAMES=<unset>"; }

# 1. Our devices: one GPU, no NVIDIA. Nothing set; one line naming the GPU (none on the Frame).
R=$T/steam-machine; gpu $R 0000:03:00.0 0x1002 0x7481 0x030000 amdgpu
run "Steam Machine (RADV NAVI33)" $R
check "nothing set" offload "<unset>"; check "VK/GLX untouched" untouched
check "the line" [ "$OUT" = "[launcher] graphics: AMD 1002:7481 (amdgpu)" ]
R=$T/deck; gpu $R 0000:04:00.0 0x1002 0x1435 0x030000 amdgpu
run "Steam Deck OLED (VANGOGH)" $R
check "nothing set" offload "<unset>"; check "the line" [ "$OUT" = "[launcher] graphics: AMD 1002:1435 (amdgpu)" ]
R=$T/frame; mkdir -p $R/sys/bus/pci/devices
run "Steam Frame (Adreno: not on PCI)" $R
check "nothing set" offload "<unset>"; check "no line" [ -z "$OUT" ]

# 2. PR #6's laptop, NVIDIA's driver loaded, its manifest in each place the loader looks.
for place in /usr/share /etc /usr/local/share /etc/xdg; do
    R=$T/laptop$(echo $place | tr / _); laptop $R; nvidia_loaded $R; icd $R $place nvidia_icd.json
    run "Intel + NVIDIA, manifest in $place/vulkan/icd.d" $R
    check "offload on" offload 1; check "VK/GLX untouched" untouched
    check "the line" [ "$OUT" = "[launcher] graphics: Intel 8086:a7a0 (i915), NVIDIA 10de:25a2 (nvidia); NVIDIA driver 580.82.07 beside another GPU; NVIDIA's Vulkan driver $place/vulkan/icd.d/nvidia_icd.json; __NV_PRIME_RENDER_OFFLOAD=1 (PRIME render offload: NVIDIA's layer lists its GPU first)" ]
done
R=$T/fedora; laptop $R; nvidia_loaded $R; icd $R /usr/share nvidia_icd.x86_64.json
run "Intel + NVIDIA, Fedora's nvidia_icd.x86_64.json" $R
check "offload on" offload 1; check "the manifest named" has "$OUT" "/usr/share/vulkan/icd.d/nvidia_icd.x86_64.json"
R=$T/i686only; laptop $R; nvidia_loaded $R; icd $R /usr/share nvidia_icd.i686.json
run "Intel + NVIDIA, only the 32-bit manifest" $R
check "not the 32-bit one" offload "<unset>"; check "says why" has "$OUT" "no NVIDIA Vulkan driver (nvidia_icd.json) where the Vulkan loader looks"
R=$T/nixos; laptop $R; nvidia_loaded $R; icd $R /run/opengl-driver/share nvidia_icd.x86_64.json
run "Intel + NVIDIA, NixOS (XDG_DATA_DIRS)" $R XDG_DATA_DIRS=/run/opengl-driver/share:/usr/share
check "offload on" offload 1; check "from XDG_DATA_DIRS" has "$OUT" "/run/opengl-driver/share/vulkan/icd.d/nvidia_icd.x86_64.json"
R=$T/userdata; laptop $R; nvidia_loaded $R; icd $R /home/.local/share nvidia_icd.json
run "Intel + NVIDIA, manifest in ~/.local/share" $R
check "offload on" offload 1; check "from HOME" has "$OUT" "/home/.local/share/vulkan/icd.d/nvidia_icd.json"
R=$T/amd-nvidia; gpu $R 0000:65:00.0 0x1002 0x15bf 0x030000 amdgpu; gpu $R 0000:01:00.0 0x10de 0x28e0 0x030000 nvidia
nvidia_loaded $R; icd $R /usr/share nvidia_icd.json
run "AMD integrated + NVIDIA" $R
check "offload on" offload 1; check "both named" has "$OUT" "NVIDIA 10de:28e0 (nvidia), AMD 1002:15bf (amdgpu)"

# 3. Not hybrid, or no NVIDIA driver: nothing set.
R=$T/nomanifest; laptop $R; nvidia_loaded $R
run "Intel + NVIDIA, no manifest anywhere" $R
check "nothing set" offload "<unset>"; check "says why" has "$OUT" "no PRIME render offload"
R=$T/desktop; gpu $R 0000:01:00.0 0x10de 0x2786 0x030000 nvidia; nvidia_loaded $R; icd $R /usr/share nvidia_icd.json
run "NVIDIA desktop (one GPU)" $R
check "nothing set" offload "<unset>"; check "the line" [ "$OUT" = "[launcher] graphics: NVIDIA 10de:2786 (nvidia)" ]
R=$T/nouveau; gpu $R 0000:00:02.0 0x8086 0xa7a0 0x030000 i915; gpu $R 0000:01:00.0 0x10de 0x25a2 0x030200 nouveau
icd $R /usr/share nvidia_icd.json
run "Intel + NVIDIA on nouveau" $R
check "nothing set" offload "<unset>"
R=$T/notloaded; laptop $R; icd $R /usr/share nvidia_icd.json
run "Intel + NVIDIA, /proc/driver/nvidia missing" $R
check "nothing set" offload "<unset>"
R=$T/vfio; gpu $R 0000:00:02.0 0x8086 0xa7a0 0x030000 vfio-pci; gpu $R 0000:01:00.0 0x10de 0x25a2 0x030000 nvidia
nvidia_loaded $R; icd $R /usr/share nvidia_icd.json
run "NVIDIA + an Intel GPU passed to a VM (vfio-pci)" $R
check "nothing set" offload "<unset>"
R=$T/unbound; gpu $R 0000:00:02.0 0x8086 0xa7a0 0x030000 -; gpu $R 0000:01:00.0 0x10de 0x25a2 0x030000 nvidia
nvidia_loaded $R; icd $R /usr/share nvidia_icd.json
run "NVIDIA + an Intel GPU with no driver" $R
check "nothing set" offload "<unset>"; check "named without a driver" has "$OUT" "Intel 8086:a7a0 (no driver)"
R=$T/audio; gpu $R 0000:01:00.0 0x10de 0x2786 0x030000 nvidia; gpu $R 0000:01:00.1 0x10de 0x22bc 0x040300 snd_hda_intel
gpu $R 0000:00:1f.3 0x8086 0x7a50 0x040300 snd_hda_intel; nvidia_loaded $R; icd $R /usr/share nvidia_icd.json
run "NVIDIA desktop + Intel audio (not a GPU)" $R
check "nothing set" offload "<unset>"; check "audio not listed" [ "$OUT" = "[launcher] graphics: NVIDIA 10de:2786 (nvidia)" ]

# 4. The user's values win; the launcher adds no others.
R=$T/user0; laptop $R; nvidia_loaded $R; icd $R /usr/share nvidia_icd.json
run "Intel + NVIDIA, __NV_PRIME_RENDER_OFFLOAD=0 set" $R __NV_PRIME_RENDER_OFFLOAD=0
check "kept 0" offload 0; check "says so" has "$OUT" "__NV_PRIME_RENDER_OFFLOAD=0 was already set: left as it is"
run "Intel + NVIDIA, __NV_PRIME_RENDER_OFFLOAD= (empty) set" $R __NV_PRIME_RENDER_OFFLOAD=
check "kept empty" offload ""
run "Intel + NVIDIA, VK_DRIVER_FILES set" $R VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.x86_64.json
check "offload on" offload 1; check "VK_DRIVER_FILES kept" untouched /usr/share/vulkan/icd.d/intel_icd.x86_64.json

echo
if [ $failures = 0 ]; then echo "all passed ($cases cases)"; else echo "$failures failure(s) in $cases cases"; fi
[ $failures = 0 ]
