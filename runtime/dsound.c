/* DirectSound 8 shim: secondary buffers live in guest memory (so Lock can
 * hand out direct pointers) and are mixed in software on SDL's audio
 * thread. The primary buffer only exists to satisfy the API. */
#include "hcom.h"
#include <SDL2/SDL.h>

#define MIX_RATE 44100
typedef struct {
    uint32_t data;      /* guest address */
    uint32_t size;      /* bytes */
    int refs;           /* shared between duplicates */
} DSData;

typedef struct {
    DSData *d;
    int primary;
    int channels, bits, rate;
    uint32_t flags;
    int32_t volume, pan;
    uint32_t freq;
    double pos;         /* in frames */
    int playing, looping;
    uint32_t guest;     /* guest object */
    /* IDirectSound3DBuffer (software 3D: distance attenuation + panning) */
    float p3[3], dmin, dmax;
    uint32_t mode3d;    /* DS3DMODE_NORMAL 0, HEADRELATIVE 1, DISABLE 2 */
} DSBuf;

/* IDirectSound3DListener: one per process (there is one primary buffer) */
static struct { float pos[3], vel[3], front[3], top[3], dist_factor, rolloff, doppler; } g_lis = {
    {0, 0, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 0}, 1.0f, 1.0f, 1.0f};
#define DSBCAPS_CTRL3D 0x10

static HComClass k_ds8, k_dsb, k_ds3db, k_ds3dl;
static SDL_mutex *g_mix_lock;
static DSBuf **g_bufs;
static size_t g_nbufs, g_bufcap;
static SDL_AudioDeviceID g_dev;
static float g_master = 1.0f;

static float db_to_amp(int32_t v) {
    if (v <= -10000) return 0.0f;
    return powf(10.0f, (float)v / 2000.0f);
}

static inline float sample_at(DSBuf *b, uint32_t frame, int ch) {
    uint32_t bpf = (uint32_t)(b->channels * b->bits / 8);
    uint32_t nframes = b->d->size / bpf;
    if (frame >= nframes) return 0.0f;
    if (ch >= b->channels) ch = b->channels - 1;
    uint32_t a = b->d->data + frame * bpf + (uint32_t)ch * (uint32_t)(b->bits / 8);
    if (b->bits == 16) return (float)(int16_t)M16(a) / 32768.0f;
    return ((float)M8(a) - 128.0f) / 128.0f;
}

/* Gain and pan (-1..1) of a 3D buffer for the current listener. DirectSound's
 * default (software) model: full volume inside the minimum distance, then
 * min / (min + rolloff * (d - min)), no further change past the maximum. The
 * coordinate system is left-handed (right = top x front). */
static void spatial(const DSBuf *b, float *gain, float *pan) {
    *gain = 1.0f;
    *pan = 0.0f;
    if (!(b->flags & DSBCAPS_CTRL3D) || b->channels != 1 || b->mode3d == 2) return;
    float d[3] = {b->p3[0], b->p3[1], b->p3[2]};
    if (b->mode3d != 1)
        for (int i = 0; i < 3; i++) d[i] -= g_lis.pos[i];
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float mn = b->dmin > 0.0f ? b->dmin : 1.0f, mx = b->dmax > mn ? b->dmax : mn;
    float dd = dist < mn ? mn : dist > mx ? mx : dist;
    *gain = mn / (mn + g_lis.rolloff * (dd - mn));
    if (dist > 1e-6f) {
        const float *f = g_lis.front, *t = g_lis.top;
        float r[3] = {t[1] * f[2] - t[2] * f[1], t[2] * f[0] - t[0] * f[2], t[0] * f[1] - t[1] * f[0]};
        float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        if (rl > 1e-6f) *pan = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / (dist * rl);
    }
}

