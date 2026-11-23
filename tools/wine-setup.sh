#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds a Wine prefix that runs the original 16-bit TWTW.EXE.
# Verified with Wine 11.22 (Arch, WoW64 build): it shows the title screen.
#
# Usage: tools/wine-setup.sh            # set up the prefix (idempotent)
#        tools/wine-setup.sh run [args]  # launch TWTW.EXE in an 800x600 virtual desktop
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="$ROOT/Jak To Działa.iso"
WORK="$ROOT/work"
CD="$WORK/cd"                      # extracted ISO, mapped as D: (type cdrom)
export WINEPREFIX="$WORK/wineprefix"
export WINEDEBUG="${WINEDEBUG:--all}"

setup() {
    mkdir -p "$WORK"
    if [ ! -d "$CD/DKCODE" ]; then
        7z x -y -o"$CD" "$ISO" >/dev/null
    fi
    printf 'DKMMTWTW__CDFS_' > "$CD/.windows-label"   # ISO volume ID

    [ -d "$WINEPREFIX/drive_c" ] || wineboot -i >/dev/null 2>&1

    # The app refuses to run unless MSCDEX reports a CD-ROM drive.
    ln -sfn "$CD" "$WINEPREFIX/dosdevices/d:"
    wine reg add 'HKLM\Software\Wine\Drives' /v 'd:' /t REG_SZ /d cdrom /f >/dev/null

    # Wine's builtin wing.dll16 does not fill in BITMAPINFO in WinGGetDIBPointer,
    # which crashes the app. Use the original WinG 1.0 DLLs shipped on the CD instead.
    local sys="$WINEPREFIX/drive_c/windows/system" tmp
    mkdir -p "$sys"
    tmp="$(mktemp -d)"
    for f in WING.DL_ WINGDE.DL_ WINGDIB.DR_ WINGPAL.WN_; do
        7z x -y -o"$tmp" "$CD/DKCODE/$f" >/dev/null
    done
    # SZDD stores the name with the last character dropped, so restore the extensions
    cp "$tmp/WING.DLL"   "$sys/WING.DLL"
    cp "$tmp/WINGDE.DLL" "$sys/WINGDE.DLL"
    cp "$tmp/WINGDIB.DR" "$sys/WINGDIB.DRV"
    cp "$tmp/WINGPAL.WN" "$sys/WINGPAL.WND"
    rm -rf "$tmp"
    # 16-bit modules are looked up as "<name>.dll16"; a plain "wing" key is not enough.
    wine reg add 'HKCU\Software\Wine\DllOverrides' /v wing.dll16 /t REG_SZ /d native /f >/dev/null
}

case "${1:-setup}" in
    setup) setup; echo "Prefix ready: $WINEPREFIX" ;;
    run)   shift; setup
           cd "$CD/DKCODE"
           LC_ALL=pl_PL.UTF-8 exec wine explorer /desktop=TWTW,800x600 'D:\DKCODE\TWTW.EXE' "$@" ;;
    *)     echo "usage: $0 [setup|run]" >&2; exit 2 ;;
esac
