/* Shims for imports Oblivion (Oblivion.exe 1.2.0.416) uses beyond the other
 * games: WINMM mmio* (WAV loading), VERSION, COMCTL32 stubs and a few
 * kernel32/user32 calls. The INI calls Oblivion needs (Get/WritePrivateProfile*)
 * already come from skyrim_shims.c. Compiled for every game; only registers
 * its shims, so it is harmless elsewhere. */
#define _GNU_SOURCE
#include "rt.h"
#include <string.h>

/* ===================== WINMM: mmio (RIFF/WAVE files) =====================
 * Oblivion parses its WAV files with mmioOpen / mmioDescend / mmioRead, from
 * a file path or from memory (FOURCC_MEM, sound data already read from a BSA).
 * Read-only: the whole file is loaded into guest memory, so the MMIOINFO
 * buffer pointers (pchBuffer / pchNext / pchEndRead) are real guest addresses
 * and mmioGetInfo + direct reads work like the real buffered I/O. */
#define FOURCC_RIFF 0x46464952u  /* 'RIFF' */
#define FOURCC_LIST 0x5453494Cu  /* 'LIST' */
#define FOURCC_MEM  0x204D454Du  /* 'MEM ' */
#define MMIO_FINDCHUNK 0x10
#define MMIO_FINDRIFF  0x20
#define MMIO_FINDLIST  0x40
#define MMIOERR_FILENOTFOUND  257
#define MMIOERR_CANNOTOPEN    259
#define MMIOERR_CHUNKNOTFOUND 265
/* MMIOINFO (32-bit): dwFlags 0, fccIOProc 4, pIOProc 8, wErrorRet 12, htask 16,
 * cchBuffer 20, pchBuffer 24, pchNext 28, pchEndRead 32, pchEndWrite 36,
 * lBufOffset 40, lDiskOffset 44, adwInfo[3] 48, reserved 60/64, hmmio 68; size 72.
 * MMCKINFO: ckid 0, cksize 4, fccType 8, dwDataOffset 12, dwFlags 16; size 20. */

typedef struct { uint32_t buf, size, pos; int owned; } Mmio;
#define MMIO_SLOTS 64
#define MMIO_BASE 0x7E000000u  /* handle values, apart from kernel32's table */
static Mmio *g_mmio[MMIO_SLOTS];
static pthread_mutex_t g_mmio_lock = PTHREAD_MUTEX_INITIALIZER;

static Mmio *mm_get(uint32_t h) {
    if (h < MMIO_BASE || h >= MMIO_BASE + 4 * MMIO_SLOTS || ((h - MMIO_BASE) & 3)) return NULL;
    return g_mmio[(h - MMIO_BASE) / 4];
}

static uint32_t rd32g(uint32_t a) { return M32(a); }

/* (const char *name, MMIOINFO *info, DWORD flags) -> HMMIO */
SHIM(mmioOpenA) {
    uint32_t name = ARG(0), info = ARG(1), flags = ARG(2);
    int err = 0;
    Mmio *m = calloc(1, sizeof *m);
    if (flags & (0x1 | 0x2 | 0x1000 | 0x200)) {          /* WRITE, READWRITE, CREATE, DELETE: not supported */
        err = MMIOERR_CANNOTOPEN;
    } else if (!name) {                                    /* memory file */
        if (!info || M32(info + 4) != FOURCC_MEM) err = MMIOERR_CANNOTOPEN;
        else { m->buf = M32(info + 24); m->size = M32(info + 20); }
    } else {
        char host[1024];
        rt_host_path(GSTR(name), host, sizeof host);
        FILE *f = fopen(host, "rb");
        if (!f) err = MMIOERR_FILENOTFOUND;
        else {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            m->buf = gmalloc((uint32_t)(n > 0 ? n : 1));
            m->size = fread(MEM + m->buf, 1, (size_t)(n > 0 ? n : 0), f);
            m->owned = 1;
            fclose(f);
        }
    }
    if (err) {
        if (info) M16(info + 12) = (uint16_t)err;
        free(m);
        RET(0, 3);
    }
    uint32_t h = 0;
    pthread_mutex_lock(&g_mmio_lock);
    for (int i = 0; i < MMIO_SLOTS && !h; i++)
        if (!g_mmio[i]) { g_mmio[i] = m; h = MMIO_BASE + 4u * (uint32_t)i; }
    pthread_mutex_unlock(&g_mmio_lock);
    if (!h) { if (m->owned) gfree(m->buf); free(m); if (info) M16(info + 12) = MMIOERR_CANNOTOPEN; RET(0, 3); }
    TRACE("mmioOpenA(%s) = %08x (%u bytes)", name ? GSTR(name) : "<mem>", h, m->size);
    RET(h, 3);
}