static void mix_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    float *out = (float *)stream;
    int frames = len / (int)(2 * sizeof(float));
    memset(stream, 0, (size_t)len);
    SDL_LockMutex(g_mix_lock);
    for (size_t k = 0; k < g_nbufs; k++) {
        DSBuf *b = g_bufs[k];
        if (!b || b->primary || !b->playing || !b->d->size) continue;
        uint32_t bpf = (uint32_t)(b->channels * b->bits / 8);
        double nframes = (double)(b->d->size / bpf);
        double step = (double)(b->freq ? b->freq : (uint32_t)b->rate) / MIX_RATE;
        float g3, p3;
        spatial(b, &g3, &p3);
        float vol = db_to_amp(b->volume) * g_master * g3;
        float lv = vol * (b->pan > 0 ? db_to_amp(-b->pan) : 1.0f);
        float rv = vol * (b->pan < 0 ? db_to_amp(b->pan) : 1.0f);
        if (b->flags & DSBCAPS_CTRL3D && b->channels == 1 && b->mode3d != 2) {  /* equal-power pan, centre = unity */
            float a = (p3 + 1.0f) * 0.78539816f;
            lv *= 1.41421356f * cosf(a);
            rv *= 1.41421356f * sinf(a);
        }
        for (int i = 0; i < frames; i++) {
            uint32_t f = (uint32_t)b->pos;
            float l = sample_at(b, f, 0), r = sample_at(b, f, 1);
            out[2 * i] += l * lv;
            out[2 * i + 1] += r * rv;
            b->pos += step;
            if (b->pos >= nframes) {
                if (b->looping) b->pos -= nframes;
                else { b->pos = 0; b->playing = 0; break; }
            }
        }
    }
    SDL_UnlockMutex(g_mix_lock);
    for (int i = 0; i < 2 * frames; i++) {
        if (out[i] > 1.0f) out[i] = 1.0f;
        else if (out[i] < -1.0f) out[i] = -1.0f;
    }
}

static void audio_open(void) {
    if (g_dev) return;
    g_mix_lock = SDL_CreateMutex();
    SDL_AudioSpec want = {0}, have;
    want.freq = MIX_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mix_cb;
    rt_host_affinity_begin();  /* SDL's audio thread runs no guest code */
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    rt_host_affinity_end();
    if (!g_dev) LOG("audio: SDL_OpenAudioDevice failed: %s (continuing silently)", SDL_GetError());
    else SDL_PauseAudioDevice(g_dev, 0);
}

static uint32_t buf_register(DSBuf *b) {
    SDL_LockMutex(g_mix_lock);
    size_t slot = 0;
    while (slot < g_nbufs && g_bufs[slot]) slot++;
    if (slot == g_nbufs) {
        if (g_nbufs == g_bufcap) {
            size_t cap = g_bufcap ? g_bufcap * 2 : 64;
            DSBuf **bufs = realloc(g_bufs, cap * sizeof *g_bufs);
            if (!bufs) {
                SDL_UnlockMutex(g_mix_lock);
                rt_fatal("out of memory growing DirectSound buffer table");
            }
            memset(bufs + g_bufcap, 0, (cap - g_bufcap) * sizeof *bufs);
            g_bufs = bufs;
            g_bufcap = cap;
        }
        g_nbufs++;
    }
    g_bufs[slot] = b;
    SDL_UnlockMutex(g_mix_lock);
    b->guest = hcom_new(&k_dsb, b);
    return b->guest;
}

static void buf_destroy(DSBuf *b) {
    SDL_LockMutex(g_mix_lock);
    for (size_t k = 0; k < g_nbufs; k++)
        if (g_bufs[k] == b) g_bufs[k] = NULL;
    SDL_UnlockMutex(g_mix_lock);
    if (--b->d->refs == 0) {
        if (b->d->data) gfree(b->d->data);
        free(b->d);
    }
    free(b);
}

