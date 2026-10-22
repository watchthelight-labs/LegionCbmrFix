# LegionCbmrFix

A small OpenCore UEFI driver for Lenovo laptops whose BIOS loads Microsoft's `cbmr_driver.efi` (Cloud Bare Metal Recovery, the "reinstall Windows from the cloud" feature) on every boot. That image sits at 0x180000000, and macOS later trips over its pages and panics. The driver unloads the image before macOS starts and reserves its 164 KiB so macOS never uses them.

Found on a Legion 5 15IAX10 (83F0, BIOS S2CN26WW). Other Lenovo models with cloud recovery in the firmware may have the same problem; if yours does, please open an issue so I can add it to the list.

## Symptoms

Random kernel panics under memory pressure (big builds, `git`, browsers, any process), sometimes a few minutes after boot, sometimes hours. The panic always names a page between 0x180000000 and 0x180028fff:

```
panic(cpu 1 caller 0xffffff800ecd002f): vm_page_validate_no_references: page 0x18001f000 is referenced @vm_resident.c:731
```

The backtrace runs through `vm_page_free_list` and `vm_object_sleep`, usually from `exit` or `vm_map_terminate`. It happens with or without GPU drivers (`-nvoff` didn't help here), so it is easy to blame the wrong kext.

## Status

| What | Tested on | Result |
|---|---|---|
| Finds and unloads cbmr_driver.efi | Legion 5 15IAX10, BIOS S2CN26WW, OpenCore 1.0.8 | found at 0x180000000, 41 pages, unload and reserve both succeed every boot |
| Panic gone | macOS 15.8.1 (24H32) | memory churn test (16 GB mmap/touch/munmap loops, 360 s, 47 rounds) passed; it panicked in 4.5 to 10 min before |
| Daily use | macOS 15.8.1 and 26.7.1 (25G241) | no panic of this kind since 2026-10-16 |

## What it can't do

- It only knows the image by the string `cbmr_driver` inside it. If Lenovo renames it, the driver finds nothing and only reserves the default range.
- It doesn't stop the BIOS from loading the image. It cleans up after it.
- Tested on one model and one BIOS version.

## Installation

1. Get `LegionCbmrFix.efi` from the [releases](../../releases), or build it (below).
2. Copy it to `EFI/OC/Drivers/`.
3. In `config.plist`, `UEFI > Drivers`, add:

   ```
   Path         LegionCbmrFix.efi
   Enabled      true
   LoadEarly    false
   Arguments    (empty)
   Comment      unload cbmr_driver.efi before macOS
   ```
4. Reboot, then check it worked:

   ```
   $ tools/cbmr-status.sh
   record   RBMC
   cbmr     found and unloaded
   range    0x180000000, 41 pages (164 KiB)
   reserve  success
   ```
   "not loaded by this firmware this boot" means your BIOS doesn't load cbmr (or not this time); the driver still reserved the default range, which is harmless.

To roll back, set `Enabled` to false. Nothing is written to flash: the result variable is volatile and gone after a reboot.

## Am I affected?

Install it and run `tools/cbmr-status.sh`: "found" means your firmware loads the image. Without the driver, OpenCore's OpenShell shows it too: `memmap` lists a `BS_Code` range starting at 0x180000000, and dumping that range with `dmem` turns up the string `cbmr_driver.pdb`.

Disabling cloud recovery in the BIOS setup might also work, but I haven't found that option on the 15IAX10.

## Building

```
./build.sh
```

Needs clang and lld-link from LLVM: `apt install clang lld` on Linux, `port install llvm-20` (MacPorts) or `brew install llvm` on macOS. Apple's clang lacks lld-link. The build is reproducible: with LLVM 20 (what the releases and my own builds use) the SHA-256 printed at the end matches the release. Other LLVM versions give a different but equivalent file.

## How it works

At load time the driver walks every loaded image, picks the one whose bytes contain `cbmr_driver`, calls `UnloadImage()` on it and then `AllocatePages(AllocateAddress, EfiReservedMemoryType)` over its range. Reserved memory is never handed to macOS, so the pages that panicked can't be reused. More detail, including why unloading alone isn't enough: [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md).

## Credits

- [OpenCore](https://github.com/acidanthera/OpenCorePkg) (acidanthera): the bootloader this plugs into, and the vendor GUID used for the result variable.
- The UEFI specification for the boot services used.

## Licence

BSD 3-Clause, see [LICENSE](LICENSE).
