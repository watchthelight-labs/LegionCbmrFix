/*
 * LegionCbmrFix: OpenCore UEFI driver that keeps Microsoft's cbmr_driver.efi out of macOS's memory.
 *
 * Some Lenovo BIOSes (seen on the Legion 5 15IAX10, BIOS S2CN26WW) load cbmr_driver.efi (Cloud Bare Metal
 * Recovery) on every boot at 0x180000000, 41 pages of boot-services code, and macOS later panics in
 * vm_page_validate_no_references on pages of that range. This driver unloads the image and reserves its pages
 * so the macOS VM never gets them, then stores the result in the volatile NVRAM variable
 * 4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:legion-cbmr. Details: docs/HOW-IT-WORKS.md.
 *
 * Freestanding, no EDK2 headers. Build with build.sh.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 watchthelight
 */
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef u64 UINTN;
typedef UINTN EFI_STATUS;
typedef void *EFI_HANDLE;
typedef struct { u32 a; u16 b, c; u8 d[8]; } EFI_GUID;

#define EFIAPI __attribute__((ms_abi))
#define ERR(x) (0x8000000000000000ULL | (x))

typedef EFI_STATUS (EFIAPI *ALLOCATE_PAGES)(u32 type, u32 memtype, UINTN pages, u64 *addr);
typedef EFI_STATUS (EFIAPI *HANDLE_PROTOCOL)(EFI_HANDLE h, EFI_GUID *g, void **iface);
typedef EFI_STATUS (EFIAPI *UNLOAD_IMAGE)(EFI_HANDLE h);
typedef EFI_STATUS (EFIAPI *LOCATE_HANDLE_BUFFER)(u32 type, EFI_GUID *g, void *key, UINTN *n, EFI_HANDLE **buf);
typedef EFI_STATUS (EFIAPI *FREE_POOL)(void *p);
typedef EFI_STATUS (EFIAPI *SET_VARIABLE)(u16 *name, EFI_GUID *g, u32 attr, UINTN size, void *data);

typedef struct {
    u8 hdr[24];
    void *RaiseTPL, *RestoreTPL;
    ALLOCATE_PAGES AllocatePages;
    void *FreePages, *GetMemoryMap, *AllocatePool;
    FREE_POOL FreePool;
    void *CreateEvent, *SetTimer, *WaitForEvent, *SignalEvent, *CloseEvent, *CheckEvent;
    void *InstallProtocolInterface, *ReinstallProtocolInterface, *UninstallProtocolInterface;
    HANDLE_PROTOCOL HandleProtocol;
    void *Reserved, *RegisterProtocolNotify, *LocateHandle, *LocateDevicePath, *InstallConfigurationTable;
    void *LoadImage, *StartImage, *Exit;
    UNLOAD_IMAGE UnloadImage;
    void *ExitBootServices, *GetNextMonotonicCount, *Stall, *SetWatchdogTimer, *ConnectController,
         *DisconnectController, *OpenProtocol, *CloseProtocol, *OpenProtocolInformation, *ProtocolsPerHandle;
    LOCATE_HANDLE_BUFFER LocateHandleBuffer;
} EFI_BOOT_SERVICES;

typedef struct {
    u8 hdr[24];
    void *GetTime, *SetTime, *GetWakeupTime, *SetWakeupTime, *SetVirtualAddressMap, *ConvertPointer, *GetVariable,
         *GetNextVariableName;
    SET_VARIABLE SetVariable;
} EFI_RUNTIME_SERVICES;

typedef struct {
    u8 hdr[24];
    void *FirmwareVendor; u32 FirmwareRevision;
    void *ConsoleInHandle, *ConIn, *ConsoleOutHandle, *ConOut, *StandardErrorHandle, *StdErr;
    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
} EFI_SYSTEM_TABLE;

typedef struct {
    u32 Revision; EFI_HANDLE ParentHandle; EFI_SYSTEM_TABLE *SystemTable; EFI_HANDLE DeviceHandle; void *FilePath;
    void *Reserved; u32 LoadOptionsSize; void *LoadOptions; void *ImageBase; u64 ImageSize; u32 ImageCodeType;
    u32 ImageDataType; void *Unload;
} EFI_LOADED_IMAGE;

static EFI_GUID gLoadedImage = { 0x5B1B31A1, 0x9562, 0x11D2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };
static EFI_GUID gOcVendor = { 0x4D1FDA02, 0x38C7, 0x4A6A, { 0x9C, 0xC6, 0x4B, 0xCC, 0xA8, 0xB3, 0x01, 0x02 } };

static int contains(const u8 *p, u64 n, const char *s)
{
    u64 m = 0; while (s[m]) m++;
    for (u64 i = 0; i + m <= n; i++) {
        u64 k = 0;
        while (k < m && p[i + k] == (u8)s[k]) k++;
        if (k == m) return 1;
    }
    return 0;
}

// result record, read back by tools/cbmr-status.sh
typedef struct { u32 magic; u32 found; u64 base; u64 pages; u64 unload_st; u64 reserve_st; } RESULT;

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    RESULT r = { 0x434d4252u /* "RBMC" */, 0, 0, 0, ERR(14), ERR(14) };   // 14 = NOT_FOUND
    UINTN n = 0; EFI_HANDLE *hs = 0;
    if (bs->LocateHandleBuffer(2 /* ByProtocol */, &gLoadedImage, 0, &n, &hs) == 0) {
        for (UINTN i = 0; i < n && !r.found; i++) {
            EFI_LOADED_IMAGE *li = 0;
            if (hs[i] == image || bs->HandleProtocol(hs[i], &gLoadedImage, (void **)&li) != 0 || !li || !li->ImageBase) continue;
            if (li->ImageSize > 0x200000 || !contains((const u8 *)li->ImageBase, li->ImageSize, "cbmr_driver")) continue;
            r.found = 1; r.base = (u64)li->ImageBase; r.pages = (li->ImageSize + 0xfff) >> 12;
            r.unload_st = bs->UnloadImage(hs[i]);
        }
        bs->FreePool(hs);
    }
    // Reserve the range whether or not the image was found this boot, so macOS never allocates it.
    u64 addr = r.found ? r.base : 0x180000000ULL, pages = r.found ? r.pages : 0x29;
    r.reserve_st = bs->AllocatePages(2 /* AllocateAddress */, 0 /* EfiReservedMemoryType */, pages, &addr);
    if (!r.found) { r.base = 0x180000000ULL; r.pages = 0x29; }
    u16 name[] = { 'l','e','g','i','o','n','-','c','b','m','r',0 };
    st->RuntimeServices->SetVariable(name, &gOcVendor, 0x2 | 0x4 /* BS|RT, volatile */, sizeof r, &r);
    return 0;
}
