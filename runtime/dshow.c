/* Small DirectShow filter-graph shim for Oblivion's MP3 music. The macOS
 * build uses AVAudioPlayer to play the file selected by IGraphBuilder. */
#include "hcom.h"

#ifdef __APPLE__
#include <limits.h>
#include <math.h>

#define S_OK 0u
#define E_FAIL 0x80004005u
#define E_NOINTERFACE 0x80004002u
#define E_NOTIMPL 0x80004001u
#define E_POINTER 0x80004003u
#define GRAPH_STOPPED 0
#define GRAPH_PAUSED 1
#define GRAPH_RUNNING 2

static const uint8_t k_clsid_filter_graph[16] = {
    0xB3, 0xEB, 0x36, 0xE4, 0x4F, 0x52, 0xCE, 0x11,
    0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70};
static const uint8_t k_iid_unknown[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
static const uint8_t k_iid_graph_builder[16] = {
    0xA9, 0x68, 0xA8, 0x56, 0xD4, 0x0A, 0xCE, 0x11,
    0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70};
static const uint8_t k_iid_media_control[16] = {
    0xB1, 0x68, 0xA8, 0x56, 0xD4, 0x0A, 0xCE, 0x11,
    0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70};
static const uint8_t k_iid_media_position[16] = {
    0xB2, 0x68, 0xA8, 0x56, 0xD4, 0x0A, 0xCE, 0x11,
    0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70};
static const uint8_t k_iid_basic_audio[16] = {
    0xB3, 0x68, 0xA8, 0x56, 0xD4, 0x0A, 0xCE, 0x11,
    0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70};
static const uint8_t k_iid_media_event[16] = {
    0xB6, 0x68, 0xA8, 0x56, 0xD4, 0x0A, 0xCE, 0x11,
    0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70};

typedef struct {
    uint32_t builder, control, media_position, basic_audio, media_event;
    unsigned views;
    int state, volume_mb, balance;
    char path[PATH_MAX];
    void *player;
} MediaGraph;

static HComClass k_graph_builder, k_media_control, k_media_position, k_basic_audio, k_media_event;
static int g_classes_ready;

extern void *mac_audio_open(const char *path);
extern int mac_audio_play(void *handle);
extern void mac_audio_pause(void *handle);
extern void mac_audio_stop(void *handle);
extern void mac_audio_set_volume(void *handle, float volume);
extern void mac_audio_release(void *handle);
extern double mac_audio_duration(void *handle);
extern double mac_audio_position(void *handle);
extern void mac_audio_set_position(void *handle, double position);

static uint32_t graph_qi(uint32_t self, uint32_t iid, uint32_t out) {
    if (!out) return E_POINTER;
    MediaGraph *g = hcom_host(self);
    if (!g || !iid) { M32(out) = 0; return E_NOINTERFACE; }
    const uint8_t *want = MEM + iid;
    uint32_t *slot = NULL;
    HComClass *klass = NULL;
    if (!memcmp(want, k_iid_unknown, 16)) {
        hcom_addref(self);
        M32(out) = self;
        return S_OK;
    }
    if (!memcmp(want, k_iid_graph_builder, 16)) { slot = &g->builder; klass = &k_graph_builder; }
    else if (!memcmp(want, k_iid_media_control, 16)) { slot = &g->control; klass = &k_media_control; }
    else if (!memcmp(want, k_iid_media_position, 16)) { slot = &g->media_position; klass = &k_media_position; }
    else if (!memcmp(want, k_iid_basic_audio, 16)) { slot = &g->basic_audio; klass = &k_basic_audio; }
    else if (!memcmp(want, k_iid_media_event, 16)) { slot = &g->media_event; klass = &k_media_event; }
    else { M32(out) = 0; return E_NOINTERFACE; }
    if (*slot) hcom_addref(*slot);
    else { *slot = hcom_new(klass, g); g->views++; }
    M32(out) = *slot;
    return S_OK;
}

static uint32_t graph_addref(uint32_t self) { return hcom_addref(self); }

static void graph_stop(MediaGraph *g) {
    if (!g) return;
    if (g->player) mac_audio_stop(g->player);
    g->state = GRAPH_STOPPED;
}

static uint32_t graph_release(uint32_t self) {
    MediaGraph *g = hcom_host(self);
    uint32_t left = 0;
    int died = hcom_release(self, &left);
    if (died && g) {
        if (g->builder == self) g->builder = 0;
        if (g->control == self) g->control = 0;
        if (g->media_position == self) g->media_position = 0;
        if (g->basic_audio == self) g->basic_audio = 0;
        if (g->media_event == self) g->media_event = 0;
        if (g->views && --g->views == 0) {
            graph_stop(g);
            if (g->player) mac_audio_release(g->player);
            free(g);
        }
    }
    return left;
}

static int wide_path(uint32_t src, char *dst, size_t cap) {
    if (!src) return 0;
    size_t out = 0;
    for (size_t i = 0; i + 1 < cap; i++) {
        uint32_t c = M16(src + (uint32_t)(2 * i));
        if (!c) { dst[out] = 0; return out != 0; }
        if (c < 0x80) {
            if (out + 1 >= cap) break;
            dst[out++] = (char)c;
        } else if (c < 0x800) {
            if (out + 2 >= cap) break;
            dst[out++] = (char)(0xC0 | (c >> 6));
            dst[out++] = (char)(0x80 | (c & 0x3F));
        } else {
            if (out + 3 >= cap) break;
            dst[out++] = (char)(0xE0 | (c >> 12));
            dst[out++] = (char)(0x80 | ((c >> 6) & 0x3F));
            dst[out++] = (char)(0x80 | (c & 0x3F));
        }
    }
    dst[0] = 0;
    return 0;
}

static uint32_t graph_render_file(MediaGraph *g, uint32_t guest_path) {
    char win_path[1024], host_path[1024], absolute_path[PATH_MAX];
    if (!g || !wide_path(guest_path, win_path, sizeof win_path) ||
        !rt_host_path(win_path, host_path, sizeof host_path) ||
        !realpath(host_path, absolute_path)) {
        LOG("directshow: RenderFile could not resolve guest path");
        return E_FAIL;
    }
    graph_stop(g);
    if (g->player) { mac_audio_release(g->player); g->player = NULL; }
    g->player = mac_audio_open(absolute_path);
    if (!g->player) return E_FAIL;
    snprintf(g->path, sizeof g->path, "%s", absolute_path);
    mac_audio_set_volume(g->player, powf(10.0f, (float)g->volume_mb / 2000.0f));
    LOG("directshow: RenderFile %s", g->path);
    return S_OK;
}

static uint32_t graph_run(MediaGraph *g) {
    if (!g->player) return E_FAIL;
    int ok = mac_audio_play(g->player);
    if (ok) g->state = GRAPH_RUNNING;
    return ok ? S_OK : E_FAIL;
}

SHIM(graph_QueryInterface) { RET(graph_qi(ARG(0), ARG(1), ARG(2)), 3); }
SHIM(graph_AddRef) { RET(graph_addref(ARG(0)), 1); }
SHIM(graph_Release) { RET(graph_release(ARG(0)), 1); }
SHIM(gb_AddFilter) { RET(E_NOTIMPL, 3); }
SHIM(gb_RemoveFilter) { RET(E_NOTIMPL, 2); }
SHIM(gb_EnumFilters) { RET(E_NOTIMPL, 2); }
SHIM(gb_FindFilterByName) { RET(E_NOTIMPL, 3); }
SHIM(gb_ConnectDirect) { RET(E_NOTIMPL, 4); }
SHIM(gb_Reconnect) { RET(E_NOTIMPL, 2); }
SHIM(gb_Disconnect) { RET(E_NOTIMPL, 2); }
SHIM(gb_SetDefaultSyncSource) { RET(S_OK, 1); }
SHIM(gb_Connect) { RET(E_NOTIMPL, 3); }
SHIM(gb_Render) { RET(E_NOTIMPL, 2); }
SHIM(gb_RenderFile) { RET(graph_render_file(hcom_host(ARG(0)), ARG(1)), 3); }
SHIM(gb_AddSourceFilter) { RET(E_NOTIMPL, 4); }
SHIM(gb_SetLogFile) { RET(S_OK, 2); }
SHIM(gb_Abort) { RET(S_OK, 1); }
SHIM(gb_ShouldOperationContinue) { RET(S_OK, 1); }

SHIM(mc_GetTypeInfoCount) { if (ARG(1)) M32(ARG(1)) = 0; RET(S_OK, 2); }
SHIM(mc_GetTypeInfo) { RET(E_NOTIMPL, 4); }
SHIM(mc_GetIDsOfNames) { RET(E_NOTIMPL, 6); }
SHIM(mc_Invoke) { RET(E_NOTIMPL, 9); }
SHIM(mc_Run) { RET(graph_run(hcom_host(ARG(0))), 1); }
SHIM(mc_Pause) {
    MediaGraph *g = hcom_host(ARG(0));
    if (g && g->player) { mac_audio_pause(g->player); g->state = GRAPH_PAUSED; }
    RET(S_OK, 1);
}
SHIM(mc_Stop) { graph_stop(hcom_host(ARG(0))); RET(S_OK, 1); }
SHIM(mc_GetState) {
    MediaGraph *g = hcom_host(ARG(0));
    if (ARG(2)) M32(ARG(2)) = g ? (uint32_t)g->state : GRAPH_STOPPED;
    RET(S_OK, 3);
}
SHIM(mc_RenderFile) { RET(graph_render_file(hcom_host(ARG(0)), ARG(1)), 3); }
SHIM(mc_AddSourceFilter) { RET(E_NOTIMPL, 4); }
SHIM(mc_GetFilterCollection) { if (ARG(1)) M32(ARG(1)) = 0; RET(E_NOTIMPL, 2); }
SHIM(mc_GetRegFilterCollection) { if (ARG(1)) M32(ARG(1)) = 0; RET(E_NOTIMPL, 2); }
SHIM(mc_StopWhenReady) { graph_stop(hcom_host(ARG(0))); RET(S_OK, 1); }

SHIM(mp_GetTypeInfoCount) { if (ARG(1)) M32(ARG(1)) = 0; RET(S_OK, 2); }
SHIM(mp_GetTypeInfo) { RET(E_NOTIMPL, 4); }
SHIM(mp_GetIDsOfNames) { RET(E_NOTIMPL, 6); }
SHIM(mp_Invoke) { RET(E_NOTIMPL, 9); }
SHIM(mp_GetDuration) {
    MediaGraph *g = hcom_host(ARG(0));
    if (!ARG(1)) RET(E_POINTER, 2);
    MF64(ARG(1)) = g && g->player ? mac_audio_duration(g->player) : 0.0;
    RET(S_OK, 2);
}
SHIM(mp_SetCurrentPosition) {
    MediaGraph *g = hcom_host(ARG(0));
    double pos = MF64(c->r[4] + 8);
    if (g && g->player) mac_audio_set_position(g->player, pos);
    RET(S_OK, 3);
}
SHIM(mp_GetCurrentPosition) {
    MediaGraph *g = hcom_host(ARG(0));
    if (!ARG(1)) RET(E_POINTER, 2);
    MF64(ARG(1)) = g && g->player ? mac_audio_position(g->player) : 0.0;
    RET(S_OK, 2);
}
SHIM(mp_GetStopTime) {
    MediaGraph *g = hcom_host(ARG(0));
    if (!ARG(1)) RET(E_POINTER, 2);
    MF64(ARG(1)) = g && g->player ? mac_audio_duration(g->player) : 0.0;
    RET(S_OK, 2);
}
SHIM(mp_SetStopTime) { RET(S_OK, 3); }
SHIM(mp_GetPrerollTime) { if (!ARG(1)) RET(E_POINTER, 2); MF64(ARG(1)) = 0.0; RET(S_OK, 2); }
SHIM(mp_SetPrerollTime) { RET(S_OK, 3); }
SHIM(mp_SetRate) { RET(S_OK, 3); }
SHIM(mp_GetRate) { if (!ARG(1)) RET(E_POINTER, 2); MF64(ARG(1)) = 1.0; RET(S_OK, 2); }
SHIM(mp_CanSeekForward) { if (!ARG(1)) RET(E_POINTER, 2); M32(ARG(1)) = 1; RET(S_OK, 2); }
SHIM(mp_CanSeekBackward) { if (!ARG(1)) RET(E_POINTER, 2); M32(ARG(1)) = 1; RET(S_OK, 2); }

SHIM(ba_GetTypeInfoCount) { if (ARG(1)) M32(ARG(1)) = 0; RET(S_OK, 2); }
SHIM(ba_GetTypeInfo) { RET(E_NOTIMPL, 4); }
SHIM(ba_GetIDsOfNames) { RET(E_NOTIMPL, 6); }
SHIM(ba_Invoke) { RET(E_NOTIMPL, 9); }
SHIM(ba_GetBalance) { MediaGraph *g = hcom_host(ARG(0)); if (ARG(1)) M32(ARG(1)) = g ? (uint32_t)g->balance : 0; RET(S_OK, 2); }
SHIM(ba_SetBalance) { MediaGraph *g = hcom_host(ARG(0)); if (g) g->balance = (int32_t)ARG(1); RET(S_OK, 2); }
SHIM(ba_GetVolume) { MediaGraph *g = hcom_host(ARG(0)); if (ARG(1)) M32(ARG(1)) = g ? (uint32_t)g->volume_mb : -1000; RET(S_OK, 2); }
SHIM(ba_SetVolume) {
    MediaGraph *g = hcom_host(ARG(0));
    int32_t v = (int32_t)ARG(1);
    if (v < -10000 || v > 0) RET(0x80070057u, 2);
    if (g) {
        g->volume_mb = v;
        if (g->player) mac_audio_set_volume(g->player, powf(10.0f, (float)v / 2000.0f));
    }
    RET(S_OK, 2);
}

/* IMediaEvent::GetEvent reports E_ABORT when its timeout expires with no
 * event queued. The AVAudioPlayer-backed music graph loops indefinitely. */
SHIM(me_GetEventHandle) { RET(E_NOTIMPL, 2); }
SHIM(me_GetEvent) { RET(0x80004004u, 5); }
SHIM(me_WaitForCompletion) { RET(0x80004004u, 3); }
SHIM(me_CancelDefaultHandling) { RET(S_OK, 2); }
SHIM(me_RestoreDefaultHandling) { RET(S_OK, 2); }
SHIM(me_FreeEventParams) { RET(S_OK, 4); }

static const ShimFn k_graph_builder_fns[] = {
    shim_graph_QueryInterface, shim_graph_AddRef, shim_graph_Release,
    shim_gb_AddFilter, shim_gb_RemoveFilter, shim_gb_EnumFilters, shim_gb_FindFilterByName,
    shim_gb_ConnectDirect, shim_gb_Reconnect, shim_gb_Disconnect, shim_gb_SetDefaultSyncSource,
    shim_gb_Connect, shim_gb_Render, shim_gb_RenderFile, shim_gb_AddSourceFilter,
    shim_gb_SetLogFile, shim_gb_Abort, shim_gb_ShouldOperationContinue};
static const ShimFn k_media_control_fns[] = {
    shim_graph_QueryInterface, shim_graph_AddRef, shim_graph_Release,
    shim_mc_GetTypeInfoCount, shim_mc_GetTypeInfo, shim_mc_GetIDsOfNames, shim_mc_Invoke,
    shim_mc_Run, shim_mc_Pause, shim_mc_Stop, shim_mc_GetState, shim_mc_RenderFile,
    shim_mc_AddSourceFilter, shim_mc_GetFilterCollection, shim_mc_GetRegFilterCollection,
    shim_mc_StopWhenReady};
static const ShimFn k_media_position_fns[] = {
    shim_graph_QueryInterface, shim_graph_AddRef, shim_graph_Release,
    shim_mp_GetTypeInfoCount, shim_mp_GetTypeInfo, shim_mp_GetIDsOfNames, shim_mp_Invoke,
    shim_mp_GetDuration, shim_mp_SetCurrentPosition, shim_mp_GetCurrentPosition,
    shim_mp_GetStopTime, shim_mp_SetStopTime, shim_mp_GetPrerollTime, shim_mp_SetPrerollTime,
    shim_mp_SetRate, shim_mp_GetRate, shim_mp_CanSeekForward, shim_mp_CanSeekBackward};
static const ShimFn k_basic_audio_fns[] = {
    shim_graph_QueryInterface, shim_graph_AddRef, shim_graph_Release,
    shim_ba_GetTypeInfoCount, shim_ba_GetTypeInfo, shim_ba_GetIDsOfNames, shim_ba_Invoke,
    shim_ba_GetBalance, shim_ba_SetBalance, shim_ba_GetVolume, shim_ba_SetVolume};
static const ShimFn k_media_event_fns[] = {
    shim_graph_QueryInterface, shim_graph_AddRef, shim_graph_Release,
    shim_mc_GetTypeInfoCount, shim_mc_GetTypeInfo, shim_mc_GetIDsOfNames, shim_mc_Invoke,
    shim_me_GetEventHandle, shim_me_GetEvent, shim_me_WaitForCompletion,
    shim_me_CancelDefaultHandling, shim_me_RestoreDefaultHandling, shim_me_FreeEventParams};

static void graph_classes_init(void) {
    if (g_classes_ready) return;
    g_classes_ready = 1;
    hcom_class_init(&k_graph_builder, "IGraphBuilder", k_graph_builder_fns,
                    sizeof k_graph_builder_fns / sizeof k_graph_builder_fns[0]);
    hcom_class_init(&k_media_control, "IMediaControl", k_media_control_fns,
                    sizeof k_media_control_fns / sizeof k_media_control_fns[0]);
    hcom_class_init(&k_media_position, "IMediaPosition", k_media_position_fns,
                    sizeof k_media_position_fns / sizeof k_media_position_fns[0]);
    hcom_class_init(&k_basic_audio, "IBasicAudio", k_basic_audio_fns,
                    sizeof k_basic_audio_fns / sizeof k_basic_audio_fns[0]);
    hcom_class_init(&k_media_event, "IMediaEvent", k_media_event_fns,
                    sizeof k_media_event_fns / sizeof k_media_event_fns[0]);
}

/* Returns 1 if this CoCreateInstance call targets CLSID_FilterGraph. */
int dshow_create(uint32_t clsid, uint32_t iid, uint32_t *out, uint32_t *hr) {
    if (!clsid || memcmp(MEM + clsid, k_clsid_filter_graph, 16)) return 0;
    *out = 0;
    *hr = E_NOINTERFACE;
    if (!iid || (memcmp(MEM + iid, k_iid_graph_builder, 16) &&
                 memcmp(MEM + iid, k_iid_unknown, 16) &&
                 memcmp(MEM + iid, k_iid_media_control, 16) &&
                 memcmp(MEM + iid, k_iid_media_position, 16) &&
                 memcmp(MEM + iid, k_iid_basic_audio, 16) &&
                 memcmp(MEM + iid, k_iid_media_event, 16))) {
        return 1;
    }
    graph_classes_init();
    MediaGraph *g = calloc(1, sizeof *g);
    if (!g) { *hr = E_FAIL; return 1; }
    g->volume_mb = -1000;
    uint32_t first = hcom_new(&k_graph_builder, g);
    g->builder = first;
    g->views = 1;
    if (!memcmp(MEM + iid, k_iid_media_control, 16)) {
        g->control = hcom_new(&k_media_control, g);
        g->views++;
        *out = g->control;
    } else if (!memcmp(MEM + iid, k_iid_media_position, 16)) {
        g->media_position = hcom_new(&k_media_position, g);
        g->views++;
        *out = g->media_position;
    } else if (!memcmp(MEM + iid, k_iid_basic_audio, 16)) {
        g->basic_audio = hcom_new(&k_basic_audio, g);
        g->views++;
        *out = g->basic_audio;
    } else if (!memcmp(MEM + iid, k_iid_media_event, 16)) {
        g->media_event = hcom_new(&k_media_event, g);
        g->views++;
        *out = g->media_event;
    } else *out = first;
    *hr = S_OK;
    return 1;
}
#else
int dshow_create(uint32_t clsid, uint32_t iid, uint32_t *out, uint32_t *hr) {
    (void)clsid; (void)iid; (void)out; (void)hr;
    return 0;
}
#endif