/* ---------------- IDirectSoundBuffer8 ---------------- */
#define B ((DSBuf *)hcom_host(ARG(0)))
SHIM(dsb_QueryInterface) {
    /* IDirectSoundBuffer8 / IDirectSoundBuffer: same object. 3D/notify: not supported yet. */
    uint8_t iid[16];
    memcpy(iid, MEM + ARG(1), 16);
    uint32_t d1 = iid[0] | iid[1] << 8 | iid[2] << 16 | (uint32_t)iid[3] << 24;
    if (d1 == 0x279AFA85u || d1 == 0x6825A449u || d1 == 0) { /* IDirectSoundBuffer, IDirectSoundBuffer8, IUnknown */
        hcom_addref(ARG(0));
        M32(ARG(2)) = ARG(0);
        RET(0, 3);
    }
    if (d1 == 0x279AFA86u && (B->flags & DSBCAPS_CTRL3D) && !B->primary) {  /* IDirectSound3DBuffer */
        hcom_addref(B->guest);  /* the view keeps its buffer alive */
        M32(ARG(2)) = hcom_new(&k_ds3db, B);
        RET(0, 3);
    }
    if (d1 == 0x279AFA84u && B->primary) {  /* IDirectSound3DListener */
        hcom_addref(B->guest);
        M32(ARG(2)) = hcom_new(&k_ds3dl, B);
        RET(0, 3);
    }
    LOG("dsound: QueryInterface for unsupported %08x", d1);
    M32(ARG(2)) = 0;
    RET(E_NOINTERFACE_, 3);
}
SHIM(dsb_AddRef) { RET(hcom_addref(ARG(0)), 1); }
SHIM(dsb_Release) {
    uint32_t left;
    DSBuf *b = B;
    if (hcom_release(ARG(0), &left)) buf_destroy(b);
    RET(left, 1);
}
SHIM(dsb_GetCaps) {
    uint32_t p = ARG(1);
    M32(p + 4) = B->flags;
    M32(p + 8) = B->d->size;
    M32(p + 12) = 0;
    M32(p + 16) = 0;
    RET(0, 2);
}
SHIM(dsb_GetCurrentPosition) {
    DSBuf *b = B;
    SDL_LockMutex(g_mix_lock);
    uint32_t bpf = (uint32_t)(b->channels * b->bits / 8);
    uint32_t play = b->primary || !b->d->size ? 0 : ((uint32_t)b->pos * bpf) % b->d->size;
    SDL_UnlockMutex(g_mix_lock);
    uint32_t ahead = b->rate * bpf / 50; /* 20 ms */
    if (ARG(1)) M32(ARG(1)) = play;
    if (ARG(2)) M32(ARG(2)) = b->d->size ? (play + ahead) % b->d->size : 0;
    RET(0, 3);
}
SHIM(dsb_GetFormat) {
    DSBuf *b = B;
    uint32_t p = ARG(1);
    if (p) {
        M16(p) = 1;
        M16(p + 2) = (uint16_t)b->channels;
        M32(p + 4) = (uint32_t)b->rate;
        M16(p + 12) = (uint16_t)(b->channels * b->bits / 8);
        M32(p + 8) = (uint32_t)b->rate * M16(p + 12);
        M16(p + 14) = (uint16_t)b->bits;
        if (ARG(2) >= 18) M16(p + 16) = 0;
    }
    if (ARG(3)) M32(ARG(3)) = 18;
    RET(0, 4);
}
SHIM(dsb_GetVolume) { M32(ARG(1)) = (uint32_t)B->volume; RET(0, 2); }
SHIM(dsb_GetPan) { M32(ARG(1)) = (uint32_t)B->pan; RET(0, 2); }
SHIM(dsb_GetFrequency) { M32(ARG(1)) = B->freq ? B->freq : (uint32_t)B->rate; RET(0, 2); }
SHIM(dsb_GetStatus) {
    DSBuf *b = B;
    M32(ARG(1)) = (b->playing ? 1u : 0u) | (b->playing && b->looping ? 4u : 0u);
    RET(0, 2);
}
SHIM(dsb_Initialize) { RET(0x88780082u, 3); } /* DSERR_ALREADYINITIALIZED */
SHIM(dsb_Lock) {
    /* (this, offset, bytes, p1, n1, p2, n2, flags) */
    DSBuf *b = B;
    uint32_t size = b->d->size;
    uint32_t off = ARG(1), n = ARG(2), fl = ARG(7);
    if (!size) { RET(0x88780032u, 8); }
    if (fl & 1) { /* DSBLOCK_FROMWRITECURSOR */
        uint32_t bpf = (uint32_t)(b->channels * b->bits / 8);
        off = (((uint32_t)b->pos * bpf) + (uint32_t)b->rate * bpf / 50) % size;
    }
    if (fl & 2) n = size; /* DSBLOCK_ENTIREBUFFER */
    if (n > size) n = size;
    off %= size;
    uint32_t n1 = n <= size - off ? n : size - off;
    M32(ARG(3)) = b->d->data + off;
    M32(ARG(4)) = n1;
    if (ARG(5)) M32(ARG(5)) = n1 < n ? b->d->data : 0;
    if (ARG(6)) M32(ARG(6)) = n - n1;
    RET(0, 8);
}
SHIM(dsb_Play) {
    DSBuf *b = B;
    SDL_LockMutex(g_mix_lock);
    b->playing = 1;
    b->looping = (ARG(3) & 1) != 0;
    SDL_UnlockMutex(g_mix_lock);
    RET(0, 4);
}
SHIM(dsb_SetCurrentPosition) {
    DSBuf *b = B;
    uint32_t bpf = (uint32_t)(b->channels * b->bits / 8);
    SDL_LockMutex(g_mix_lock);
    b->pos = bpf ? (double)(ARG(1) / bpf) : 0;
    SDL_UnlockMutex(g_mix_lock);
    RET(0, 2);
}
SHIM(dsb_SetFormat) {
    DSBuf *b = B;
    uint32_t w = ARG(1);
    if (w) { b->channels = M16(w + 2); b->rate = (int)M32(w + 4); b->bits = M16(w + 14); }
    RET(0, 2);
}
SHIM(dsb_SetVolume) { B->volume = (int32_t)ARG(1); RET(0, 2); }
SHIM(dsb_SetPan) { B->pan = (int32_t)ARG(1); RET(0, 2); }
SHIM(dsb_SetFrequency) { B->freq = ARG(1); RET(0, 2); }
SHIM(dsb_Stop) {
    SDL_LockMutex(g_mix_lock);
    B->playing = 0;
    SDL_UnlockMutex(g_mix_lock);
    RET(0, 1);
}
SHIM(dsb_Unlock) { RET(0, 5); }
SHIM(dsb_Restore) { RET(0, 1); }
SHIM(dsb_SetFX) { RET(E_NOTIMPL_, 4); }
SHIM(dsb_AcquireResources) { RET(0, 4); }
SHIM(dsb_GetObjectInPath) { RET(E_NOTIMPL_, 5); }
#undef B