/* (HMMIO, UINT flags) */
SHIM(mmioClose) {
    Mmio *m = mm_get(ARG(0));
    if (!m) RET(0, 2);
    pthread_mutex_lock(&g_mmio_lock);
    g_mmio[(ARG(0) - MMIO_BASE) / 4] = NULL;
    pthread_mutex_unlock(&g_mmio_lock);
    if (m->owned) gfree(m->buf);
    free(m);
    RET(0, 2);
}

/* (HMMIO, HPSTR out, LONG n) -> bytes read, 0 at the end, -1 on a bad handle */
SHIM(mmioRead) {
    Mmio *m = mm_get(ARG(0));
    if (!m) RET(-1, 3);
    int32_t want = (int32_t)ARG(2);
    if (want < 0) want = 0;
    uint32_t left = m->size > m->pos ? m->size - m->pos : 0, n = (uint32_t)want < left ? (uint32_t)want : left;
    memcpy(MEM + ARG(1), MEM + m->buf + m->pos, n);
    m->pos += n;
    RET(n, 3);
}

/* (HMMIO, MMCKINFO *ck, const MMCKINFO *parent, UINT flags): enters the next
 * chunk, or the first one matching ck->ckid / (RIFF|LIST) + ck->fccType. */
SHIM(mmioDescend) {
    Mmio *m = mm_get(ARG(0));
    uint32_t ck = ARG(1), parent = ARG(2), fl = ARG(3);
    if (!m || !ck) RET(MMIOERR_CHUNKNOTFOUND, 4);
    uint32_t pos = m->pos, pend = m->size;
    if (parent) {
        uint32_t ps = M32(parent + 12);
        pend = ps + M32(parent + 4);
        if (pend > m->size) pend = m->size;
        if (pos < ps || pos >= pend) RET(MMIOERR_CHUNKNOTFOUND, 4);
    }
    uint32_t want_id = 0, want_type = 0;
    if (fl & MMIO_FINDCHUNK) want_id = M32(ck);
    else if (fl & MMIO_FINDRIFF) { want_id = FOURCC_RIFF; want_type = M32(ck + 8); }
    else if (fl & MMIO_FINDLIST) { want_id = FOURCC_LIST; want_type = M32(ck + 8); }
    for (;;) {
        if (pos + 8 > pend) RET(MMIOERR_CHUNKNOTFOUND, 4);
        uint32_t id = rd32g(m->buf + pos), sz = rd32g(m->buf + pos + 4);
        uint32_t type = pos + 12 <= m->size ? rd32g(m->buf + pos + 8) : 0;
        M32(ck) = id;
        M32(ck + 4) = sz;
        M32(ck + 8) = type;
        M32(ck + 12) = pos + 8;
        M32(ck + 16) = 0;
        if (!(fl & (MMIO_FINDCHUNK | MMIO_FINDRIFF | MMIO_FINDLIST))) break;
        if (id == want_id && (!want_type || type == want_type)) break;
        pos += 8 + ((sz + 1) & ~1u);
    }
    /* RIFF and LIST chunks start with their form type, which is consumed too */
    m->pos = (M32(ck) == FOURCC_RIFF || M32(ck) == FOURCC_LIST) ? M32(ck + 12) + 4 : M32(ck + 12);
    RET(0, 4);
}

/* (HMMIO, MMCKINFO *ck, UINT flags): moves to just after the chunk (padded to even) */
SHIM(mmioAscend) {
    Mmio *m = mm_get(ARG(0));
    if (!m || !ARG(1)) RET(MMIOERR_CHUNKNOTFOUND, 3);
    uint32_t sz = M32(ARG(1) + 4);
    m->pos = M32(ARG(1) + 12) + ((sz + 1) & ~1u);
    RET(0, 3);
}

/* (HMMIO, MMIOINFO *out, UINT flags) */
SHIM(mmioGetInfo) {
    Mmio *m = mm_get(ARG(0));
    uint32_t o = ARG(1);
    if (!m || !o) RET(11 /* MMSYSERR_INVALPARAM */, 3);
    memset(MEM + o, 0, 72);
    uint32_t p = m->pos <= m->size ? m->pos : m->size;
    M32(o + 20) = m->size;                 /* cchBuffer */
    M32(o + 24) = m->buf;                  /* pchBuffer */
    M32(o + 28) = m->buf + p;              /* pchNext */
    M32(o + 32) = m->buf + m->size;        /* pchEndRead */
    M32(o + 36) = m->buf + m->size;        /* pchEndWrite */
    M32(o + 44) = p;                       /* lDiskOffset */
    M32(o + 68) = ARG(0);                  /* hmmio */
    RET(0, 3);
}

