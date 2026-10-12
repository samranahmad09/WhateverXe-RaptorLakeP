#!/usr/bin/env bash
# rplp-capture.sh - read-only Linux reference capture for Raptor Lake-P macOS work (WhateverXe-RaptorLakeP).
#
# Run it from an Ubuntu 26.04 "Try Ubuntu" live session, with the laptop's GPU mode set to Standard (panel on the
# Intel GPU, NVIDIA card still visible), booted with these kernel parameters:
#     i915.enable_guc=0 drm.debug=0x1e log_buf_len=16M
#
# Usage (from the USB stick this file is on):
#     cd /media/ubuntu/<STICK-NAME>
#     sudo bash rplp-capture.sh
#
# What it does: READS kernel logs, i915 debugfs, GPU/display registers, Vulkan capabilities, the panel EDID
# and the ACPI tables, then writes ONE file rpl-capture-<time>.tar.gz next to this script.
# What it never does: write to any GPU register, change any setting, or touch the internal SSD.
# Privacy: dmidecode serial numbers / UUIDs are redacted and the MSDM ACPI table (Windows product key) is skipped.

set -u

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run it with sudo:  sudo bash $0"
    exit 1
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
STAMP="$(date +%Y%m%d-%H%M%S)"
NAME="rpl-capture-$STAMP"
OUT="/tmp/$NAME"                     # work in RAM; FAT32/exFAT sticks cannot hold some file names
mkdir -p "$OUT"
LOG="$OUT/capture.log"

say() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$LOG"; }
run() {                              # run <output file> <command...>; never stops the script
    local f="$1"; shift
    echo "\$ $*" >> "$LOG"
    timeout 180 "$@" > "$f" 2>&1 || echo "(command exited with $?)" >> "$f"
}

# ---------------------------------------------------------------------------------------------------------
say "1/10 kernel log (taken first, before anything else adds to it)"
dmesg > "$OUT/dmesg.txt" 2>&1
journalctl -k -b --no-pager > "$OUT/journal-kernel.txt" 2>&1 || true
cat /proc/cmdline > "$OUT/cmdline.txt"
cat /sys/module/i915/parameters/enable_guc > "$OUT/i915-enable_guc.txt" 2>&1 || echo "i915 not loaded" > "$OUT/i915-enable_guc.txt"
case "$(cat /proc/cmdline)" in
    *i915.enable_guc=0*) say "    kernel parameters OK (i915.enable_guc=0 present)";;
    *) say "    WARNING: i915.enable_guc=0 is missing from the kernel command line. Reboot with it on the kernel command line if you can.";;
esac

# ---------------------------------------------------------------------------------------------------------
say "2/10 installing read-only tools into the live session (needs internet; skipped if offline)"
have_tools() {
    for t in intel_reg intel_vbt_decode vulkaninfo glxinfo edid-decode lspci dmidecode; do
        command -v "$t" > /dev/null 2>&1 || return 1
    done
    ls /usr/share/vulkan/icd.d/intel_icd*.json > /dev/null 2>&1
}
# A live USB is slow and the desktop's own updater often holds the apt lock for minutes, so a slow
# "apt-get update" is not proof of being offline: probe the network separately and wait for the lock.
if have_tools; then
    say "    all tools already present; nothing to install"
elif ! timeout 20 getent hosts archive.ubuntu.com > /dev/null 2>&1; then
    say "    no internet: continuing with the tools already on the live system"
else
    APT="apt-get -o DPkg::Lock::Timeout=600"
    add-apt-repository -y universe >> "$LOG" 2>&1 || true
    timeout 900 $APT update >> "$LOG" 2>&1 || say "    apt-get update did not finish; trying the install anyway"
    DEBIAN_FRONTEND=noninteractive timeout 1200 $APT install -y \
        intel-gpu-tools vulkan-tools mesa-vulkan-drivers mesa-utils edid-decode pciutils dmidecode >> "$LOG" 2>&1 \
        || say "    some packages failed to install; continuing with what is available"
fi

