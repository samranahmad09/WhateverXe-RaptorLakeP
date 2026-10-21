# WhateverXe-RaptorLakeP

[日本語](README_ja.md)

A fork of [WhateverXe](https://github.com/b00t0x/WhateverXe) working toward macOS support for Intel
**Raptor Lake-P** integrated graphics: the UHD and Iris Xe GPUs in 13th-generation Core H, P and U laptop
processors, including the laptop's internal (eDP) panel.

> **Status: the internal panel lights natively and the GPU accelerator runs on it (Metal); validation continues.**
> - Since 2026-10-20 the native Display 13 framebuffer path drives the reference machine's eDP panel (link
>   training, 60 Hz mode, cursor, WindowServer desktop) with the `-wex-rplp-gt` boot argument, and the native
>   accelerator starts on it: the Metal, OpenGL and video plugins attach, WindowServer renders through the GPU,
>   and About This Mac reports 1536 MB for the iGPU.
> - That build is `rplp-accel`: upstream 0.3.0 merged (one binary for Big Sur through Tahoe; the MMIO cache
>   bridges on the 20G1443 kernel bound to the BAR page range) plus the fork's panel path. `main` follows it.
> - Display sleep and wake work (the interrupt re-arm at the pipe's activation boundary).
> - OpenGL applications start since 2026-10-21: the accelerator's hardware-capabilities reply presents Tiger Lake
>   GT2 (9A49) to the prepatched user-mode OpenGL and video drivers, whose device lists end at Alder Lake-N
>   (`-wex-rplp-nocaps` keeps the physical ID), and Chess draws its 3D board. The picture still flickers while the
>   board rotates and the application feels slow; that is the open OpenGL item. Not validated yet: Metal workloads
>   beyond the desktop, hardware video decode (a Safari video plays; the decode path is not yet confirmed), system
>   sleep, brightness control (a backlight PWM probe changed nothing visible on this panel), 144 Hz, external
>   outputs. The findings describe each step.
> - The read-only `-wex-rplp-observe` and the one-shot `-wex-rplp-gpu` self-test remain available.
> - Raptor Lake-P support is experimental and tested on one machine.
> - Everything upstream supports (Tiger Lake, Alder Lake-N / Twin Lake) behaves exactly as in upstream.
> - Hardware findings so far: [docs/raptor-lake-p-findings.md](docs/raptor-lake-p-findings.md).

This is an independent hobby fork, not affiliated with the upstream author. Work that holds up on real
hardware will be offered upstream. `main` follows the newest `rplp-*` branch.

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
- **Internal panel.** This branch has an experimental Raptor Lake-P eDP boot-handoff path.
  Native desktop operation, panel power sequencing, backlight and link retraining still need hardware validation.
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
| R2 | GT backend: the existing BCS self-test, then the accelerator fixes with this GPU's URB and L3 values | The self-test passes and the accelerator starts on the firmware framebuffer | Done on the reference machine: the BCS self-test passes and, since 2026-10-20, the native accelerator starts on the internal panel (branch `rplp-accel`) |
| R3 | Internal panel, first light: keep the donor's internal-panel connector and adopt the link the firmware trained | Desktop on the internal panel at the firmware mode | Done on the reference machine (2026-10-20): native desktop on the panel at 1920x1080, 60 Hz. Power sequencing, backlight and retraining are R4 |
| R4 | Internal panel, complete: power sequencing, backlight, blank/unblank, link retraining with ADL-P tables, 144 Hz | Brightness, sleep/wake and the native refresh rate work | Not started |
| R5 | Metal on the internal panel and stability; later USB-C outputs | WindowServer is accelerated on the panel | In progress: WindowServer is accelerated on the panel since 2026-10-20; stability, sleep/wake and workloads are unverified |

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

This project is experimental and requires the signed graphics package from
[WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics). It installs
the graphics stack under `/Library` and updates the Auxiliary Kernel Collection
(AuxKC), without patching System files or snapshots. WEX and the graphics package
have independent release versions.

The sections below are upstream's, changed only where this fork differs. They apply to the stages that
load Apple's drivers (R2 onward).

### Compatibility

One WEX binary and one graphics pkg cover the following macOS versions:

| macOS | Required version |
| --- | --- |
| Big Sur | 11.7.11 |
| Monterey | 12.7.6 |
| Ventura | 13.7.8 |
| Sonoma | 14.8.9 |
| Sequoia | 15.8.x |
| Tahoe | 26.7 or newer |

Big Sur through Sonoma require the qualified kernel identities. Sequoia and
Tahoe accept servicing updates within their OS family while retaining runtime
instruction and ABI checks. Future updates are not prevalidated.

WEX selects the OS ABI and GPU backend independently. OS-specific graphics
payloads and the common AppleIntelTGLCompat provider are installed by WhateverXe
Graphics; GPU generation and connector routing are handled by WEX.

| GPU family | Device IDs with an implemented backend |
| --- | --- |
| Tiger Lake | `9A40`, `9A49`, `9A78` |
| Alder Lake-N / Twin Lake | `46D0`–`46D4` |

Raptor Lake-P (`A7A8` revision 4) is admitted only with `-wex-rplp-gt`; see the [plan](#plan) above.

Support varies by system and connector layout. The table includes implemented
backends, not a guarantee for every machine. See [device selection](docs/device-selection.md)
for detailed selection conditions and overrides.

### Required components

Download the **RELEASE** ZIP from [Releases](https://github.com/b00t0x/WhateverXe/releases).
The DEBUG ZIP is for troubleshooting. Each ZIP contains only `WhateverXe.kext`
and its matching dSYM. `SHA256SUMS` contains checksums for both archives.
See [CHANGELOG](CHANGELOG.md) for release notes.

This fork publishes CI builds only, as described above; upstream's releases are on
[b00t0x/WhateverXe Releases](https://github.com/b00t0x/WhateverXe/releases). The signed graphics package has not
been tried on the reference machine, which still runs the earlier Xe root patch from the OCLP fork.

| Component | Role |
| --- | --- |
| Lilu | Runtime required by WhateverXe |
| WhateverXe.kext | Load through OpenCore after Lilu |
| [WhateverXe Graphics pkg](https://github.com/b00t0x/WhateverXe-Graphics/releases) | Install the signed graphics stack into `/Library` and rebuild AuxKC |
| [AMFIPass v1.4.1 Mod](https://github.com/b00t0x/WhateverXe-Graphics/blob/main/extras/AMFIPass-v1.4.1-Mod-RELEASE.zip) | Load through OpenCore for the modified graphics stack |

Configure SIP with `csr-active-config=03080000` (value `0x803`). Full SIP is not
supported. With the signed graphics package and linked Mod AMFIPass,
`amfi=0x80` and `ipc_control_port_options=0` are not required.

Tahoe requires `CoreMedia / AllowOnePassMetalScaling` to be disabled; the
WhateverXe Graphics installer applies this setting.

Boot the target OS with WEX **0.3.0 or newer** before installing the graphics
pkg. If an older Xe System patch is installed, restore it using its original
recovery procedure first. The pkg does not change EFI, System files, snapshots
or SIP settings. See [installation and removal](https://github.com/b00t0x/WhateverXe-Graphics#install)
for the package instructions and the standalone uninstall script.

Extract `AMFIPass.kext` from the linked **Mod** archive, place it in
`EFI/OC/Kexts`, and enable it in `Kernel > Add`. Replace an existing AMFIPass
with this Mod build; do not load both copies. Upstream AMFIPass does not support
the Mod signing chain.

If you use OCLP for other root patches, use the
[custom OCLP build](https://github.com/b00t0x/OpenCore-Legacy-Patcher), which uses
the same Mod signing chain. Xe graphics installation itself uses WhateverXe
Graphics independently of OCLP.

WhateverGreen and WhateverXe must not be enabled together, including in
mixed-GPU configurations.

### Configuration

GPU selection is automatic from the PCI device ID. Connector routing is read
from firmware VBT where supported; fixed HDMI and fixed DisplayPort layouts
normally need no manual wiring properties. Advanced `device-id` overrides are
described in [device selection](docs/device-selection.md).

On supported GPUs, WhateverXe also normalizes the detected Intel Management
Engine device name to `IMEI`, as WhateverGreen does. A separate ACPI rename
from `HECI` or `MEI` to `IMEI` is unnecessary.

#### DVMT

Set **DVMT Pre-Allocated to 96MB** in the BIOS/UEFI settings where possible.

If it is fixed at **60MB**, add `-igfxdvmt` to `boot-args` and set both properties
below on the iGPU under OpenCore's `DeviceProperties > Add`
(usually `PciRoot(0x0)/Pci(0x2,0x0)`).

| Property | Type | Value (hex) |
| --- | --- | --- |
| `framebuffer-patch-enable` | Data | `01000000` |
| `framebuffer-stolenmem` | Data | `0000C003` |

These Data values use little-endian byte order. `-igfxdvmt` corrects the capacity
calculation, while the properties limit the display memory pool to fit within
60MB. Neither setting increases the memory allocated by the firmware.
With 96MB, these DVMT-specific settings are unnecessary.

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

No family-selection or per-feature enable arguments are required for
Alder Lake-N / Twin Lake.

#### Optional boot arguments

| Argument | Purpose |
| --- | --- |
| `gfxrst=` | Boot framebuffer handoff: 0 automatic (default), 1 reset, 2 copy, 3 clear, 4 disable. |
| `-wexdbg` | Enable debug logging in a debug build. |
| `-wexoff` | Disable WhateverXe. |
| `-wexbeta` | Bypass the plugin's OS-version check. Exact kernel/driver checks still apply. |
| `-wex-tgl-off` | Disable the Tiger Lake path. |
| `-wex-noaccel` | Tiger Lake framebuffer-only mode; blocks accelerator probing. |
| `-wex-rplp-observe` | Raptor Lake-P only: publish the read-only R1 diagnostic described above. The GPU stays unsupported. |
| `-wex-rplp-gpu` | Raptor Lake-P only, with `-wex-rplp-observe`: run the one-shot R2 BCS self-test described above. The GPU stays unsupported. |
| `-wex-rplp-gt` | Raptor Lake-P A7A8 revision 4 only: admit the device to the in-development backend (native panel path and accelerator). |
| `-wex-rplp-pwm` | Raptor Lake-P only, with `-wex-rplp-gt`: once the desktop appears, write the display engine's backlight PWM duty to 70 % (the firmware leaves about 35 %) and record it. Tells whether the PWM pin drives this panel. |
| `-wex-rplp-nocaps` | Raptor Lake-P only: keep the physical device ID in the accelerator's hardware-caps reply. By default that reply presents Tiger Lake GT2 (9A49) so the prepatched OpenGL and video user-mode drivers, whose device lists lack A7A8, admit the GPU; Metal is unaffected either way. |

### Tested features and known issues

Tiger Lake and Alder Lake-N have been tested across all six OS families with
dual external 1920×1080 at 60 Hz output, Metal/MPS, OpenGL, OpenCL, hardware
H.264 encode/decode, hardware HEVC decode, display sleep/wake, Safari hardware
video decode and Widevine playback. The common TGLCompat build has also passed
installation and functional checks on Alder Lake-N across all six OS families.
These checks do not establish compatibility with every codec, application or GPU
in the table.

#### Known issues and limits

- VA image conversion can crash with a null-pointer access.
- DisplayPort reconnection can cause a black screen or system hang, including
  on Sequoia and Tahoe.
- Occasional Safari frame drops have been observed.
- Three simultaneous displays, system sleep, higher resolutions and other
  connector layouts require further validation. USB-C and internal display
  support are planned below.

### Upstream roadmap

#### Planned

- Alder Lake and Raptor Lake S/H/P/U support
- USB-C display output
- Internal displays

#### Not Planned

- Xe-LPG support
- Xe2 and later architectures

### Build and test

Use macOS with Xcode command-line tools, Python 3.9 or newer, MacKernelSDK and
Lilu's development headers/library. CI uses the Lilu
[1.7.2 release tag](https://github.com/acidanthera/Lilu/tree/1.7.2),
which includes the Tahoe kernel definition. Place `MacKernelSDK/` and `Lilu.kext/` in the
repository root, or pass their paths with `--sdk` and `--lilu`.

```sh
python3 tools/build.py --release
python3 tools/test.py
```

WhateverXe and AppleIntelTGLCompat kexts and a source manifest are written to
`build/`. TGLCompat source and shared headers are maintained here; its signed
binary is distributed through WhateverXe Graphics. The WEX release ZIP contains
only the OpenCore plugin, not the graphics package. Optional donor replay tests
accept `--donor /path/to/AppleIntelTGLGraphicsFramebuffer` and
`--accelerator /path/to/AppleIntelTGLGraphics`, using the exact binaries accepted
by each test. CPU tests do not establish physical GPU compatibility.
WhateverGreen source is not a build dependency.
This fork's CI runs the same commands on every push to `main` and on manual dispatch for the `rplp-*`
branches, and uploads the packaged builds.

### License and credits

WhateverXe retains WhateverGreen's copyright notices and BSD-3-Clause license.
See [LICENSE.txt](LICENSE.txt). This fork's additions use the same license. Lilu and MacKernelSDK are
separate dependencies.