static const ShimFn k_dsb_fns[] = {
    shim_dsb_QueryInterface, shim_dsb_AddRef, shim_dsb_Release, shim_dsb_GetCaps, shim_dsb_GetCurrentPosition,
    shim_dsb_GetFormat, shim_dsb_GetVolume, shim_dsb_GetPan, shim_dsb_GetFrequency, shim_dsb_GetStatus,
    shim_dsb_Initialize, shim_dsb_Lock, shim_dsb_Play, shim_dsb_SetCurrentPosition, shim_dsb_SetFormat,
    shim_dsb_SetVolume, shim_dsb_SetPan, shim_dsb_SetFrequency, shim_dsb_Stop, shim_dsb_Unlock,
    shim_dsb_Restore, shim_dsb_SetFX, shim_dsb_AcquireResources, shim_dsb_GetObjectInPath,
};

/* ---------------- IDirectSound3DBuffer / IDirectSound3DListener ----------------
 * Settings apply immediately (DS3D_DEFERRED is treated as DS3D_IMMEDIATE). The
 * view object's host pointer is the DSBuf it belongs to. */
#define V3(a, i) MF32((a) + 4 * (i))
static void put3(uint32_t a, const float *v) { for (int i = 0; i < 3; i++) MF32(a + 4 * i) = v[i]; }
static void get3(uint32_t a, float *v) { for (int i = 0; i < 3; i++) v[i] = MF32(a + 4 * i); }
static void view_release_owner(DSBuf *b) { uint32_t left; if (hcom_release(b->guest, &left)) buf_destroy(b); }
#define L3 SDL_LockMutex(g_mix_lock)
#define U3 SDL_UnlockMutex(g_mix_lock)
SHIM(d3b_QueryInterface) { if (ARG(2)) M32(ARG(2)) = 0; RET(E_NOINTERFACE_, 3); }
SHIM(d3b_AddRef) { RET(hcom_addref(ARG(0)), 1); }
SHIM(d3b_Release) {
    if (!ARG(0)) {
        static int warned;
        if (!__atomic_exchange_n(&warned, 1, __ATOMIC_RELAXED))
            LOG("dsound: ignored IDirectSound3DBuffer::Release(NULL)");
        RET(0, 1);
    }
    DSBuf *b = hcom_host(ARG(0));
    uint32_t left;
    if (hcom_release(ARG(0), &left)) view_release_owner(b);
    RET(left, 1);
}
SHIM(d3b_GetAllParameters) {
    DSBuf *b = hcom_host(ARG(0));
    uint32_t p = ARG(1);
    memset(MEM + p + 4, 0, 60);
    M32(p) = 64;
    put3(p + 4, b->p3);
    M32(p + 28) = 360; M32(p + 32) = 360;
    MF32(p + 44) = 1.0f;
    MF32(p + 52) = b->dmin; MF32(p + 56) = b->dmax;
    M32(p + 60) = b->mode3d;
    RET(0, 2);
}
SHIM(d3b_GetConeAngles) { if (ARG(1)) M32(ARG(1)) = 360; if (ARG(2)) M32(ARG(2)) = 360; RET(0, 3); }
SHIM(d3b_GetConeOrientation) { MF32(ARG(1)) = 0; MF32(ARG(1) + 4) = 0; MF32(ARG(1) + 8) = 1.0f; RET(0, 2); }
SHIM(d3b_GetConeOutsideVolume) { M32(ARG(1)) = 0; RET(0, 2); }
SHIM(d3b_GetMaxDistance) { DSBuf *b = hcom_host(ARG(0)); MF32(ARG(1)) = b->dmax; RET(0, 2); }
SHIM(d3b_GetMinDistance) { DSBuf *b = hcom_host(ARG(0)); MF32(ARG(1)) = b->dmin; RET(0, 2); }
SHIM(d3b_GetMode) { DSBuf *b = hcom_host(ARG(0)); M32(ARG(1)) = b->mode3d; RET(0, 2); }
SHIM(d3b_GetPosition) { DSBuf *b = hcom_host(ARG(0)); put3(ARG(1), b->p3); RET(0, 2); }
SHIM(d3b_GetVelocity) { memset(MEM + ARG(1), 0, 12); RET(0, 2); }
SHIM(d3b_SetAllParameters) {
    DSBuf *b = hcom_host(ARG(0));
    uint32_t p = ARG(1);
    L3;
    get3(p + 4, b->p3);
    b->dmin = MF32(p + 52); b->dmax = MF32(p + 56);
    b->mode3d = M32(p + 60);
    U3;
    RET(0, 3);
}
SHIM(d3b_SetConeAngles) { RET(0, 4); }
SHIM(d3b_SetConeOrientation) { RET(0, 5); }
SHIM(d3b_SetConeOutsideVolume) { RET(0, 3); }
SHIM(d3b_SetMaxDistance) { DSBuf *b = hcom_host(ARG(0)); L3; b->dmax = ARGF(1); U3; RET(0, 3); }
SHIM(d3b_SetMinDistance) { DSBuf *b = hcom_host(ARG(0)); L3; b->dmin = ARGF(1); U3; RET(0, 3); }
SHIM(d3b_SetMode) { DSBuf *b = hcom_host(ARG(0)); L3; b->mode3d = ARG(1); U3; RET(0, 3); }
SHIM(d3b_SetPosition) {
    DSBuf *b = hcom_host(ARG(0));
    L3; b->p3[0] = ARGF(1); b->p3[1] = ARGF(2); b->p3[2] = ARGF(3); U3;
    RET(0, 5);
}
SHIM(d3b_SetVelocity) { RET(0, 5); }
static const ShimFn k_d3b_fns[] = {
    shim_d3b_QueryInterface, shim_d3b_AddRef, shim_d3b_Release, shim_d3b_GetAllParameters, shim_d3b_GetConeAngles,
    shim_d3b_GetConeOrientation, shim_d3b_GetConeOutsideVolume, shim_d3b_GetMaxDistance, shim_d3b_GetMinDistance,
    shim_d3b_GetMode, shim_d3b_GetPosition, shim_d3b_GetVelocity, shim_d3b_SetAllParameters, shim_d3b_SetConeAngles,
    shim_d3b_SetConeOrientation, shim_d3b_SetConeOutsideVolume, shim_d3b_SetMaxDistance, shim_d3b_SetMinDistance,
    shim_d3b_SetMode, shim_d3b_SetPosition, shim_d3b_SetVelocity,
};

