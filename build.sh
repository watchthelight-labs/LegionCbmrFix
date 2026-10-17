#!/bin/bash
# Builds out/LegionCbmrFix.efi (PE32+, EFI boot-service driver).
# Needs clang and lld-link from LLVM. Found automatically on Linux (apt install clang lld), MacPorts (llvm-20)
# and Homebrew (llvm); or set CC and LLD_LINK. Apple's own clang has no lld-link.
set -euo pipefail
cd "$(dirname "$0")"

pick() { for c in "$@"; do command -v "$c" >/dev/null 2>&1 && { command -v "$c"; return 0; }; done; return 0; }
CC=${CC:-$(pick clang-mp-20 /opt/local/bin/clang-mp-20 /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang clang-20 clang)}
LLD_LINK=${LLD_LINK:-$(pick lld-link-mp-20 /opt/local/bin/lld-link-mp-20 /opt/homebrew/opt/llvm/bin/lld-link /usr/local/opt/llvm/bin/lld-link lld-link-20 lld-link)}
[ -n "$CC" ] && [ -n "$LLD_LINK" ] || { echo "need clang and lld-link (see the top of build.sh)"; exit 1; }

mkdir -p out
"$CC" -target x86_64-unknown-windows -ffreestanding -fno-stack-protector -fshort-wchar -mno-red-zone \
  -fno-builtin -O2 -Wall -c src/LegionCbmrFix.c -o out/LegionCbmrFix.o
# /Brepro: no timestamp in the PE header, so the same source gives the same file
"$LLD_LINK" /subsystem:efi_boot_service_driver /entry:efi_main /nodefaultlib /dll /base:0x10000000 /Brepro \
  /out:out/LegionCbmrFix.efi out/LegionCbmrFix.o
rm -f out/LegionCbmrFix.o out/LegionCbmrFix.lib
shasum -a 256 out/LegionCbmrFix.efi 2>/dev/null || sha256sum out/LegionCbmrFix.efi