# ---------------------------------------------------------------------------------------------------------
say "3/10 system and PCI information"
run "$OUT/uname.txt" uname -a
run "$OUT/os-release.txt" cat /etc/os-release
run "$OUT/lscpu.txt" lscpu
dmidecode -t bios -t system -t baseboard -t processor 2>&1 \
    | sed -E 's/((Serial Number|UUID|Asset Tag|SKU Number):).*/\1 <redacted>/' > "$OUT/dmidecode.txt"
run "$OUT/lspci-nnvvv.txt" lspci -nnvvv
run "$OUT/lspci-tree.txt" lspci -tvnn

# ---------------------------------------------------------------------------------------------------------
say "4/10 i915 debugfs (display state, power wells, DBUF, CDCLK, DMC, GuC, topology, VBT ...)"
mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug 2>/dev/null
I915=""
for d in /sys/kernel/debug/dri/*; do
    if [ -f "$d/name" ] && grep -q i915 "$d/name" 2>/dev/null; then I915="$d"; break; fi
done
if [ -z "$I915" ]; then
    say "    WARNING: no i915 debugfs directory found (on a MUX laptop, is the panel switched to the other GPU?)"
else
    say "    found $I915 : $(cat "$I915/name")"
    mkdir -p "$OUT/debugfs"
    ( cd "$I915" && find . -maxdepth 4 -type f ) | sort | while read -r f; do
        case "$f" in
            *crc/data*|*i915_forcewake_user*) continue;;   # CRC data blocks; forcewake_user takes a wake reference
        esac
        mkdir -p "$OUT/debugfs/$(dirname "$f")"
        timeout 10 head -c 33554432 "$I915/$f" > "$OUT/debugfs/$f" 2>/dev/null || true
    done
    cp "$I915/i915_vbt" "$OUT/vbt.bin" 2>/dev/null || say "    WARNING: could not read i915_vbt"
fi

# ---------------------------------------------------------------------------------------------------------
say "5/10 register snapshot (intel_reg READ only)"
if command -v intel_reg > /dev/null 2>&1; then
    mkdir -p "$OUT/regs"
    run "$OUT/regs/intel_reg-dump.txt" intel_reg dump
    # 0x9134 EU disable, 0x9138 slice enable, 0x913C DSS enable, 0x9118 mirror fuse 3 (L3 banks), 0x9140 media engine fuses
    run "$OUT/regs/fuses.txt" intel_reg read 0x9134 0x9138 0x913C 0x9118 0x9140
    addrs() { local a=$(( $1 )) e=$(( $2 )) l=""; while [ "$a" -le "$e" ]; do l="$l $(printf '0x%X' "$a")"; a=$(( a + 4 )); done; echo "$l"; }
    rng() {   # rng <name> <first> <last>
        # shellcheck disable=SC2046
        timeout 60 intel_reg read $(addrs "$2" "$3") > "$OUT/regs/$1.txt" 2>&1 || true
    }
    # --- GT ---
    rng gt_misc_mocs           0x4000   0x40FC     # GEN12 global MOCS, ERROR_GEN6 0x40A0
    rng rcs_ring               0x2000   0x23FC     # RCS ring/execlist/CSB registers
    rng gt_chicken_7000        0x7000   0x73FC     # COMMON_SLICE_CHICKEN* etc.
    rng gt_fuse_pg             0x9000   0x91FC     # fuses, power gating
    rng gt_pm_a000             0xA000   0xA2FC     # RC6 / RPS / forcewake requests
    rng gt_forcewake_media     0xA540   0xA56C
    rng gt_forcewake_acks      0x0D50   0x0D8C
    rng gt_lncf_mocs           0xB020   0xB09C
    rng gt_mcr_e000            0xE000   0xE5FC     # multicast (steered) registers
    rng gt_irq                 0x190000 0x1900FC   # GEN11 GT interrupt registers
    run "$OUT/regs/gt_singles.txt" intel_reg read 0xFDC 0x130044 0x2580 0x20E0 0x9424
    # --- display ---
    rng de_irq                 0x44200  0x4447C
    rng dbuf_ctl_s3_s4         0x44300  0x4430C
    rng dbuf_ctl_s1_s2         0x44FE0  0x4500C
    rng power_wells            0x45400  0x4547C
    rng dc_states              0x45500  0x4552C
    rng cdclk_dpll             0x46000  0x4607C
    rng cpu_backlight          0x48250  0x4825C
    rng transcoder_a           0x60000  0x600FC
    rng trans_ddi_a            0x60400  0x6043C
    rng ddi_a_aux              0x64000  0x6402C
    rng pipe_scalers_a         0x68100  0x682FC
    rng pipe_a_planes          0x70000  0x707FC     # pipe A, cursor, planes 1..7 (CTL/STRIDE/SURF/WM/BUF_CFG)
    rng dmc_8f000              0x8F000  0x8F0FC
    rng combo_phy_a            0x162000 0x16207C
    rng sde_hotplug            0xC4000  0xC403C
    rng pch_pps                0xC7200  0xC721C     # panel power sequencer
    rng pch_backlight          0xC8250  0xC825C
    # --- everything WhateverXe's -wex-rplp-observe readout reads, so the two can be diffed ---
    rng trans_a_dp_tp          0x60500  0x6054C     # TGL_DP_TP_CTL/STATUS A
    rng trans_a_psr            0x60800  0x6084C     # SRD_CTL/STATUS A
    rng trans_a_psr2           0x60900  0x6094C     # PSR2_CTL/STATUS A
    rng ddi_b_c_aux            0x64100  0x6412C     # DDI_BUF_CTL B, AUX B
    rng pch_pps_1              0xC7300  0xC731C
    rng pch_backlight_2        0xC8350  0xC835C
    rng dpclka_dpll_cfgcr      0x164280 0x1642BC
    run "$OUT/regs/observe_singles.txt" intel_reg read 0x42000 0x42084 0x4438C 0x45010 0x45014 \
        0x46010 0x46014 0x64200 0x1082C0 0x1082C4
    run "$OUT/regs/i915-bound.txt" sh -c 'readlink /sys/bus/pci/devices/0000:00:02.0/driver || echo "no driver bound (GOP state)"'
else
    say "    intel_reg not available (no internet?); skipping register snapshot"
fi

# ---------------------------------------------------------------------------------------------------------
say "6/10 Vulkan capabilities of Mesa ANV (the Intel driver only)"
ICD=$(ls /usr/share/vulkan/icd.d/intel_icd*.json /etc/vulkan/icd.d/intel_icd*.json 2> /dev/null | head -n 1)
if command -v vulkaninfo > /dev/null 2>&1 && [ -n "$ICD" ] && [ -f "$ICD" ]; then
    export VK_DRIVER_FILES="$ICD" VK_ICD_FILENAMES="$ICD"
    run "$OUT/vulkaninfo-summary.txt" vulkaninfo --summary
    run "$OUT/vulkaninfo.txt" vulkaninfo
    run "$OUT/vulkaninfo-formats.txt" vulkaninfo --show-formats
    unset VK_DRIVER_FILES VK_ICD_FILENAMES
    {
        echo "== What the Metal-on-Vulkan layer needs from ANV on this GPU =="
        for k in deviceName driverName driverInfo apiVersion VK_EXT_external_memory_host minImportedHostPointerAlignment \
                 shaderInt64 shaderStorageImageReadWithoutFormat shaderStorageImageWriteWithoutFormat \
                 VK_EXT_rasterization_order_attachment_access VK_KHR_dynamic_rendering_local_read; do
            printf '%-45s ' "$k"
            grep -m1 -E "\b$k\b" "$OUT/vulkaninfo.txt" | sed -E 's/^[[:space:]]+//' || true
            grep -q -E "\b$k\b" "$OUT/vulkaninfo.txt" || echo "NOT FOUND"
        done
        echo
        echo "== FORMAT_B8G8R8A8_UNORM (linearTilingFeatures must include SAMPLED, COLOR_ATTACHMENT, TRANSFER_SRC/DST) =="
        grep -n -A40 -m1 'FORMAT_B8G8R8A8_UNORM' "$OUT/vulkaninfo-formats.txt" | sed -n '1,45p'
    } > "$OUT/vulkan-checks.txt" 2>&1
else
    say "    vulkaninfo or the Intel ICD is missing; skipping"
fi
command -v glxinfo > /dev/null 2>&1 && run "$OUT/glxinfo-B.txt" glxinfo -B

# ---------------------------------------------------------------------------------------------------------
say "7/10 panel EDID, modes and backlight"
for c in /sys/class/drm/card*-eDP-*; do
    [ -e "$c/edid" ] || continue
    n="$(basename "$c")"
    cp "$c/edid" "$OUT/edid-$n.bin" 2>/dev/null
    cat "$c/modes" > "$OUT/modes-$n.txt" 2>/dev/null
    command -v edid-decode > /dev/null 2>&1 && edid-decode "$OUT/edid-$n.bin" > "$OUT/edid-$n.txt" 2>&1
done
for b in /sys/class/backlight/*; do
    [ -d "$b" ] && echo "$b max=$(cat "$b/max_brightness") actual=$(cat "$b/actual_brightness")"
done > "$OUT/backlight.txt" 2>&1

# ---------------------------------------------------------------------------------------------------------
say "8/10 ACPI tables (MSDM, the Windows product key table, is skipped)"
mkdir -p "$OUT/acpi"
( cd /sys/firmware/acpi/tables && find . -type f ) | while read -r t; do
    case "$t" in *MSDM*) continue;; esac
    mkdir -p "$OUT/acpi/$(dirname "$t")"
    cp "/sys/firmware/acpi/tables/$t" "$OUT/acpi/$t" 2>/dev/null || true
done

# ---------------------------------------------------------------------------------------------------------
say "9/10 i915 module parameters and firmware"
grep -r . /sys/module/i915/parameters/ > "$OUT/i915-params.txt" 2>&1
ls -l /lib/firmware/i915/ 2>/dev/null | grep -E 'adlp|tgl' > "$OUT/firmware-files.txt"
grep -iE 'firmware|dmc|guc|huc' "$OUT/dmesg.txt" | grep -iE 'i915|xe' | grep -viE 'drm:.*debug' | head -200 > "$OUT/firmware-dmesg.txt"
command -v intel_vbt_decode > /dev/null 2>&1 && [ -f "$OUT/vbt.bin" ] \
    && run "$OUT/vbt-decoded.txt" intel_vbt_decode --file "$OUT/vbt.bin"

say "    NVIDIA dGPU facts for the NVIDIA-driver route (read-only; needs GPU mode Standard, not Eco)"
{
    echo "== NVIDIA PCI functions =="
    lspci -nn -d 10de: 2>&1
    for dev in $(lspci -D -d 10de: 2>/dev/null | awk '/VGA|3D|Display/ {print $1}'); do
        echo
        echo "== $dev: BARs, expansion ROM, Resizable BAR capability, link, driver =="
        lspci -vvv -s "$dev" 2>&1 | grep -E 'Region|Expansion ROM|Resizable BAR|BAR [0-9]:|LnkCap:|LnkSta:|Kernel driver'
    done
    echo
    echo "== where nouveau found the VBIOS (PRAMIN / PROM / ACPI) and GSP status =="
    grep -iE 'nouveau.*(bios|vbios|gsp|firmware)' "$OUT/dmesg.txt" | head -30
    echo
    echo "== ACPI tables containing a _ROM method (VBIOS handed over by the system firmware) =="
    grep -l -a '_ROM' "$OUT"/acpi/* "$OUT"/acpi/*/* 2>/dev/null || echo "none"
} > "$OUT/nvidia-checks.txt" 2>&1

# ---------------------------------------------------------------------------------------------------------
say "10/10 packing"
tar -C /tmp -czf "$HERE/$NAME.tar.gz" "$NAME"
sync
SIZE="$(du -h "$HERE/$NAME.tar.gz" | cut -f1)"
( cd "$HERE" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256" )
sync

echo
echo "=================================================================================================="
echo " DONE.  $HERE/$NAME.tar.gz  ($SIZE)"
echo
echo " Quick look:"
[ -f "$OUT/regs/fuses.txt" ] && sed 's/^/   /' "$OUT/regs/fuses.txt"
[ -f "$OUT/vulkan-checks.txt" ] && sed -n '1,14p' "$OUT/vulkan-checks.txt" | sed 's/^/   /'
echo
echo " Next: copy the .tar.gz somewhere safe. It holds your ACPI tables and VBT; share it only where you choose."
echo "=================================================================================================="