SHIM(d3l_QueryInterface) { if (ARG(2)) M32(ARG(2)) = 0; RET(E_NOINTERFACE_, 3); }
SHIM(d3l_AddRef) { RET(hcom_addref(ARG(0)), 1); }
SHIM(d3l_Release) {
    if (!ARG(0)) {
        static int warned;
        if (!__atomic_exchange_n(&warned, 1, __ATOMIC_RELAXED))
            LOG("dsound: ignored IDirectSound3DListener::Release(NULL)");
        RET(0, 1);
    }
    DSBuf *b = hcom_host(ARG(0));
    uint32_t left;
    if (hcom_release(ARG(0), &left)) view_release_owner(b);
    RET(left, 1);
}
SHIM(d3l_GetAllParameters) {
    uint32_t p = ARG(1);
    memset(MEM + p + 4, 0, 60);
    M32(p) = 64;
    put3(p + 4, g_lis.pos); put3(p + 16, g_lis.vel); put3(p + 28, g_lis.front); put3(p + 40, g_lis.top);
    MF32(p + 52) = g_lis.dist_factor; MF32(p + 56) = g_lis.rolloff; MF32(p + 60) = g_lis.doppler;
    RET(0, 2);
}
SHIM(d3l_GetDistanceFactor) { MF32(ARG(1)) = g_lis.dist_factor; RET(0, 2); }
SHIM(d3l_GetDopplerFactor) { MF32(ARG(1)) = g_lis.doppler; RET(0, 2); }
SHIM(d3l_GetOrientation) { put3(ARG(1), g_lis.front); put3(ARG(2), g_lis.top); RET(0, 3); }
SHIM(d3l_GetPosition) { put3(ARG(1), g_lis.pos); RET(0, 2); }
SHIM(d3l_GetRolloffFactor) { MF32(ARG(1)) = g_lis.rolloff; RET(0, 2); }
SHIM(d3l_GetVelocity) { put3(ARG(1), g_lis.vel); RET(0, 2); }
SHIM(d3l_SetAllParameters) {
    uint32_t p = ARG(1);
    L3;
    get3(p + 4, g_lis.pos); get3(p + 16, g_lis.vel); get3(p + 28, g_lis.front); get3(p + 40, g_lis.top);
    g_lis.dist_factor = MF32(p + 52); g_lis.rolloff = MF32(p + 56); g_lis.doppler = MF32(p + 60);
    U3;
    RET(0, 3);
}
SHIM(d3l_SetDistanceFactor) { g_lis.dist_factor = ARGF(1); RET(0, 3); }
SHIM(d3l_SetDopplerFactor) { g_lis.doppler = ARGF(1); RET(0, 3); }
SHIM(d3l_SetOrientation) {
    L3;
    for (int i = 0; i < 3; i++) { g_lis.front[i] = ARGF(1 + i); g_lis.top[i] = ARGF(4 + i); }
    U3;
    RET(0, 8);
}
SHIM(d3l_SetPosition) { L3; for (int i = 0; i < 3; i++) g_lis.pos[i] = ARGF(1 + i); U3; RET(0, 5); }
SHIM(d3l_SetRolloffFactor) { L3; g_lis.rolloff = ARGF(1); U3; RET(0, 3); }
SHIM(d3l_SetVelocity) { RET(0, 5); }
SHIM(d3l_CommitDeferredSettings) { RET(0, 1); }
static const ShimFn k_d3l_fns[] = {
    shim_d3l_QueryInterface, shim_d3l_AddRef, shim_d3l_Release, shim_d3l_GetAllParameters, shim_d3l_GetDistanceFactor,
    shim_d3l_GetDopplerFactor, shim_d3l_GetOrientation, shim_d3l_GetPosition, shim_d3l_GetRolloffFactor,
    shim_d3l_GetVelocity, shim_d3l_SetAllParameters, shim_d3l_SetDistanceFactor, shim_d3l_SetDopplerFactor,
    shim_d3l_SetOrientation, shim_d3l_SetPosition, shim_d3l_SetRolloffFactor, shim_d3l_SetVelocity,
    shim_d3l_CommitDeferredSettings,
};
#undef L3
#undef U3
#undef V3

