#!/bin/bash
# Prints what LegionCbmrFix did this boot, from the volatile NVRAM variable it leaves for macOS.
# Usage: tools/cbmr-status.sh        (no sudo needed)
set -euo pipefail
VAR=4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:legion-cbmr
if ! xml=$(nvram -x "$VAR" 2>/dev/null); then
  echo "no $VAR: LegionCbmrFix did not run this boot (not in UEFI > Drivers, or not enabled)"
  exit 1
fi
/usr/bin/python3 -I - "$xml" <<'EOF'
import plistlib, struct, sys
data = next(iter(plistlib.loads(sys.argv[1].encode()).values()))
magic, found, base, pages, unload, reserve = struct.unpack("<4sIQQQQ", data[:40])
codes = {0: "success", 2: "invalid parameter", 3: "unsupported", 9: "out of resources", 14: "not found",
         15: "access denied"}
def st(v):
    return codes.get(v & ~(1 << 63), hex(v)) if v else "success"
print("record   %s" % magic.decode(errors="replace"))
print("cbmr     %s" % ("found and unloaded" if found and not unload else "found, unload: " + st(unload) if found
                       else "not loaded by this firmware this boot"))
print("range    0x%x, %d pages (%d KiB)" % (base, pages, pages * 4))
print("reserve  %s" % st(reserve))
EOF
