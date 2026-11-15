# ios-on7xelte

**Apple's iOS 16 kernel and userland on the Samsung Galaxy J7 Prime (SM-G610F, `on7xelte`).**

This repository ports Apple's open-source XNU kernel (xnu-8796.141.3, the iOS 16 generation) to the
Exynos 7870 and boots the stock iOS 16.7 (20H392) iPhone X userland from the phone's internal eMMC.
CoreAnimation draws on the Mali-T830 GPU through Mesa's panfrost driver.

> **Status: 2026-11-15, project paused.**
> The J7 Prime boots XNU on all 8 cores, mounts an HFS+ iOS root from the `userdata` partition and
> runs `launchd`, backboardd and SpringBoard. The home screen, touch, keyboard, Wi-Fi and Safari
> work. CoreAnimation renders on the GPU with zero-copy IOSurfaces. The full engineering log and
> the last hand-off are in [`NOTES.md`](NOTES.md).

## Device

| | |
| --- | --- |
| Model | Samsung Galaxy J7 Prime, SM-G610F (`on7xelte`) |
| SoC | Exynos 7870: 8x Cortex-A53, ARMv8.0 (no LSE, no PAN) |
| GPU | Mali-T830 MP1 (Midgard), driven by our kernel backend + Mesa 25.0.7 panfrost |
| Storage | eMMC; BOOT = `mmcblk0p10`, iOS root in `userdata` (`mmcblk0p25`), GPT unchanged |
| Display | 1080x1920 TD4300 panel; the bootloader leaves the DECON scan-out buffer at `0x67000000` |
| Interrupts | GIC-400 (GICv2): distributor `0x104E1000`, CPU interface `0x104E2000` |
| USB | Works: adb/TWRP for flashing, and a USB ACM serial line for the kernel log and commands |

The phone is an experimental bring-up target. Android has been removed from `userdata`; the
bootloader, recovery (TWRP), modem, EFS/persist and the partition backups are kept intact.

## What works

| Area | State |
| --- | --- |
| Kernel | XNU boots through the shim, 4K pages, GICv2, generic timer, 8-core SMP, CPU DVFS |
| Storage | eMMC driver with multi-block and DMA reads, read/write HFS+ root in `userdata` |
| Userland | `launchd`, backboardd, SpringBoard and system daemons from the iOS 16.7 root |
| Display | Presented as an iPhone 8 Plus (1242x2208, 414x736 pt @3x) and scaled to the 1080x1920 panel on 4 CPU threads at 60 Hz |
| Input | Touchscreen, hardware keys, on-screen keyboard |
| Power | Battery level, panel brightness and sleep, RTC |
| Wi-Fi | SDIO Broadcom chip with our own driver: scan, WPA2 join, DHCP, auto-join; Safari browses |
| GPU | Mali-T830 power, MMU, job IRQs; Mesa panfrost in user space; CoreAnimation in backboardd draws through `libon7gl` (EAGLContext on Mesa) with zero-copy IOSurfaces |

Experimental: the audio codec and speaker path, the `sweetfb_2x=1` @2x display mode (828x1472,
about twice as fast on the GPU).

Not working: cellular, Bluetooth, camera, sensors, Metal (apps that need Metal do not run).

## How it boots

```
Samsung S-Boot (unlocked)
  -> BOOT partition (p10): Samsung boot.img
     -> shim (shim/on7xelte): EL1, display/PMU/CMU setup, copies XNU + ADT + boot-args
        -> XNU-8796 (MACHINE_CONFIG=A5XELTE, Exynos 7870 addresses)
           -> HFS+ root in userdata (rd=emmcroot0 emmcroot=USERDATA)
              -> launchd -> backboardd (+ libon7gl, Mesa panfrost) -> SpringBoard
```

Every test is a temporary BOOT image: the phone is sent to TWRP over USB, the image is written to
`mmcblk0p10`, read back and verified, and the phone reboots. The kernel log arrives over the USB
serial line.

## Building