/* ---------------- IDirectSound8 ---------------- */
SHIM(ds_QueryInterface) { hcom_addref(ARG(0)); M32(ARG(2)) = ARG(0); RET(0, 3); }
SHIM(ds_AddRef) { RET(hcom_addref(ARG(0)), 1); }
SHIM(ds_Release) { uint32_t left; hcom_release(ARG(0), &left); RET(left, 1); }
SHIM(ds_CreateSoundBuffer) {
    uint32_t desc = ARG(1);
    DSBuf *b = calloc(1, sizeof *b);
    b->d = calloc(1, sizeof *b->d);
    b->d->refs = 1;
    b->flags = M32(desc + 4);
    b->primary = (b->flags & 1) != 0;
    b->channels = 2; b->bits = 16; b->rate = 44100;
    b->dmin = 1.0f; b->dmax = 1e9f;  /* DS3D_DEFAULTMINDISTANCE / MAXDISTANCE */
    uint32_t wfx = M32(desc + 16);
    if (wfx) { b->channels = M16(wfx + 2); b->rate = (int)M32(wfx + 4); b->bits = M16(wfx + 14); }
    if (!b->primary) {
        b->d->size = M32(desc + 8);
        b->d->data = gmalloc(b->d->size ? b->d->size : 16);
        memset(MEM + b->d->data, b->bits == 8 ? 0x80 : 0, b->d->size);
    }
    TRACE("dsound: buffer %u bytes %dch %dHz %dbit flags %x", b->d->size, b->channels, b->rate, b->bits, b->flags);
    M32(ARG(2)) = buf_register(b);
    RET(0, 4);
}
SHIM(ds_GetCaps) {
    uint32_t p = ARG(1);
    /* The game can pass an uninitialized dwSize here. DSCAPS is 96 bytes in
     * this 32-bit ABI; never trust the guest value as a memory write length. */
    memset(MEM + p, 0, 96);
    M32(p) = 96;
    M32(p + 4) = 0x00000F5F; /* continuous rate, 8/16 bit, mono/stereo, secondary */
    M32(p + 8) = 100;
    M32(p + 12) = 200000;
    RET(0, 2);
}
SHIM(ds_DuplicateSoundBuffer) {
    DSBuf *src = hcom_host(ARG(1));
    DSBuf *b = calloc(1, sizeof *b);
    *b = *src;
    b->d->refs++;
    b->playing = 0;
    b->pos = 0;
    M32(ARG(2)) = buf_register(b);
    RET(0, 3);
}
SHIM(ds_SetCooperativeLevel) { RET(0, 3); }
SHIM(ds_Compact) { RET(0, 1); }
SHIM(ds_GetSpeakerConfig) { M32(ARG(1)) = 4; RET(0, 2); } /* DSSPEAKER_STEREO */
SHIM(ds_SetSpeakerConfig) { RET(0, 2); }
SHIM(ds_Initialize) { RET(0, 2); }
SHIM(ds_VerifyCertification) { M32(ARG(1)) = 0; RET(0, 2); }

