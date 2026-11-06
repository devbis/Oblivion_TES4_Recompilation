#!/usr/bin/env python3
"""Removes the Steam wrapper (the `.bind` section) from Oblivion.exe.

Usage: unpack_oblivion.py Oblivion.exe Oblivion_unpacked.exe

Why this is all that is needed: the old SteamStub v1 wrapper used on
Oblivion 1.2.0.416 does not encrypt the game's own code. It only adds a
`.bind` section, points the PE entry point at its stub, and the stub jumps to
the real entry (the MSVC CRT startup) after checking with Steam. So unlike
Skyrim's SteamStub (which needs Steamless) the fix is to put the entry point
back and drop the section.

The real entry point is found by its code (the CRT startup that calls
GetStartupInfoA), not by a hard-coded address, so other 1.2.0.416 dumps work.
The same logic is mirrored in runtime/main.c (load_pe) so the original
Oblivion.exe also runs on the phone without this step.
"""
import hashlib
import struct
import sys

import pefile


def find_oep(pe, data):
    """VA of the MSVC CRT startup (WinMainCRTStartup)."""
    base = pe.OPTIONAL_HEADER.ImageBase
    iat = None
    for imp in pe.DIRECTORY_ENTRY_IMPORT:
        for e in imp.imports:
            if e.name == b"GetStartupInfoA":
                iat = e.address
    if iat is None:
        raise SystemExit("no GetStartupInfoA import: not an Oblivion.exe?")
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    raw = text.get_data()
    tva = base + text.VirtualAddress
    # The CRT startup begins:  push imm8 / push imm32 / call __SEH_prolog /
    # and [ebp-4],0 / lea eax,[ebp-X] / push eax / call [GetStartupInfoA]
    call_si = b"\x50\xff\x15" + struct.pack("<I", iat)
    p = raw.find(call_si)
    while p >= 0:
        # `and [ebp-4],0` (4 bytes) + `lea eax,[ebp-X]` (3 bytes) sit before the push eax
        and_at = p - 7
        if raw[and_at:and_at + 4] == b"\x83\x65\xfc\x00" and raw[and_at + 4:and_at + 6] == b"\x8d\x45":
            q = and_at - 5 - 5 - 2  # call rel32, push imm32, push imm8
            if q >= 0 and raw[q] == 0x6A and raw[q + 2] == 0x68 and raw[q + 7] == 0xE8:
                return tva + q
        p = raw.find(call_si, p + 1)
    raise SystemExit("could not find the CRT startup; send the exe for analysis")


def main():
    src, dst = sys.argv[1:3]
    data = bytearray(open(src, "rb").read())
    pe = pefile.PE(data=bytes(data))
    names = [s.Name.rstrip(b"\0") for s in pe.sections]
    print("sha1", hashlib.sha1(data).hexdigest())
    if b".bind" not in names:
        print("no .bind section: already unpacked, copying")
        open(dst, "wb").write(data)
        return
    bind = pe.sections[names.index(b".bind")]
    if names[-1] != b".bind":
        raise SystemExit(".bind is not the last section")
    lo, hi = bind.VirtualAddress, bind.VirtualAddress + bind.Misc_VirtualSize
    for i, d in enumerate(pe.OPTIONAL_HEADER.DATA_DIRECTORY):
        if d.VirtualAddress and lo <= d.VirtualAddress < hi:
            raise SystemExit("data directory %d lives in .bind; unsupported" % i)
    oep = find_oep(pe, data)
    print("original entry point: %08x (stub was %08x)" % (oep, pe.OPTIONAL_HEADER.ImageBase + pe.OPTIONAL_HEADER.AddressOfEntryPoint))
    if pe.OPTIONAL_HEADER.DATA_DIRECTORY[9].VirtualAddress:  # TLS callbacks must not point into the stub
        tls = pe.DIRECTORY_ENTRY_TLS.struct
        cb = tls.AddressOfCallBacks - pe.OPTIONAL_HEADER.ImageBase
        if cb and pe.get_dword_at_rva(cb):
            raise SystemExit("TLS callbacks present; unsupported")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    opt = e_lfanew + 24
    nsec_off = e_lfanew + 6
    nsec = struct.unpack_from("<H", data, nsec_off)[0]
    sec_off = opt + struct.unpack_from("<H", data, e_lfanew + 20)[0]
    struct.pack_into("<I", data, opt + 16, oep - pe.OPTIONAL_HEADER.ImageBase)  # AddressOfEntryPoint
    struct.pack_into("<I", data, opt + 56, bind.VirtualAddress)                  # SizeOfImage
    struct.pack_into("<H", data, nsec_off, nsec - 1)
    data[sec_off + 40 * (nsec - 1):sec_off + 40 * nsec] = b"\0" * 40
    del data[bind.PointerToRawData:]
    open(dst, "wb").write(data)
    pe2 = pefile.PE(dst)
    print("wrote %s: %d sections, entry %08x, %d bytes" % (
        dst, len(pe2.sections), pe2.OPTIONAL_HEADER.ImageBase + pe2.OPTIONAL_HEADER.AddressOfEntryPoint, len(data)))


if __name__ == "__main__":
    main()