The kernel is built by GitHub Actions on `macos-15` with
[blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build): `xnu.yml` fetches
xnu-8796.141.3, applies `xnu/patches/` in order, scans for ARMv8.1 LSE instructions (the
Cortex-A53 has none) and uploads `kernel.development.a5xelte`.

```sh
gh workflow run xnu.yml -R furkan-karapinar/ios-on7xelte
gh run download RUN_ID -R furkan-karapinar/ios-on7xelte -n xnu-kernel -D ci/xnu-kernel-RUN_ID
```

`mesa.yml` builds Mesa (OSMesa + panfrost) for iOS arm64, the `hakios-drm` shim and
`libon7gl.dylib` (artifact `gpu-userland`). The kernel installs `libon7gl` on the root at boot from
an embedded payload (patch 0194, `tools/on7put-payload.py`).

The boot image is made with `tools/mkpayload.py` (kernel + Apple Device Tree + boot-args) and
`tools/on7xelte-pack-probe.py` (shim + payload into the Samsung boot image).

## Patch series

`xnu/patches/` holds the whole series, applied in order. `0001`-`0083` are the ios-sweet base,
`0084`-`0136` the Exynos A5XELTE series this port started from, `0137` onwards the J7 Prime work:

| Patches | Content |
| --- | --- |
| `0084`-`0136` | GICv2, 4K pages, Cortex-A53 code generation, no PAN, eMMC + GPT root, AMFI bridge, DECON/IOSurface display bridge, SMP, touch, PMIC, USB ACM log, CPU DVFS |
| `0137`-`0153` | Keys, touch, battery, RTC; parking hardware services that are missing; eMMC multi-block and DMA |
| `0154`-`0159` | Panel brightness and sleep, audio codec and speaker path |
| `0160`-`0186` | Wi-Fi: SDIO, firmware, scan, WPA2 handshake, DHCP, auto-join, apple80211 and IOKit registry for wifid |
| `0184`, `0187`, `0188`, `0199` | iPhone 8 Plus scaled display, present timing, present thread with multi-core scaling, optional @2x |
| `0189`-`0198` | Mali-T830 bring-up (reset, power, MMU, IRQ), HakiosMali backend for Mesa, GPU DVFS, `libon7gl` injection into backboardd, zero-copy IOSurface import |

## Repository layout

| Path | Content |
| --- | --- |
| `xnu/patches/` | XNU patch series |
| `.github/workflows/` | `xnu.yml` (kernel), `mesa.yml` (GPU userland), root image workflows |
| `shim/on7xelte/` | Boot shim for the J7 Prime |
| `tools/mkadt.py`, `tools/mkadt_a5xelte.py` | Apple Device Tree generation |
| `tools/eagl/` | `libon7gl`: EAGLContext for CoreAnimation on Mesa |
| `tools/mesa/` | Mesa build for iOS, panfrost patches, `hakios-drm` |
| `tools/on7wifid/` | User-space helper for the Wi-Fi bring-up |
| `tools/on7xelte-*.py` | USB log, boot image packing, register and Wi-Fi helpers |
| `NOTES.md` | Engineering log and hand-off: every boot, measurement and next step |

Device dumps, partition backups, serial numbers, Apple firmware, IPSW files and generated root
images are not in git.

## Credits

Built on Apple's open-source releases (XNU, IOHIDFamily, libpthread, HFS),
[ios-sweet](https://github.com/dipa-studio/ios-sweet), the A5XELTE Exynos series
([orhunnx/ios-a5xelte](https://github.com/orhunnx/ios-a5xelte),
[nickdemon22/ios-a51](https://github.com/nickdemon22/ios-a51)), the HakiOS Galaxy A50 port (Mali
backend and `eagl.c`, MIT License, see `tools/eagl/README.md`), [Mesa](https://mesa3d.org)
panfrost, [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build),
[ipsw](https://github.com/blacktop/ipsw), Samsung's Exynos 7870 kernel sources and TWRP. iOS itself
is Apple's and is not distributed here.

This is an experimental port, not a usable phone OS. Apple's security model is largely disabled;
do not store anything personal on the device.