static const ShimFn k_ds_fns[] = {
    shim_ds_QueryInterface, shim_ds_AddRef, shim_ds_Release, shim_ds_CreateSoundBuffer, shim_ds_GetCaps,
    shim_ds_DuplicateSoundBuffer, shim_ds_SetCooperativeLevel, shim_ds_Compact, shim_ds_GetSpeakerConfig,
    shim_ds_SetSpeakerConfig, shim_ds_Initialize, shim_ds_VerifyCertification,
};

SHIM(DirectSoundCreate8) {
    static int init;
#if defined(GAME_OBLIVION) && defined(__ANDROID__)
    /* The Android port keeps DirectSound disabled; desktop SDL audio uses
     * the software mixer below. */
    if (ARG(1)) M32(ARG(1)) = 0;
    LOG("DirectSoundCreate: sound disabled on this platform (DSERR_NODRIVER)");
    RET(0x88780078u, 3);
#endif
    if (!init) {
        hcom_class_init(&k_ds8, "IDirectSound8", k_ds_fns, sizeof k_ds_fns / sizeof k_ds_fns[0]);
        hcom_class_init(&k_dsb, "IDirectSoundBuffer8", k_dsb_fns, sizeof k_dsb_fns / sizeof k_dsb_fns[0]);
        hcom_class_init(&k_ds3db, "IDirectSound3DBuffer", k_d3b_fns, sizeof k_d3b_fns / sizeof k_d3b_fns[0]);
        hcom_class_init(&k_ds3dl, "IDirectSound3DListener", k_d3l_fns, sizeof k_d3l_fns / sizeof k_d3l_fns[0]);
        audio_open();
        init = 1;
    }
    M32(ARG(1)) = hcom_new(&k_ds8, NULL);
    LOG("DirectSoundCreate8");
    RET(0, 3);
}

/* IDirectSound (not 8) has the same vtable prefix; Bink and older code create it this way. */
SHIM(DirectSoundCreate) { M32(ARG(1)) = 0; shim_DirectSoundCreate8(c); }
/* (callback, context): one device, the default one */
SHIM(DirectSoundEnumerateA) {
    uint32_t args[4] = {0, gstrdup("Primary Sound Driver"), gstrdup(""), ARG(1)};
    rt_invoke(c, ARG(0), 4, args);
    RET(0, 2);
}

static const ShimDef defs[] = {
    {"DSOUND.dll", "#1", shim_DirectSoundCreate},
    {"DSOUND.dll", "DirectSoundCreate", shim_DirectSoundCreate},
    {"DSOUND.dll", "#2", shim_DirectSoundEnumerateA},
    {"DSOUND.dll", "DirectSoundEnumerateA", shim_DirectSoundEnumerateA},
    {"DSOUND.dll", "#11", shim_DirectSoundCreate8},
    {"DSOUND.dll", "DirectSoundCreate8", shim_DirectSoundCreate8},
    {NULL, NULL, NULL}
};

void dsound_init(void) { rt_register_shims(defs); }
