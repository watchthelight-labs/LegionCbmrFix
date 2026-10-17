# How LegionCbmrFix works

## The problem

The Legion 5 15IAX10 BIOS (S2CN26WW) carries Microsoft's `cbmr_driver.efi` inside the firmware image and loads it on every boot. It is not a `Driver####` entry, not a SysPrep or PlatformRecovery entry, and not on the Windows ESP, so it can't be turned off from `bcdedit` or the boot menu. It loads at its PE preferred base, 0x180000000, as 41 pages of `EfiBootServicesCode`.

Boot-services memory belongs to the firmware until `ExitBootServices()`, then the OS may reuse it. macOS does, and the pages end up in the VM. Every panic seen here named a page in that range (0x180018000, 0x18001f000, 0x180028000), always in `vm_page_validate_no_references` when the page was being freed. The exact way the page stays referenced wasn't traced further. The working theory is that something firmware-side (a runtime callback or DMA the cbmr driver set up) still touches the range after the handoff, but that part is unverified.

How it was found:
1. A reproducer that maps, touches and unmaps 16 GB in a loop panicked in 4.5 to 10 minutes, with any GPU driver set or none.
2. All panics hit the same few physical pages.
3. OpenShell `memmap` showed those pages as `BS_Code` 0x180000000 to 0x180028fff.
4. `dmem` over the range showed a PE image whose debug directory names `cbmr_driver.pdb`.

## What the driver does

`src/LegionCbmrFix.c`, about 100 lines, freestanding (its own minimal UEFI type definitions, no EDK2):

1. `LocateHandleBuffer(ByProtocol, EFI_LOADED_IMAGE_PROTOCOL)` to list every loaded image.
2. For each one (skipping itself and anything over 2 MiB), search the image bytes for `cbmr_driver`. The PDB path in the debug directory contains it, so this matches without parsing PE headers.
3. On a match, `UnloadImage()` it. That calls the image's unload handler, if it has one, and frees its pages.
4. Then `AllocatePages(AllocateAddress, EfiReservedMemoryType)` over the same range, so the pages are reserved again and listed as reserved in the memory map macOS reads. If the image wasn't found, the default 0x180000000 / 41 pages is reserved anyway; if something else owns that range the call fails and nothing happens.
5. Store a 40-byte record in the volatile variable `4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:legion-cbmr` (OpenCore's vendor GUID, readable from macOS with `nvram`): magic `RBMC`, found flag, base, page count, and the two EFI status codes.

Why both steps: unloading alone frees the pages back to the firmware pool, and they would be handed to macOS as free memory, which is what the panics came from. Reserving alone would leave the driver running. Doing both keeps the image from running any further and keeps macOS away from the memory it used.

## Risks

- `UnloadImage()` on a driver that installed protocols other drivers use could break those drivers. Nothing in the boot path here depends on cbmr (its job is cloud reinstall of Windows from the firmware menu, which this doesn't affect on a normal boot).
- The reserved 164 KiB is lost to macOS. That's the trade.
- No NVRAM writes to flash, no changes to the firmware or the ESP beyond the file you copy in.

## Reading the result

`tools/cbmr-status.sh` decodes the variable. Raw, `nvram 4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:legion-cbmr` prints it percent-encoded: `RBMC%01%00%00%00` then base, pages and the two statuses, little-endian 64-bit each. A status of 0 is success; errors have the top bit set (`0x800000000000000e` is NOT_FOUND).
