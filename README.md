# WhateverXe-RaptorLakeP

[日本語](README_ja.md)

A fork of [WhateverXe](https://github.com/b00t0x/WhateverXe) working toward macOS support for Intel
**Raptor Lake-P** integrated graphics: the UHD and Iris Xe GPUs in 13th-generation Core H, P and U laptop
processors, including the laptop's internal (eDP) panel.

> **Status: not a working driver yet.**
> - Today this fork adds two opt-in diagnostics: the read-only `-wex-rplp-observe`, which reports the GPU's
>   configuration and the firmware's display state, and `-wex-rplp-gpu`, a one-shot GPU command self-test.
> - Raptor Lake-P GPUs remain unsupported.
> - Everything upstream supports (Tiger Lake, Alder Lake-N / Twin Lake) behaves exactly as in upstream.
> - Hardware findings so far: [docs/raptor-lake-p-findings.md](docs/raptor-lake-p-findings.md).

This is an independent hobby fork, not affiliated with the upstream author. Work that holds up on real
hardware will be offered upstream, so the `rplp-*` branches carry no fork-only changes. `main` adds this
README on top.

## Target hardware

| | |
| --- | --- |
| GPU family | Raptor Lake-P (Linux i915 `RAPTORLAKE_P` / `RAPTORLAKE_U`). Device IDs `A7A0`, `A7A1`, `A7A8`, `A7A9`, `A7AA`–`A7AD` |
| Processors | 13th-generation Intel Core **H**, **P** and **U** laptop CPUs, e.g. i7-13620H, i7-1360P, i5-1335U |
| GPU core | Xe-LP (Gen12), the same architecture as Tiger Lake's Iris Xe. Up to 96 EU depending on the SKU |
| Display engine | Display version 13 (XE_LPD), the same generation as Alder Lake-N, which upstream already drives |
| Not targeted | Raptor Lake-S desktop and **HX** laptop CPUs (`A780`–`A78B`). They use display version 12 and need a different path |
| Reference machine | ASUS TUF Gaming F15 FX507VI: i7-13620H, UHD Graphics `A7A8` rev 04 (64 EU), 1920×1080 144 Hz eDP panel. Its NVIDIA RTX 4070 sits behind a MUX and is not used by this fork |

## Why a `device-id` override is not enough

- **GT profile.** Upstream's Alder Lake-N profile accepts at most 2 DSS / 32 EU. The reference GPU has
  4 DSS / 64 EU and needs its own L3 and URB values.
- **Stepping.** Revision 04 (GT C0 / display E0 in Linux's tables) needs its own workaround review.
- **Internal panel.** WhateverXe drives fixed HDMI and DisplayPort outputs. eDP (panel power sequencing,
  backlight, link training) is not implemented for any platform yet.
- **Outputs.** On the reference machine, as on many Raptor Lake-H laptops, the Intel GPU's only outputs are
  the eDP panel and USB-C DisplayPort; HDMI is wired to the NVIDIA GPU. Upstream drives neither on display
  version 13 yet, so the internal panel (R3) is the first possible picture.
- **Platform.** Big Sur boots on the reference machine with OpenCore's CPUID spoof, `ProvideCurrentCpuInfo`
  and Booter `SetupVirtualMap` enabled. Without `SetupVirtualMap`, this firmware hangs during the hand-off to
  macOS, at `EXITBS:START`.

## Plan

Each stage follows upstream's approach: observe first, then qualify, then enable, with CPU tests for
every hardware rule.

| Stage | Goal | Done when | Status |
| --- | --- | --- | --- |
| R1 | Read-only Raptor Lake-P profile: fuses, EU layout and the firmware's display state, with no writes beyond a balanced forcewake | The macOS readout matches a Linux i915 capture from the same machine | Done: on the reference machine the macOS readout matches the Linux capture (all five fuses, 64 EU, 4 DSS) |
| R2 | GT backend: the existing BCS self-test, then the accelerator fixes with this GPU's URB and L3 values | The self-test passes and the accelerator starts on the firmware framebuffer | In progress: the BCS self-test (`-wex-rplp-gpu`) is written and CI-tested; hardware run pending. The accelerator start depends on R3, because the donor's accelerator registers only after a Display 13 framebuffer controller starts |
| R3 | Internal panel, first light: keep the donor's internal-panel connector and adopt the link the firmware trained | Desktop on the internal panel at the firmware mode | Not started |
| R4 | Internal panel, complete: power sequencing, backlight, blank/unblank, link retraining with ADL-P tables, 144 Hz | Brightness, sleep/wake and the native refresh rate work | Not started |
| R5 | Metal on the internal panel and stability; later USB-C outputs | WindowServer is accelerated on the panel | Not started |

The hardware inputs for R2-R4 are measured on the reference machine and listed in
[docs/raptor-lake-p-findings.md](docs/raptor-lake-p-findings.md):
- the GT profile values (64 EU, 6 L3 banks, 384 KB URB, Alder Lake-N media fuse);
- the eDP link the firmware leaves running (1920x1080 at 144 Hz, HBR x4, PSR off);
- the outputs;
- the backlight: the embedded controller owns it, although the firmware leaves the Intel PWM running.

## Trying the R1 diagnostic

**What it does.** The diagnostic runs only with `-wex-rplp-observe`, and only on a physical Raptor Lake-P
GPU.
- It reads the GPU fuses (slice, DSS, EU, L3, media), the Display 13 boot state, and the eDP-related
  registers (panel power, backlight, DP link, PSR).
- It publishes them in the IORegistry.
- The only register writes are a balanced forcewake request and release, the same handshake upstream uses
  on Alder Lake-N. It does not run if anything already holds forcewake.
- The GPU stays unsupported.

**Requirements.** Lilu and a DEBUG WhateverXe from this fork. The OCLP root patch and AMFIPass are not
needed for this step.

**macOS.** WhateverXe declares Big Sur, so R1 runs on any Big Sur build; acceleration later needs 11.7.11
(20G1443). On any other macOS version, add `-wexbeta` for this read-only step only, and remove it afterwards.
R1 also runs from the Big Sur recovery when OpenCore injects Lilu and WhateverXe. The recovery's Terminal has
`ioreg` and `dmesg`; run them without `sudo`.

**Builds.**
- Go to [Actions](https://github.com/samranahmad09/WhateverXe-RaptorLakeP/actions/workflows/ci.yml), open
  the latest successful run on `main`, and download **Artifacts**. It contains `WhateverXe-…-DEBUG.zip`.
- These builds share upstream's version number, so keep them apart from upstream releases.

**Collecting.**

```sh
ioreg -lw0 -r -k RPLPObserveVersion > ~/Desktop/rplp-ioreg.txt
sudo dmesg | grep -E 'RaptorLakeP|WhateverXe' > ~/Desktop/rplp-dmesg.txt
```

**Decoding.**
- `python3 tools/rplp-decode.py rplp-ioreg.txt` turns the readout into named values.
- Add `--linux <capture>` to compare it with a Linux reference capture of the same machine, made with the
  read-only [`tools/rplp-capture.sh`](tools/rplp-capture.sh) from a Ubuntu live session.

**Published properties.**
- `RPLPGTReadout`, `RPLPDisplay13Boot`, `RPLPEDPBoot`
- `RPLPPhysicalMemoryLayout`, `RPLPStolenReservation`
- Physical identity, revision, and the GT and display stepping names

## Trying the R2 GPU self-test

**What it does.** With `-wex-rplp-gpu` added to `-wex-rplp-observe`, the diagnostic submits one blitter (BCS)
command batch through the execlist queue and checks that the GPU wrote two known values to memory. It is
upstream's Alder Lake-N self-test, run at a Raptor Lake-P lease.
- It runs only on revision 4 with the fuse set measured on the reference machine (4 DSS, 64 EU, L3 `0x88`,
  media `0x000E00FE`), with physical DMA and the firmware's 1:1 stolen-memory GGTT.
- It leases 16 GGTT pages at 32 MiB, inside stolen memory and clear of the firmware's plane, and saves the
  old entries.
- It writes those PTEs and BCS engine registers, and may enable PCI bus mastering. After any engine write,
  its pages stay pinned until reboot.
- It needs no Apple driver, so it also runs from the Big Sur recovery.

**Reading it.** `tools/rplp-decode.py` prints the lease and the result, and ends with `R2 BCS self-test
verdict`. The properties are `RPLPGPUAdmission`, `RPLPGPULease` and `RPLPGPUCommandTest`.

## Related work (outside this repository)

This fork is the main route of a hobby effort to get graphics working on the reference laptop. There are
two other tracks, and neither is part of this repository.

- **NVIDIA dGPU, an optional experiment.**
  - In the laptop's MUX "dGPU only" mode, the RTX 4070 drives the panel directly.
  - [null-moth's NVIDIA driver](https://github.com/nullmoth/nvidia-macos-driver) (macOS Sequoia, PolyForm
    Noncommercial) lists this GPU. Laptop pieces such as backlight control are still stubs there.
- **An Intel stack without Apple binaries, long-term research.**
  - The idea: Mesa's ANV Vulkan driver, a new Intel kernel driver, and an existing Metal-on-Vulkan layer.
  - It would follow the pattern of [Navi48-MacOS](https://github.com/Almosst-DEV/Navi48-MacOS) for AMD and
    null-moth's Metal plugin.
  - Exploratory only; there is no code yet.

---

## Upstream WhateverXe documentation

WhateverXe is a Lilu plugin that adapts Apple graphics drivers for selected Intel Xe-LP integrated GPUs. It
is derived from WhateverGreen.

It is experimental and requires the Xe root patch provided by the
[OpenCore Legacy Patcher fork](https://github.com/b00t0x/OpenCore-Legacy-Patcher).

The sections below are upstream's, changed only where this fork differs. They apply to the stages that
load Apple's drivers (R2 onward).

### Compatibility

The current target is **macOS Big Sur build 20G1443**.

| GPU family | Device IDs with an implemented backend |
| --- | --- |
| Tiger Lake | `9A40`, `9A49`, `9A78` |
| Alder Lake-N / Twin Lake | `46D0`–`46D4` |

Raptor Lake-P has no backend in this fork yet; see the [plan](#plan) above.

Support varies by system and connector layout. The table includes implemented backends, not a guarantee
for every machine. See [device selection](docs/device-selection.md) for detailed selection conditions and
overrides.

**What has been tested upstream.**
- On Tiger Lake and Alder Lake-N: dual external 1920×1080 at 60 Hz output, Metal/OpenGL, hardware HEVC, and
  display sleep/wake.
- On Tiger Lake: hardware H.264 encode/decode as well.
- Still needing validation: three simultaneous displays, system sleep, higher resolutions and other
  connector layouts.
- USB-C and internal display support are on upstream's roadmap.

Upstream releases are on
[b00t0x/WhateverXe Releases](https://github.com/b00t0x/WhateverXe/releases). This fork publishes CI builds
only, as described above.

### Required components

| Component | Role |
| --- | --- |
| Lilu | Runtime required by WhateverXe |
| WhateverXe.kext | Load through OpenCore after Lilu |
| [OpenCore Legacy Patcher fork](https://github.com/b00t0x/OpenCore-Legacy-Patcher) | Apply its Xe root patch to install the graphics stack |
| [AMFIPass v1.4.1 Mod](https://github.com/b00t0x/OpenCore-Legacy-Patcher/blob/mod/payloads/Kexts/Acidanthera/AMFIPass-v1.4.1-Mod-RELEASE.zip) | Install through OpenCore for the modified root-patched graphics stack |

Download the OCLP fork from its [Releases page](https://github.com/b00t0x/OpenCore-Legacy-Patcher/releases).
OCLP installs its root components and updates the kernel collection.

Extract `AMFIPass.kext` from the linked **Mod** archive, place it in `EFI/OC/Kexts`, and enable it in
`Kernel > Add`. Replace an existing AMFIPass with this Mod build; do not load both copies.

WhateverGreen and WhateverXe must not be enabled together, including in mixed-GPU configurations.

### Configuration

GPU selection is automatic from the PCI device ID.
- Where supported, connector routing is read from the firmware's VBT.
- Fixed HDMI and fixed DisplayPort layouts normally need no manual wiring properties.
- Advanced `device-id` overrides are described in [device selection](docs/device-selection.md).

#### DVMT

Set **DVMT Pre-Allocated to 96MB** in the BIOS/UEFI settings where possible.

If it is fixed at **60MB**, add `-igfxdvmt` to `boot-args`. Also set both properties below on the iGPU under
OpenCore's `DeviceProperties > Add` (usually `PciRoot(0x0)/Pci(0x2,0x0)`).

| Property | Type | Value (hex) |
| --- | --- | --- |
| `framebuffer-patch-enable` | Data | `01000000` |
| `framebuffer-stolenmem` | Data | `0000C003` |

- These Data values use little-endian byte order.
- `-igfxdvmt` corrects the capacity calculation, and the properties limit the display memory pool to fit
  within 60MB.
- Neither setting increases the memory allocated by the firmware.
- With 96MB, these DVMT-specific settings are unnecessary.

#### Required boot arguments

Add the following to OpenCore's `boot-args`:

```text
-allow3d -disablegfxfirmware
```

| Argument | Purpose |
| --- | --- |
| `-allow3d` | Enable the Apple driver's 3D path. |
| `-disablegfxfirmware` | Disable the Apple driver's graphics-firmware loading path for the current backend. |
| `-wex-nopanel` | Additionally required on Tiger Lake systems without an internal display. Alder Lake-N / Twin Lake currently builds an external-display topology automatically and does not use this flag. |

No family-selection or per-feature enable arguments are required for Alder Lake-N / Twin Lake.

#### Optional boot arguments

| Argument | Purpose |
| --- | --- |
| `gfxrst=` | Boot framebuffer handoff: 0 automatic (default), 1 reset, 2 copy, 3 clear, 4 disable. |
| `-wexdbg` | Enable debug logging in a debug build. |
| `-wexoff` | Disable WhateverXe. |
| `-wexbeta` | Bypass the plugin's OS-version check. Exact kernel/driver checks still apply. |
| `-wex-tgl-off` | Disable the Tiger Lake path. |
| `-wex-rplp-observe` | Raptor Lake-P only: publish the read-only R1 diagnostic described above. The GPU stays unsupported. |
| `-wex-rplp-gpu` | Raptor Lake-P only, with `-wex-rplp-observe`: run the one-shot R2 BCS self-test described above. The GPU stays unsupported. |

### Upstream roadmap

#### Planned

- macOS Monterey through Tahoe support using OCLP's capabilities
- Alder Lake and Raptor Lake S/H/P/U support
- USB-C display output
- Internal displays

#### Not Planned

- Xe-LPG support
- Xe2 and later architectures

### Build and test

You need macOS with the Xcode command-line tools, Python 3.9 or newer, MacKernelSDK, and Lilu's development
headers and library. Place `MacKernelSDK/` and `Lilu.kext/` in the repository root, or pass their paths with
`--sdk` and `--lilu`.

```sh
python3 tools/build.py --release
python3 tools/test.py
```

- Kexts and a source manifest are written to `build/`.
- Optional donor replay tests accept `--donor /path/to/AppleIntelTGLGraphicsFramebuffer` and
  `--accelerator /path/to/AppleIntelTGLGraphics`, using the exact binaries each test accepts.
- CPU tests do not establish physical GPU compatibility.
- WhateverGreen source is not a build dependency.
- This fork's CI runs the same commands on every push to `main` and uploads the packaged builds.

### License and credits

WhateverXe retains WhateverGreen's copyright notices and BSD-3-Clause license; see
[LICENSE.txt](LICENSE.txt). This fork's additions use the same license. Lilu and MacKernelSDK are separate
dependencies.