/* (HMMIO, MMIOINFO *info, UINT flags): refill. Everything is already in the
 * buffer, so this only syncs the position and reports the end of data. */
SHIM(mmioAdvance) {
    Mmio *m = mm_get(ARG(0));
    uint32_t o = ARG(1);
    if (!m) RET(11, 3);
    if (o) {
        uint32_t next = M32(o + 28);
        if (next >= m->buf && next <= m->buf + m->size) m->pos = next - m->buf;
        M32(o + 28) = M32(o + 32) = m->buf + m->size;
        M32(o + 44) = m->pos;
    }
    RET(0, 3);
}

/* ===================== VERSION ===================== *
 * The game only reads version info for diagnostics; "no version resource" is
 * a normal answer (ERROR_RESOURCE_DATA_NOT_FOUND). */
SHIM(GetFileVersionInfoSizeA) { rt_set_last_error(c, 1812); if (ARG(1)) M32(ARG(1)) = 0; RET(0, 2); }
SHIM(GetFileVersionInfoA) { rt_set_last_error(c, 1812); RET(0, 4); }
SHIM(VerQueryValueA) { rt_set_last_error(c, 1813); RET(0, 4); }

/* ===================== COMCTL32 (only the launcher-style dialogs use it) ===================== */
SHIM(InitCommonControlsEx) { RET(1, 1); }
SHIM(ImageList_Destroy) { RET(1, 1); }
SHIM(ImageList_LoadImageA) { RET(0, 7); }

/* ===================== kernel32 / user32 ===================== */
/* (srcProc, srcHandle, dstProc, *dstHandle, access, inherit, options): one
 * handle table, no per-handle ownership, so a duplicate is the same value. */
SHIM(DuplicateHandle) { if (ARG(3)) M32(ARG(3)) = ARG(1); RET(1, 7); }
/* (len, buf): one fixed drive, "C:\" */
SHIM(GetLogicalDriveStringsA) {
    if (ARG(0) < 5) RET(5, 2);
    memcpy(MEM + ARG(1), "C:\\\0\0", 5);
    RET(4, 2);
}
SHIM(GetDoubleClickTime) { RET(500, 0); }
SHIM(SwapMouseButton) { RET(0, 1); }

/* ===================== extra imports of d3dx9_27.dll / binkw32.dll ===================== */
/* SEH handler: only called when an exception is raised, which the port never delivers. cdecl;
 * ExceptionContinueSearch. */
SHIM(_except_handler3) { RETC(1); }
SHIM(wcslen) { uint32_t n = 0, a = ARG(0); while (M16(a + 2 * n)) n++; RETC(n); }
/* (addr, size, newProtect, *oldProtect): memory is always RWX here */
SHIM(VirtualProtect) { if (ARG(3)) M32(ARG(3)) = 0x40; RET(1, 4); }

static const ShimDef defs[] = {
    {"msvcrt.dll", "_except_handler3", shim__except_handler3},
    {"msvcrt.dll", "wcslen", shim_wcslen},
    {"KERNEL32.dll", "VirtualProtect", shim_VirtualProtect},
    {"WINMM.dll", "mmioOpenA", shim_mmioOpenA},
    {"WINMM.dll", "mmioClose", shim_mmioClose},
    {"WINMM.dll", "mmioRead", shim_mmioRead},
    {"WINMM.dll", "mmioDescend", shim_mmioDescend},
    {"WINMM.dll", "mmioAscend", shim_mmioAscend},
    {"WINMM.dll", "mmioGetInfo", shim_mmioGetInfo},
    {"WINMM.dll", "mmioAdvance", shim_mmioAdvance},
    {"VERSION.dll", "GetFileVersionInfoSizeA", shim_GetFileVersionInfoSizeA},
    {"VERSION.dll", "GetFileVersionInfoA", shim_GetFileVersionInfoA},
    {"VERSION.dll", "VerQueryValueA", shim_VerQueryValueA},
    {"COMCTL32.dll", "InitCommonControlsEx", shim_InitCommonControlsEx},
    {"COMCTL32.dll", "ImageList_Destroy", shim_ImageList_Destroy},
    {"COMCTL32.dll", "ImageList_LoadImageA", shim_ImageList_LoadImageA},
    {"KERNEL32.dll", "DuplicateHandle", shim_DuplicateHandle},
    {"KERNEL32.dll", "GetLogicalDriveStringsA", shim_GetLogicalDriveStringsA},
    {"USER32.dll", "GetDoubleClickTime", shim_GetDoubleClickTime},
    {"USER32.dll", "SwapMouseButton", shim_SwapMouseButton},
    {NULL, NULL, NULL}
};

void oblivion_shims_init(void) { rt_register_shims(defs); }
