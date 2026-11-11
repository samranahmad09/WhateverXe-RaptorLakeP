# WhateverXe-RaptorLakeP

[日本語](README_ja.md)

A fork of [WhateverXe](https://github.com/b00t0x/WhateverXe) working toward macOS support for Intel
**Raptor Lake-P** integrated graphics: the UHD and Iris Xe GPUs in 13th-generation Core H, P and U laptop
processors, including the laptop's internal (eDP) panel.

> **Status (2026-11-06): the internal panel lights natively, the GPU accelerator runs on it (Metal and OpenGL), brightness works, and system sleep and wake work (the full test ladder passed on the reference machine with the tested boot arguments; see the sleep bullet).**
> - Since 2026-10-20 the native Display 13 framebuffer path drives the reference machine's eDP panel (link
>   training, 60 Hz mode, cursor, WindowServer desktop) with the `-wex-rplp-gt` boot argument, and the native
>   accelerator starts on it: the Metal, OpenGL and video plugins attach, WindowServer renders through the GPU,
>   and About This Mac reports 1536 MB for the iGPU.
> - The branch is `rplp-accel`: upstream 0.3.0 merged (one binary for Big Sur through Tahoe; the MMIO cache
>   bridges on the 20G1443 kernel bound to the BAR page range) plus the fork's panel path. `main` is refreshed from
>   it after validated rounds (last on 2026-11-06, after round 60); upstream 0.6.1 is fetched and not merged yet.
> - Display sleep and wake work (the interrupt re-arm at the pipe's activation boundary).
> - OpenGL works since 2026-10-28. The accelerator's hardware-capabilities reply presents Tiger Lake GT2 (9A49) to
>   the prepatched user-mode OpenGL and video drivers (since 2026-10-21; `-wex-rplp-nocaps` keeps the physical ID),
>   and the plugin publishes five URB banks to them instead of the measured six (`wex-rplp-urb-banks=5`): the OpenGL
>   driver then ends its vertex URB allocation at 320 KiB instead of 384 KiB, every offscreen readback probe passes,
>   and Chess and WebGL render correctly. The driver's exact allocation rule is a follow-up. Hardware video: Safari
>   playback works; the screen recorder crashes inside the video driver during a pixel transfer. Hiding the renderer
>   key makes it work (`-wex-no-va-renderer`, round 61, at the price of software decoding in Apple's decoder service);
>   the one-byte page correction that keeps the renderer (`-wex-rplp-va-vpp`) did not arm in that round and is
>   retested in round 62.
> - Brightness works through the embedded controller since 2026-10-21: the display engine's PWM duty does not reach
>   this panel, so `-wex-rplp-ecbl` forwards the framebuffer's duty writes to the firmware's WMI brightness method and
>   gives AppleBacklight the generic panel curves. The brightness keys work with two byte patches on the DSDT's key
>   guards and acidanthera's BrightnessKeys, both in the EFI, not in this plugin.
> - **System sleep and wake work since round 60 (2026-11-06).** On the reference machine the full test ladder
>   passed in one boot with the tested boot arguments (below): two system sleeps (the second with Chess and Safari
>   open), a lid sleep woken by the lid, and a seven-minute sleep woken by its own alarm. The entries took 16 to 26
>   seconds, each wake brought the desktop back within a few seconds, and the window server stayed alive through a
>   one-minute soak after each; none of the plugin's certificates refused. The tested set, on top of the daily
>   arguments, is `-wex-rplp-wakelog -wex-rplp-fwguard -wex-rplp-coldinit -wex-rplp-coldphyboot -wex-rplp-auxstanding -wex-rplp-fwwait -wex-rplp-pciwake -wex-rplp-coldcore -wex-rplp-wsstart -wex-rplp-entryirq -wex-rplp-cursorlag` (each is described in the optional-argument table); a smaller set has not
>   been tested, and two of them (`-wex-rplp-fwwait`, `-wex-rplp-pciwake`) changed nothing on their own in their
>   rounds. The work took rounds 43 to 60 (2026-10-29 to 2026-11-06), each fix one step further into the wake after a
>   real power loss: the plugin's power-well restore waited one microsecond for a cold well's fuse (now 2 ms); its
>   engine enable refused the plane-buffer and MBUS registers the power loss resets (now re-established); the two
>   combo PHYs had lost their state (now re-initialised as i915 does, from the firmware's own words); the firmware's
>   standing request on the panel's AUX power well, which the plugin had only borrowed, was the one boot-time request
>   the restore did not recreate (now a standing reference, the policy i915 follows for a DisplayPort encoder's AUX
>   domain); the display core registers a power loss zeroes, among them CHICKEN_MISC_2, whose bit 30 keeps the engine
>   from fetching tiled planes through a page table no macOS driver builds (now restored from the boot values; found
>   through [Thatisjigen/WhateverXe](https://github.com/Thatisjigen/WhateverXe)'s sleep branch); the graphics family
>   never told the window server or the accelerators about the sleep, because the undriven discrete GPU's placeholder
>   framebuffer controller keeps a window-server wait bit set that the family's cached state never recomputes (now
>   cleared, with the cached word, at will-sleep: the two-minute entries and the missing accelerator wake had this one
>   cause); and three refusals of the plugin's own certificates on the second sleep of a boot (a port-only release of
>   a stopped pipe, the ownership of display buffer slices requested back after a power loss, and a watermark latch
>   whose cursor live surface lagged its base), each now handled.
>   Not validated yet: Metal workloads beyond the desktop, external outputs, sleep on battery, sleep at 144 Hz, a
>   smaller set of the sleep arguments. 144 Hz itself runs since round 61 with an EDID override on the iGPU: the
>   panel's 144 Hz timing sits in a DisplayID block the driver never reads, so this is a configuration change, not
>   a plugin one.
> - The read-only `-wex-rplp-observe` and the one-shot `-wex-rplp-gpu` self-test remain available.
> - Raptor Lake-P support is experimental and tested on one machine.
> - Everything upstream supports (Tiger Lake, Alder Lake-N / Twin Lake) behaves exactly as in upstream.
> - Hardware findings so far: [docs/raptor-lake-p-findings.md](docs/raptor-lake-p-findings.md).

This is an independent hobby fork, not affiliated with the upstream author. Work that holds up on real
hardware will be offered upstream. `main` is refreshed from the newest `rplp-*` branch after validated rounds.

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
| R4 | Internal panel, complete: power sequencing, backlight, blank/unblank, link retraining with ADL-P tables, 144 Hz | Brightness, sleep/wake and the native refresh rate work | In progress: brightness since 2026-10-21 (embedded-controller bridge), display sleep/wake done; system sleep/wake since 2026-10-29: four causes fixed in turn, the AUX IO power well after a real power loss fixed 2026-11-04 (a standing AUX reference), the GPU after the wake open (the device's configuration space restored at the power-on notice on trial); 144 Hz and retraining not started |
| R5 | Metal on the internal panel and stability; later USB-C outputs | WindowServer is accelerated on the panel | In progress: WindowServer accelerated on the panel since 2026-10-20, OpenGL correct since 2026-10-28; system sleep/wake in progress (R4); workloads beyond the desktop and USB-C outputs unverified |

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

Raptor Lake-P (`A7A8` revision 4) additionally needs `-wex-rplp-gt`; the reference machine's daily configuration
adds `-wex-rplp-ecbl` (brightness through the embedded controller) and `wex-rplp-urb-banks=5` (OpenGL); and system
sleep and wake were validated (round 60, 2026-11-06) with `-wex-rplp-wakelog -wex-rplp-fwguard -wex-rplp-coldinit -wex-rplp-coldphyboot -wex-rplp-auxstanding -wex-rplp-fwwait -wex-rplp-pciwake -wex-rplp-coldcore -wex-rplp-wsstart -wex-rplp-entryirq -wex-rplp-cursorlag`
on top. Each is described in the table below. A smaller sleep set has not been tested.

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
| `-wex-rplp-capsadln` | Raptor Lake-P only: present Alder Lake-N (46D0) instead of Tiger Lake GT2 in that reply, the other identity those user-mode drivers accept. An experiment for the OpenGL rendering defects; `-wex-rplp-nocaps` wins when both are given. |
| `-wex-rplp-ecbl` | Raptor Lake-P only: brightness through the embedded controller. The framebuffer's backlight PWM duty writes are forwarded as a 0 to 100 level to the firmware's WMI brightness method (the EC drives this panel, not the PWM pin); needs a PNLF device for AppleBacklight, whose panel lookup is then given the generic panel curves (as WhateverGreen's `applbkl` does) so the built-in display gets its brightness slider (`N97RPLPPanelInject`). A visible self-test raises the level for 1.5 s after the desktop appears and records level, maximum and source in `N97RPLPECBacklight`. |
| `-wex-rplp-wakelog` | Raptor Lake-P only, diagnostic: records the system sleep and wake messages, the plugin's submit, software-interrupt and forcewake traffic relative to them (acquisitions and releases apart, with the raw forcewake request and acknowledgement registers and the reference counts read before each call of the wake window), and the accelerator's engine start and every return, as `RPLPWakeLog` on the registry root and `rplp-wakelog` in NVRAM, so a panic during a wake still leaves the sequence behind. The record runs from the sleep message through the wake, so a dispatch at the first instant of a wake is kept even before the plugin hears of the wake. Counts, timestamps and register reads only; nothing is deferred or refused. With the record armed, every power-well restore also starts a four-second trace of the panel port's registers at 25 ms (the panel power sequencer, DPLL0 and the clock select, the DDI buffer, the training pattern and status, the PHY's lane words, the AUX channel, the DDI wells; `N97FBPortTrace`, reads only), and the last power requests before a wake's first DDI release are kept (`N97FBPowerRingAtPutDDI`, `N97FBPortRingAtPutDDI`). The trace also carries the AUX and driver power-well registers. Since r341 the flag also binds IOGraphicsFamily for the record: the family's sleep bookkeeping sampled every three seconds through the sleep entry and the first minute of the wake (`RPLPWakeIOG`), the accelerator's display pipes with their notifiers and timing stamps at fixed points of the cycle (`RPLPWakePipes`), the family's framebuffer notification entries and exits observed through the cycle (`RPLPWakeFBEvents`), and the first forcewake call after which one of the accelerator's reference counts reads negative, with its caller (`RPLPForceWakeUnderflow`). The object offsets are those of the installed IOGraphicsFamily 585.2 and IOAcceleratorFamily2 442.10; nothing is read unless the family's function anchors match, and every pointer is checked before it is followed. |
| `-wex-rplp-coldinit` | Raptor Lake-P only, experimental: before a power-well restore after a real power loss, program the PCH raw clock from the strap, enable the PCH reset handshake and initialise the two combo PHYs the way Linux's i915 does before it requests power well 1 (each PHY is verified first and left alone when its state survived; `N97FBColdPHY`), with i915's Alder Lake-P workarounds around it (the FLR source disabled before the well, the DCPR_2 bits and the fatal error mask after MBUS). The raw clock is written only when its divider field reads zero. Registers are recorded before and after in `N97FBPowerRestoreWait` and `N97FBColdResume`; nothing is written when the registers already agree or read all-ones. On a cold resume it also writes the MBUS credit words (ABOX 0 and 1, each pipe's DBOX) back to the values read at the first engine enable after boot where a power loss reset them (`N97FBCreditBoot`, `N97FBColdMBUSCredits`); Linux's i915 writes the DBOX words in every modeset. |
| `-wex-rplp-fwguard` | Raptor Lake-P only, experimental: before every forcewake call, read the render acknowledgement register and skip the native call when it reads all-ones, a value it cannot legitimately hold. That read means the device is not decoding its registers (early in a wake, before its configuration space is restored); the native acquire passes on it, and its release then waits for a bit that never clears and panics. The reference counts are untouched, so acquire and release stay paired. Skips are counted in the wake record (`-wex-rplp-wakelog`, word 46 and the event flag) and `RPLPForceWakeGuardArmed` says the guard is on. |
| `-wex-rplp-auxlane` | Raptor Lake-P only, experiment: set the combo PHY's AUX lane enable bit (CL_DW12 bit 0) after a combo AUX power request and clear it once the release has taken, the sequence Linux's i915 uses on Ice Lake and not on this generation, so it is an experiment for a wake after a real power loss whose AUX well gave no acknowledgement, not an established requirement. `N97FBAuxLane` records the last operation; without the flag the register is only read into `N97FBAcquireWait`. |
| `-wex-rplp-coldpps` | Raptor Lake-P only, experimental: on a wake after a real power loss, before the native framebuffer's own work, rewrite the panel power sequencer's delays (PP_ON_DELAYS, PP_OFF_DELAYS and the power cycle delay field of PP_CONTROL) from the values read at the first engine enable after boot, only when both delay registers read zero, the way Linux's i915 reprograms them on resume. The power-on, VDD, backlight and reset bits stay the native path's. `N97FBColdPPS` records the outcome; `N97FBPortBoot` and `N97FBPortCold` the port's registers at boot and after the restores; `N97FBPowerRing` and `N97FBPortRing` the last power requests with the port's state at each. |
| `-wex-rplp-coldpll` | Raptor Lake-P only, experimental: on a wake after a real power loss, re-enable the port PLL the firmware had selected for DDI A (DPLL0 or DPLL1) with the configuration read at the first engine enable after boot, then select and ungate the port's clock, in the order Linux's i915 uses (power enable and its state, the configuration registers, enable and lock, the clock select and the clock-off bit in separate writes); only when that PLL reads neither enabled nor locked. `N97FBColdPLL`. |
| `-wex-rplp-coldphyboot` | Raptor Lake-P only, experimental: with `-wex-rplp-coldinit`, initialise a combo PHY that lost its state with the firmware's own words read at the first engine enable after boot (`N97FBPHYFirmware`: the procmon reference values in place of Linux's table, and two common-lane bit fields a power loss clears, CL_DW5's clock configuration and CL_DW10's low bits, which Linux's i915 writes in its DP enable; CL_DW12's upper bits read as status and are recorded only) instead of the table alone. `N97FBColdPHY` records what was written and what read back. On this machine the firmware's values equal the table's, so the flag changes the two bit fields only. |
| `-wex-rplp-auxstanding` | Raptor Lake-P only, experimental: hold a standing reference on the boot port's AUX IO power well from the boot handoff on, taken over the request the firmware left set, so that the power-well restore after a real power loss recreates that request (`N97FBAUXStanding`). Without it the native driver's per-transaction requests toggle the well physically after a power loss, and on this machine the well never acknowledged again once it had powered down during a wake's link bring-up, while at boot the firmware's standing request kept it up through the same transactions (round 50). Linux's i915 holds the AUX power domain of a DisplayPort encoder for as long as its link is up. Round 51: with it the link trained and the panel lit after a real power loss. |
| `-wex-rplp-fwwait` | Raptor Lake-P only, experimental: when a regular forcewake call finds the device not decoding its registers (the render acknowledgement register reads all-ones, as at the first instant of a wake before the device's configuration space is restored), wait for it, 100 microseconds at a time and half a second at most, and then let the call proceed; past the bound the guard (`-wex-rplp-fwguard`) decides. `RPLPForceWakeWait` records the waits. Round 51: the accelerator's first work of a wake, 4 ms after the power-on notice, went to such a device, and the GPU executed nothing afterwards. Round 52: the wait happened (8 ms, no skip) and changed nothing. The GT's registers are recorded with the forcewake domains held through the accelerator's own counted entry, as its engine start leaves them, at will-sleep, after a cold restore and four seconds into a wake (`N97GTEngineStart`, `N97GTSleep`, `N97GTRestore`, `N97GTWake`, reads only; the first engine enable's `N97GTBoot` predates the accelerator), with the wake's software interrupts (`RPLPWakeSWIRQ`), its first sixteen submissions (`RPLPWakeSubmits`) and forcewake calls (`RPLPWakeForceWakeFirst`). |
| `-wex-rplp-pciwake` | Raptor Lake-P only, experimental: at the system's power-on notice, where the plugin's own sleep/wake handler is first in line, read the GPU's configuration space and, where it differs from the copy taken at will-sleep, put the device back into D0, rewrite the BARs and the command register from that copy, so that the accelerator's own wake work (4 ms after the notice in rounds 51 and 52) reaches a device that decodes its registers; without the flag the family's own restore does the same about 20 ms later, after that work has run, and the record alone is kept (`RPLPWakePCI`: the configuration at will-sleep, at the notice before and after). |
| `-wex-rplp-coldcore` | Raptor Lake-P only, experimental: on a wake after a real power loss, after the plugin's MBUS credit restore and before the native framebuffer's own work, write back the boot values (read at the first engine enable after boot, `N97FBCoreBoot`) of the bandwidth buddy registers, of CHICKEN_MISC_2 with its bit 30 set (Linux's i915 `CHICKEN_MISC_DISABLE_DPT`, written at every pipe enable on display version 13: with it clear the display engine fetches tiled planes through a display page table that no macOS driver builds, and a tiled primary plane shows black while the linear cursor still shows), and of the powered pipes' chicken registers, where the live value differs; verify-first, read back after. `N97FBColdCore` records the outcome; `N97FBCoreSleep`, `N97FBCoreCold`, `N97FBCoreResume` and `N97FBCoreWake` the twenty registers at the sleep override, after the cold restore before the plugin's writes, after them and four seconds into the wake. The observation that a system sleep zeroes these registers on this display generation comes from [Thatisjigen/WhateverXe](https://github.com/Thatisjigen/WhateverXe) (branch `pr/system-sleep-0.7.0`). |
| `-wex-rplp-wsstart` | Raptor Lake-P only, experimental: at the system's will-sleep notice, and again at every sampler tick of the sleep entry and the wake, clear the graphics family's window-server wait byte and the cached computed-state word of a framebuffer controller whose every framebuffer is an `IONDRVFramebuffer` (the placeholder the family attaches to an undriven discrete GPU, which the window server never opens). With that wait bit in the aggregate state the family never sends the window server its sleep notification and never sends the accelerators their WillSleep (public IOGraphics source, `IOFramebuffer::systemWork`), so the accelerator's wake has nothing to restart and the sleep entry waits out the family's 90-second acknowledgement timeout; and the family's `computeState` returns the cached word unless the controller has pending work, which the placeholder's never has, so the byte alone changes nothing (round 56). The writes are one byte and one word into the family's controller object, at the offsets the family's own paths use, and only after the controller's framebuffers have been checked by class; a zero state word is the transparent one. `RPLPWSStart` records every controller's byte and word before and after, with or without the flag, and the ticks that had to clear again. |
| `-wex-rplp-entryirq` | Raptor Lake-P only, experimental: two changes to the sleep entry's interrupt bookkeeping. A port-only power release (no PW1, no pipe well) that the release gate would refuse only because the bridge still counts an affected pipe active proceeds when the hardware shows every affected pipe stopped (pipe, cursor, planes, transcoder and DSB off): at round 57's second sleep the native path released the panel's DDI well at its display power-off with everything off and the bookkeeping stale, and the refusal latched a fault the wake inherited. And at the sleep override, a display interrupt master still on with no handler running is turned off through the plugin's own disable path, so the suspend's quiescence proof meets the state of a first sleep. `N97FBEntryIRQ` records the relaxed passes and refusals and the override disables; `N97FBFirstPowerCheck`'s word 24 says whether the relaxed rule was tried and passed. |
| `-wex-rplp-cursorlag` | Raptor Lake-P only, experimental: the watermark latch's proof of the cursor's live surface (CURSURFLIVE equal to the cursor base the update intended) is required only when the update moved the cursor's display buffer allocation. Otherwise a cursor live surface still behind its base when the latch's budget runs out (three frames and a millisecond) completes the latch instead of refusing it, with the lag recorded in the pipe's latch record (bit 16 of its last word), the `N97FBWMLatchCursorLag` snapshot (its first occurrence kept as `N97FBWMLatchCursorLagFirst`) and the `N97FBCursorLag` counters: the frame that latched the primary surface and the watermarks is proven either way, and the cursor's allocation did not change, so retiring the old allocation frees nothing the cursor uses. Round 59 (2026-11-06): after the first wake of the boot the cursor's live surface stayed a frame behind its base for the whole budget, the refusal faulted the watermark state, the next display buffer layout call faulted the buffer state, and the second sleep's engine stop refused on it. Without the flag the refusal stands and is only counted. Round 60 (2026-11-06) passed the whole ladder with the flag on; the lag did not recur. |
| `-wex-rplp-cursorsize` | Raptor Lake-P only, experimental: a cursor resize in flight is not a fault. When the cursor is enlarged or shrunk while it is shown (the shake-to-locate enlargement, a large drag image), the native path asks for the cursor's watermarks at the new size before it rewrites the cursor control, and the plugin's setup refused the request because the live control still showed the previous size; that refusal is sticky and made the next sleep fail (round 61, where it looked like a 144 Hz problem and was not). With the flag the setup computes the plan for the new size when the live cursor is enabled at another valid size (64, 128 or 256 pixels), and a watermark latch that still sees the previous size is in flight (`CursorSize`, verified again at the next arm) instead of a conflict. `N97FBCursorSize` counts the admitted setups, the in-flight latches and, with the flag off, the refused setups. |
| `-wex-rplp-va-vpp` | Raptor Lake-P A7A8 revision 04 on Big Sur 20G1443 only, experimental: a one-byte correction on one page of the pinned video driver image (`AppleIntelTGLGraphicsVADriver`, admitted by its full-file and page digests), delivered the same way as `-wex-rplp-gl-urb` (the page is changed only where the kernel validates exactly that page of exactly that file on a read-only mount; the on-disk file is never written). The site and the byte come from the project's private static analysis (Q54); with the correction the screen recorder's pixel transfer is refused cleanly by the driver instead of crashing it, ordinary decoding is meant to stay untouched, and a software fallback for the refused transfer is expected rather than demonstrated. Receipts: `RPLPVAVPPStatus` (setup state and result counters), `RPLPVAVPPReady`, `RPLPVAVPPApplied` and the three digests. Unknown OS, driver, file or validation state leaves it inactive. Round 61 could not test it: the correction never armed, because its kernel check compared the OS build string at kernel start, when userspace has not set it yet. Since r349 the kernel is admitted by its exact version string and the build string is compared where the page is validated (`RPLPPageKernel`, `RPLPPageBuild`). Round 62 retests. |
| `-wex-no-va-renderer` | Experimental, any supported GPU: do not advertise `IOVARendererID` on the accelerator. Ported from [Thatisjigen/WhateverXe](https://github.com/Thatisjigen/WhateverXe) (branch `pr/video-optin-0.7.0`): there, hiding that one key stops the VA driver's crash during pixel transfers (screen recording, the camera, media analysis), the signature this fork's Raptor Lake-P notes list as the screen-recording crash; Apple's decoder service may then find no hardware renderer and decode video in software. Not yet tried on this machine. |
| `-wex-rplp-warev3` | Raptor Lake-P only, diagnostic: hand revision 3 instead of the physical 4 to the kernel accelerator's workaround-table initialisation alone (the pre-revision-4 policies, 13 packed bits), to compare the OpenGL readback probes against the default. Can affect rendering, media, performance and stability; not a fix, not for daily use. `N97RPLPWARev3`. |
| `wex-rplp-urb-banks=` | Raptor Lake-P only: publish this many URB (unified return buffer) banks, 1 to 8, to the drivers instead of the measured six. The user-mode OpenGL driver sizes its vertex-shader URB allocation from the count; with five the allocation ends at 320 KiB instead of 384 KiB and the missing-geometry defect described in the Raptor Lake-P notes is gone (every offscreen probe passes, Chess and the cube render correctly), so `=5` is the working configuration on this part until the exact allocation rule is carried in user mode. Costs some URB capacity for every consumer. The plugin's admission guards take the published count as their reference, and `RPLPURBBanksOverride` records an applied override. No leading dash: the kernel reads a dash-prefixed name as a flag and ignores its value. |
| `-wex-rplp-gl-urb` | Experimental exact OpenGL URB allocation correction for qualified Raptor Lake-P A7A8 revision 04 on Big Sur 20G1443. Requires the pinned driver image accepted by the source. Keeps the physical six-bank capability and corrects only matching allocation packets in a validated mapped page; the on-disk bundle is unchanged. Disabled by default. Cannot be combined with `wex-rplp-urb-banks=`. Unknown OS, driver, file or validation state leaves the correction inactive; `RPLPGLURBReady` and `RPLPGLURBApplied` distinguish hook readiness from a corrected page. Hardware qualification is pending. |

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
