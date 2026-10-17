#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <ps5/kernel.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef int (*handle_fn)(void *);
typedef int (*read_fn)(void *, void *, size_t, void *, uint32_t);
typedef int (*nonvcl_fn)(void *, void *);
typedef int (*simple_fn)(void);
typedef int (*open_fn)(void **, const void *, uint8_t);
typedef int (*query_fn)(void *);
typedef int (*set_encode_fn)(const void *, void *);
typedef int (*set_global_encode_fn)(const void *);
typedef int (*allocate_video2_fn)(const void *, void *, void *);
typedef int (*free_video_fn)(void *, void *);
typedef int (*get_image_memory_fn)(void *, void **, size_t *);
typedef void (*cfg_init_fn)(void *, void *);
typedef void (*cfg_kind_fn)(void *, void *, uint32_t, uint32_t);
typedef void (*cfg_mode_fn)(void *, void *, uint32_t, uint32_t, uint32_t);
typedef void (*cfg_pair_fn)(void *, void *, uint32_t, uint32_t);
typedef void (*cfg_one_fn)(void *, void *, uint32_t);
typedef void (*proxy_ctor_fn)(void *);
typedef int (*proxy_init_fn)(void *);
typedef int (*proxy_open_fn)(void *, void **, const void *);
typedef void (*remoteplay_ctor_fn)(void *);
typedef void (*remoteplay_dtor_fn)(void *);
typedef int (*remoteplay_prepare_fn)(void *, const void *, uint32_t *, int *);
typedef int (*remoteplay_open_fn)(void *, int, int, int, int *, int *);
typedef void (*social_ctor_fn)(void *);
typedef void (*social_dtor_fn)(void *);
typedef int (*social_init_fn)(void *);
typedef void (*social_build_fn)(void *, void *);
typedef int (*social_open_native_fn)(void *);

typedef struct {
  uint32_t module;
  intptr_t module_base;
  intptr_t process_base;
  simple_fn initialize;
  open_fn open_video;
  open_fn open_audio;
  handle_fn start;
  handle_fn stop;
  handle_fn close;
  read_fn read_video;
  read_fn read_audio;
  nonvcl_fn get_non_vcl;
  query_fn get_mode;
  set_encode_fn set_encode;
  allocate_video2_fn allocate_video2;
  free_video_fn free_video;
  get_image_memory_fn get_image_memory;
} capture_api;

static unsigned char video_data[1024 * 1024];
static unsigned char audio_data[64 * 1024];
static unsigned char proxy_storage[0x400] __attribute__((aligned(32)));
static int proxy_constructed;
static int proxy_initialized;
static volatile unsigned status_open_attempts;
static volatile unsigned status_frames;
static volatile int status_init_result;
static volatile int status_open_result;
static volatile int status_start_result;
static volatile int status_read_result;
static volatile int status_encode_result;
static volatile int status_encode_buffer_result;
static volatile unsigned status_encode_attempts;
static volatile uint32_t status_budget_before;
static volatile uint32_t status_budget_video_before;
static volatile uint32_t status_budget_audio_before;
static volatile uint32_t status_budget_after;
static volatile int status_budget_updated;
static volatile int status_allocate_result;
static volatile int status_image_result;
static volatile size_t status_image_size;
static volatile uintptr_t status_image_function_offset;
static volatile uint64_t status_memory_a[4];
static volatile uint64_t status_memory_b[4];
static unsigned char capture_memory_a[0x20];
static unsigned char capture_memory_b[0x20];
static int capture_memory_owned;
static unsigned char *remoteplay_object __attribute__((unused));
static unsigned char *social_object __attribute__((unused));
static unsigned char social_parameters[0x2e8] __attribute__((unused));
static volatile int status_remoteplay_build_result;

#ifndef STREAM8_PS5_PORT
#define STREAM8_PS5_PORT 9070
#endif
#ifndef STREAM8_REQUIRE_IDR
#define STREAM8_REQUIRE_IDR 0
#endif
#ifndef STREAM8_AVCAP_PROFILE
#define STREAM8_AVCAP_PROFILE 1U
#endif
#ifndef STREAM8_AUTONOMOUS_CONFIG
#define STREAM8_AUTONOMOUS_CONFIG 0
#endif
#ifndef STREAM8_REMOTEPLAY_NATIVE
#define STREAM8_REMOTEPLAY_NATIVE 0
#endif
#ifndef STREAM8_SOCIAL_NATIVE
#define STREAM8_SOCIAL_NATIVE 0
#endif
#ifndef STREAM8_SOCIAL_PARAMETERS
#define STREAM8_SOCIAL_PARAMETERS 0
#endif
#ifndef STREAM8_RESOLUTION_INDEX
#define STREAM8_RESOLUTION_INDEX 2
#endif
#ifndef STREAM8_FPS
#define STREAM8_FPS 60
#endif
#ifndef STREAM8_OPEN_BITRATE_BPS
#define STREAM8_OPEN_BITRATE_BPS 8936000U
#endif
#ifndef STREAM8_OPEN_VBV_BPS
#define STREAM8_OPEN_VBV_BPS 10723200U
#endif
#ifndef STREAM8_RUNTIME_BITRATE_BPS
#define STREAM8_RUNTIME_BITRATE_BPS 30000000U
#endif
#ifndef STREAM8_RUNTIME_VBV_BPS
#define STREAM8_RUNTIME_VBV_BPS 36000000U
#endif
#ifndef STREAM8_NATIVE_BUFFER_UPDATE
#define STREAM8_NATIVE_BUFFER_UPDATE 0
#endif
#ifndef STREAM8_GAME_LIVE_RATE_BUDGET
#define STREAM8_GAME_LIVE_RATE_BUDGET 0
#endif
#define STREAM8_READ_NOWAIT 1U
#define STREAM8_RESTART_AFTER_MS 1500U
#define STREAM8_INITIAL_GRACE_MS 5000U
#define STREAM8_REOPEN_DELAY_US 750000U
#define STREAM8_BAD_READ_LIMIT 120U
/* Passive recovery: after a stall the handle is closed and reopened with a
 * growing delay. The proxy shutdown/init pair is NOT called unless
 * STREAM8_RECYCLE_PROXY is set: on firmware 13.60 it ran inside the only
 * server thread with no timeout and could hang the whole payload. */
#ifndef STREAM8_RECYCLE_PROXY
#define STREAM8_RECYCLE_PROXY 0
#endif

#if STREAM8_SOCIAL_NATIVE
static int prepare_social_context(const capture_api *api) {
  social_ctor_fn construct = (social_ctor_fn)(api->process_base + 0x5040);
  social_init_fn initialize = (social_init_fn)(api->process_base + 0x50d0);
  social_build_fn build = (social_build_fn)(api->process_base + 0x56a0);
  social_open_native_fn open_native =
      (social_open_native_fn)(api->process_base + 0x54a0);
  social_object = malloc(0x60);
  if (!social_object) return -1;
  memset(social_object, 0, 0x60);
  construct(social_object);
  int result = initialize(social_object);
  status_init_result = result;
  if (result < 0 || !*(void **)(social_object + 8)) return -1;
  memset(social_parameters, 0, sizeof(social_parameters));
  build(NULL, social_parameters);
  status_remoteplay_build_result = 1;
  void *native_object = *(void **)(social_object + 8);
  result = open_native(native_object);
  status_open_result = result;
  status_start_result = *(void **)((unsigned char *)native_object + 0x10)
                            ? 0
                            : -1;
  return 0;
}

static void release_social_context(const capture_api *api) {
  if (!social_object) return;
  social_dtor_fn destroy = (social_dtor_fn)(api->process_base + 0x5130);
  destroy(social_object);
  free(social_object);
  social_object = NULL;
}
#endif
#define STREAM8_BACKOFF_MIN_MS 1000U
#define STREAM8_BACKOFF_MAX_MS 5000U

#define AVCAP_ERR_EMPTY 0x81950002U
#define AVCAP_ERR_OVERRUN 0x81950004U
#define AVCAP_ERR_SESSION_END 0x81950008U
#define AVCAP_ALREADY_INITIALIZED 0x819b0007U

static uint64_t monotonic_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
  return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static int resolve_capture_api(capture_api *api) {
  memset(api, 0, sizeof(*api));
  if (kernel_dynlib_handle(getpid(), "libSceAvcap2.sprx", &api->module))
    return -1;
  api->module_base = kernel_dynlib_mapbase_addr(getpid(), api->module);
  api->process_base = kernel_dynlib_mapbase_addr(getpid(), 0);
  api->initialize = (simple_fn)kernel_dynlib_resolve(
      getpid(), api->module, "svzPXluOz8U");
  api->open_video = (open_fn)kernel_dynlib_resolve(
      getpid(), api->module, "4jwUgirLFrA");
  api->open_audio = (open_fn)kernel_dynlib_resolve(
      getpid(), api->module, "3nHpE7Dp5SE");
  api->start = (handle_fn)kernel_dynlib_resolve(
      getpid(), api->module, "8CGNCwBsItI");
  api->stop = (handle_fn)kernel_dynlib_resolve(
      getpid(), api->module, "Z5dKK0xnQ8g");
  api->close = (handle_fn)kernel_dynlib_resolve(
      getpid(), api->module, "tR7gPe1i8hw");
  api->read_video = (read_fn)kernel_dynlib_resolve(
      getpid(), api->module, "pN0hIQZsMzc");
  api->read_audio = (read_fn)kernel_dynlib_resolve(
      getpid(), api->module, "WhsHggqdwcg");
  api->get_non_vcl = (nonvcl_fn)kernel_dynlib_resolve(
      getpid(), api->module, "NeKlrUv+lbY");
  api->get_mode = (query_fn)kernel_dynlib_resolve(
      getpid(), api->module, "DHFDHdKAr9U");
  api->set_encode = (set_encode_fn)kernel_dynlib_resolve(
      getpid(), api->module, "-NotZDC2pQc");
  api->allocate_video2 = (allocate_video2_fn)kernel_dynlib_resolve(
      getpid(), api->module, "yN0w4tWivqs");
  api->free_video = (free_video_fn)kernel_dynlib_resolve(
      getpid(), api->module, "96UQhNnBlxc");
  api->get_image_memory = (get_image_memory_fn)kernel_dynlib_resolve(
      getpid(), api->module, "j7ClJpPRVQI");
  if (api->get_image_memory && api->module_base)
    status_image_function_offset =
        (uintptr_t)api->get_image_memory - (uintptr_t)api->module_base;
  return !api->process_base || !api->initialize || !api->open_video ||
                 !api->start || !api->stop || !api->close ||
                 !api->read_video || !api->get_non_vcl || !api->set_encode
             ? -1
             : 0;
}

static void release_capture_memory(const capture_api *api) {
  if (!capture_memory_owned) return;
  if (api->free_video) api->free_video(capture_memory_a, capture_memory_b);
  memset(capture_memory_a, 0, sizeof(capture_memory_a));
  memset(capture_memory_b, 0, sizeof(capture_memory_b));
  capture_memory_owned = 0;
}

static void close_capture(const capture_api *api, void **owned_handle,
                          int was_started) {
  void *handle = *owned_handle;
  *owned_handle = NULL;
  if (!handle) return;
#if STREAM8_SOCIAL_PARAMETERS
  (void)was_started;
#else
  if (was_started) api->stop(handle);
#endif
  api->close(handle);
#if STREAM8_REMOTEPLAY_NATIVE
  if (remoteplay_object &&
      *(void **)(remoteplay_object + 0x38) == handle)
    *(void **)(remoteplay_object + 0x38) = NULL;
#endif
  release_capture_memory(api);
}

static void build_video_parameters(const capture_api *api, void *parameters,
                                   uint32_t resolution, uint32_t bitrate,
                                   uint32_t vbv) {
  uint32_t mode = 0;
  if (api->get_mode) api->get_mode(&mode);
  memset(parameters, 0, 0x2e8);
#if STREAM8_AUTONOMOUS_CONFIG
  /* Process-independent form reconstructed from the public Avcap2 ABI and
   * the bounded GameLive/RemotePlay dumps. This deliberately avoids every
   * private GameLive code offset so the diagnostic payload can run inside
   * SceRemotePlay without jumping into unrelated code. */
  unsigned char *p = parameters;
  *(uint64_t *)(p + 0x00) = 0x2e8;
  *(uint64_t *)(p + 0x08) = 0x28;
  *(uint32_t *)(p + 0x10) = 0;
  *(uint32_t *)(p + 0x14) = STREAM8_AVCAP_PROFILE;
  *(uint32_t *)(p + 0x18) = resolution + 1;
  *(uint32_t *)(p + 0x1c) = STREAM8_FPS == 60 ? 1 : 0;
  *(uint32_t *)(p + 0x28) = 0xc8;
  *(uint64_t *)(p + 0x30) = 0x58;
  *(uint64_t *)(p + 0x38) = 0x18;
  *(uint64_t *)(p + 0x50) = 0x10;
  *(uint64_t *)(p + 0x88) = 0x30;
  *(uint32_t *)(p + 0x90) = 0xd2;
  *(uint32_t *)(p + 0x94) = 60;
  *(uint64_t *)(p + 0xb8) = 0xe8;
  *(uint64_t *)(p + 0xc0) = 0x20;
  *(uint32_t *)(p + 0xc8) = bitrate;
  *(uint32_t *)(p + 0xcc) = vbv;
  *(uint64_t *)(p + 0xe0) = 0x20;
  *(uint64_t *)(p + 0x100) = 0x20;
  *(uint64_t *)(p + 0x120) = 0x18;
  *(uint64_t *)(p + 0x1f0) = 0xf8;
  *(uint64_t *)(p + 0x1f8) = 0x28;
  *(uint64_t *)(p + 0x220) = 0x88;
  *(uint32_t *)(p + 0x2a8) = mode;
  *(uint32_t *)(p + 0x2ac) = 1;
  *(uint64_t *)(p + 0x2b0) = 0x10;
  if (mode & 3) p[0x2bc] = 1;
#else
  ((cfg_init_fn)(api->process_base + 0x4d260))(NULL, parameters);
  ((cfg_kind_fn)(api->process_base + 0x4d310))(NULL, parameters, 0, mode);
  ((cfg_mode_fn)(api->process_base + 0x4d460))(NULL, parameters, 0, 0, mode);
  ((cfg_pair_fn)(api->process_base + 0x4d3d0))(NULL, parameters, resolution,
                                               STREAM8_FPS == 60 ? 1 : 0);
  ((cfg_one_fn)(api->process_base + 0x4d400))(NULL, parameters,
                                              STREAM8_FPS);
  ((cfg_pair_fn)(api->process_base + 0x4d420))(NULL, parameters, bitrate, vbv);
#endif
}

#if STREAM8_SOCIAL_PARAMETERS
static void build_social_parameters(const capture_api *api, void *parameters) {
  uint32_t mode = 0;
  if (api->get_mode) api->get_mode(&mode);
  unsigned char *p = parameters;
  memset(p, 0, 0x2e8);
  *(uint64_t *)(p + 0x00) = 0x2e8;
  *(uint64_t *)(p + 0x08) = 0x28;
  *(uint32_t *)(p + 0x10) = 2;
  *(uint32_t *)(p + 0x14) = 3;
  *(uint32_t *)(p + 0x18) = 3;
  *(uint32_t *)(p + 0x1c) = 0;
  *(uint32_t *)(p + 0x28) = 0xc8;
  *(uint64_t *)(p + 0x30) = 0x58;
  *(uint64_t *)(p + 0x38) = 0x18;
  *(uint64_t *)(p + 0x50) = 0x10;
  *(uint64_t *)(p + 0x88) = 0x30;
  *(uint32_t *)(p + 0x90) = 0xd2;
  *(uint32_t *)(p + 0x94) = 0x384;
  *(uint64_t *)(p + 0xb8) = 0xe8;
  *(uint64_t *)(p + 0xc0) = 0x20;
  *(uint32_t *)(p + 0xc8) = 10000000;
  *(uint64_t *)(p + 0xe0) = 0x20;
  *(uint64_t *)(p + 0x100) = 0x20;
  *(uint64_t *)(p + 0x120) = 0x18;
  *(uint64_t *)(p + 0x1a0) = 0x50;
  *(uint64_t *)(p + 0x1b0) = 0x20;
  *(uint64_t *)(p + 0x1d0) = 0x20;
  *(uint64_t *)(p + 0x1f0) = 0x10;
  *(uint32_t *)(p + 0x1f8) = mode;
}
#endif

#if STREAM8_REMOTEPLAY_NATIVE
static int prepare_remoteplay_context(const capture_api *api) {
  remoteplay_ctor_fn construct =
      (remoteplay_ctor_fn)(api->process_base + 0x6f1c0);
  remoteplay_prepare_fn prepare =
      (remoteplay_prepare_fn)(api->process_base + 0x6f450);
  remoteplay_object = malloc(0x200400);
  if (!remoteplay_object) return -1;
  memset(remoteplay_object, 0, 0x200400);
  construct(remoteplay_object);
  *(uint32_t *)(remoteplay_object + 0x2003e8) = STREAM8_AVCAP_PROFILE;

  unsigned char config[0x24] = {0};
  *(uint32_t *)(config + 0x00) = 2;
  *(uint32_t *)(config + 0x04) = 1280;
  *(uint32_t *)(config + 0x08) = 720;
  *(uint32_t *)(config + 0x0c) = 0;
  *(uint32_t *)(config + 0x10) = STREAM8_FPS;
  *(uint32_t *)(config + 0x18) = STREAM8_FPS;
  *(uint32_t *)(config + 0x1c) = STREAM8_RUNTIME_BITRATE_BPS / 8;
  config[0x20] = 0;
  uint32_t negotiated = 0;
  int native_error = 0;
  int result = prepare(remoteplay_object, config, &negotiated, &native_error);
  status_init_result = remoteplay_object[0x200334] ? 0 : -1;
  status_remoteplay_build_result = result & 0xff;
  if (!(result & 0xff)) status_open_result = native_error;
  memcpy((void *)status_memory_a, remoteplay_object + 0x1f0,
         sizeof(capture_memory_a));
  memcpy((void *)status_memory_b, remoteplay_object + 0x210,
         sizeof(capture_memory_b));
  return result ? 0 : -1;
}

static void release_remoteplay_context(const capture_api *api) {
  if (!remoteplay_object) return;
  remoteplay_dtor_fn destroy =
      (remoteplay_dtor_fn)(api->process_base + 0x6f300);
  destroy(remoteplay_object);
  free(remoteplay_object);
  remoteplay_object = NULL;
}
#endif

static int __attribute__((unused)) start_proxy_context(const capture_api *api) {
  proxy_ctor_fn construct =
      (proxy_ctor_fn)(api->process_base + 0x4c4b0);
  proxy_init_fn initialize_proxy =
      (proxy_init_fn)(api->process_base + 0x4c590);
  proxy_open_fn open_proxy =
      (proxy_open_fn)(api->process_base + 0x4c930);
  if (!proxy_constructed) {
    memset(proxy_storage, 0, sizeof(proxy_storage));
    construct(proxy_storage);
    proxy_constructed = 1;
  }
  int result = initialize_proxy(proxy_storage);
  if (result < 0 && (uint32_t)result != AVCAP_ALREADY_INITIALIZED) return -1;
  /* Only own the shutdown when this call really initialized the proxy. */
  proxy_initialized = result >= 0;

#if STREAM8_GAME_LIVE_RATE_BUDGET
  /* GameLive's FW 13.60 rate state uses a total budget at +0x198, video at
   * +0x19c and audio at +0x1a0.  Its stock values are exactly
   * 9,000,000 = 8,936,000 + 64,000 bps.  Only touch the state when all three
   * invariants match, then use GameLive's locking setters rather than raw
   * writes. */
  unsigned char *rate_state = *(unsigned char **)(proxy_storage + 0x68);
  if (rate_state && (uintptr_t)rate_state > 0x10000U) {
    status_budget_before = *(uint32_t *)(rate_state + 0x198);
    status_budget_video_before = *(uint32_t *)(rate_state + 0x19c);
    status_budget_audio_before = *(uint32_t *)(rate_state + 0x1a0);
    if (status_budget_before == 9000000U &&
        status_budget_video_before == 8936000U &&
        status_budget_audio_before == 64000U) {
      typedef int (*rate_setter_fn)(void *, uint32_t);
      ((rate_setter_fn)(api->process_base + 0x4a8c0))(
          rate_state, STREAM8_RUNTIME_BITRATE_BPS + 64000U);
      ((rate_setter_fn)(api->process_base + 0x4a820))(rate_state, 64000U);
      ((rate_setter_fn)(api->process_base + 0x4a7d0))(
          rate_state, STREAM8_RUNTIME_BITRATE_BPS);
      status_budget_after = *(uint32_t *)(rate_state + 0x198);
      status_budget_updated =
          status_budget_after == STREAM8_RUNTIME_BITRATE_BPS + 64000U &&
          *(uint32_t *)(rate_state + 0x19c) == STREAM8_RUNTIME_BITRATE_BPS &&
          *(uint32_t *)(rate_state + 0x1a0) == 64000U;
    }
  }
#endif

  unsigned char parameters[0x2e8];
#if STREAM8_GAME_LIVE_RATE_BUDGET
  build_video_parameters(api, parameters, STREAM8_RESOLUTION_INDEX,
                         STREAM8_OPEN_BITRATE_BPS, STREAM8_OPEN_VBV_BPS);
#else
  build_video_parameters(api, parameters, 0, 3500, 4200);
#endif
  void *ignored_handle = NULL;
  open_proxy(proxy_storage, &ignored_handle, parameters);
  return 0;
}

#if STREAM8_RECYCLE_PROXY
static int recycle_proxy_context(const capture_api *api) {
  proxy_init_fn shutdown_proxy =
      (proxy_init_fn)(api->process_base + 0x4c5f0);
  if (proxy_initialized) {
    int result = shutdown_proxy(proxy_storage);
    proxy_initialized = 0;
    if (result < 0) return -1;
  }
  usleep(STREAM8_REOPEN_DELAY_US);
  return start_proxy_context(api);
}
#endif

static void *open_capture(const capture_api *api) {
#if STREAM8_REMOTEPLAY_NATIVE
  if (!remoteplay_object) return NULL;
#elif STREAM8_SOCIAL_NATIVE
  if (!social_object) return NULL;
  unsigned char parameters[0x2e8];
  memcpy(parameters, social_parameters, sizeof(parameters));
#else
  int init_result = api->initialize();
  status_init_result = init_result;
  if (init_result < 0 && (uint32_t)init_result != AVCAP_ALREADY_INITIALIZED)
    return NULL;

  unsigned char parameters[0x2e8];
#if STREAM8_SOCIAL_PARAMETERS
  build_social_parameters(api, parameters);
#else
  build_video_parameters(api, parameters, STREAM8_RESOLUTION_INDEX,
                         STREAM8_OPEN_BITRATE_BPS, STREAM8_OPEN_VBV_BPS);
  /* GameLive normally uses profile 1. SceRemotePlay uses profile 2 by
   * default (and profile 6 in another operating mode) while keeping the
   * same top-level video kind at parameters+0x10. Keep this compile-time so
   * experimental builds cannot silently change the stable payload. */
  *(uint32_t *)(parameters + 0x14) = STREAM8_AVCAP_PROFILE;
#endif

#if STREAM8_AVCAP_PROFILE != 1
  if (!api->allocate_video2 || !api->free_video) return NULL;
  *(uint64_t *)(parameters + 0x1a0) = 0x50;
  *(uint32_t *)(parameters + 0x1a8) = 1;
  unsigned char memory_config[0x90];
  memset(memory_config, 0, sizeof(memory_config));
  *(uint64_t *)memory_config = sizeof(memory_config);
  memcpy(memory_config + 0x08, parameters + 0x08, 0x28);
  memcpy(memory_config + 0x30, parameters + 0x30, 0x58);
  memset(capture_memory_a, 0, sizeof(capture_memory_a));
  memset(capture_memory_b, 0, sizeof(capture_memory_b));
  int allocate_result = api->allocate_video2(
      memory_config, capture_memory_a, capture_memory_b);
  status_allocate_result = allocate_result;
  memcpy((void *)status_memory_a, capture_memory_a, sizeof(capture_memory_a));
  memcpy((void *)status_memory_b, capture_memory_b, sizeof(capture_memory_b));
  if (allocate_result < 0) return NULL;
  capture_memory_owned = 1;
  memcpy(parameters + 0x1b0, capture_memory_a, sizeof(capture_memory_a));
  memcpy(parameters + 0x1d0, capture_memory_b, sizeof(capture_memory_b));
#endif
#endif

  void *handle = NULL;
  ++status_open_attempts;
#if STREAM8_REMOTEPLAY_NATIVE
  remoteplay_open_fn native_open =
      (remoteplay_open_fn)(api->process_base + 0x704c0);
  int retry_budget = 1000;
  int native_error = 0;
  int native_result =
      native_open(remoteplay_object, 0, 0, 0, &retry_budget, &native_error);
  handle = *(void **)(remoteplay_object + 0x38);
  int open_result = native_error;
  if (!(native_result & 0xff) || !handle) {
    status_open_result = open_result;
    return NULL;
  }
#else
  int open_result = api->open_video(&handle, parameters,
#if STREAM8_SOCIAL_NATIVE
                                    1
#elif STREAM8_SOCIAL_PARAMETERS
                                    1
#else
                                    0
#endif
  );
#endif
  status_open_result = open_result;
  if (open_result < 0 || !handle) {
    release_capture_memory(api);
    return NULL;
  }
#if STREAM8_AVCAP_PROFILE != 1
  if (!api->get_image_memory) {
    close_capture(api, &handle, 0);
    return NULL;
  }
  void *image_memory = NULL;
  size_t image_size = 0;
  int image_result =
      api->get_image_memory(handle, &image_memory, &image_size);
  status_image_result = image_result;
  status_image_size = image_size;
  if (image_result < 0 || !image_memory || !image_size ||
      image_size > 64U * 1024U * 1024U) {
    close_capture(api, &handle, 0);
    return NULL;
  }
  memset(image_memory, 0x80, image_size);
#endif
#if STREAM8_SOCIAL_PARAMETERS
  /* SocialScreenMgr does not call the public Avcap2 Start export after its
   * successful OpenVideo. Test its auto-start/read path without forcing the
   * GameLive state transition, which returns 0x81950001 for source kind 2. */
  status_start_result = 0;
#else
  int start_result = api->start(handle);
  status_start_result = start_result;
  if (start_result < 0) {
    close_capture(api, &handle, 0);
    return NULL;
  }
#endif

  /* Rate control is applied only after ReadVideo produces a real frame.  On
   * firmware 13.60 both rate-control IPCs return INVALID_STATE when called
   * immediately after Start, before the hardware encoder is actually live. */
  status_encode_buffer_result = -1;
  status_encode_result = -1;
  status_encode_attempts = 0;
  return handle;
}

static int apply_runtime_encode(const capture_api *api, void *handle) {
  unsigned char encode_config[8] = {1, 0, 0, 0, 0, 0, 0, 0};
  ++status_encode_attempts;
#if STREAM8_NATIVE_BUFFER_UPDATE
  /* Firmware 13.60 has a second Avcap2 rate-control entry point at +0x1f80.
   * It sends IPC 0xa70018 without a capture handle, immediately before the
   * public per-handle SetVideoEncodeConfig path (IPC 0xa70019).  The paired
   * update is kept behind a build flag until it is validated on hardware. */
  set_global_encode_fn set_encode_buffer =
      (set_global_encode_fn)(api->module_base + 0x1f80);
  *(uint32_t *)(encode_config + 4) = STREAM8_RUNTIME_VBV_BPS;
  status_encode_buffer_result = set_encode_buffer(encode_config);
#else
  status_encode_buffer_result = 0;
#endif
  *(uint32_t *)(encode_config + 4) = STREAM8_RUNTIME_BITRATE_BPS;
  status_encode_result = api->set_encode(encode_config, handle);
  return status_encode_buffer_result == 0 && status_encode_result == 0;
}

static int send_all(int socket_fd, const void *data, size_t size) {
  const unsigned char *cursor = data;
  while (size) {
    ssize_t written = send(socket_fd, cursor, size, 0);
    if (written <= 0) return -1;
    cursor += written;
    size -= (size_t)written;
  }
  return 0;
}

static int send_packet(int socket_fd, const void *data, uint32_t size) {
  uint32_t network_size = htonl(size);
  return send_all(socket_fd, &network_size, sizeof(network_size)) ||
         send_all(socket_fd, data, size);
}

enum stream8_packet_type {
  STREAM8_PACKET_VIDEO_CONFIG = 0,
  STREAM8_PACKET_VIDEO = 1,
  STREAM8_PACKET_AUDIO = 2,
};

static int send_media_packet(int socket_fd, int audiovisual, uint8_t type,
                             const void *data, uint32_t size) {
  if (!audiovisual) return send_packet(socket_fd, data, size);
  uint32_t network_size = htonl(size);
  return send_all(socket_fd, &type, sizeof(type)) ||
         send_all(socket_fd, &network_size, sizeof(network_size)) ||
         send_all(socket_fd, data, size);
}

static int client_disconnected(int socket_fd) {
  unsigned char byte;
  ssize_t result = recv(socket_fd, &byte, sizeof(byte), MSG_PEEK | MSG_DONTWAIT);
  if (result == 0) return 1;
  if (result > 0) return 0;
  return errno != EAGAIN && errno != EWOULDBLOCK;
}

static int contains_nal_type(const unsigned char *data, size_t size,
                             unsigned type) {
  for (size_t i = 0; i + 4 < size; ++i) {
    if (data[i] || data[i + 1]) continue;
    if (data[i + 2] == 1 && (data[i + 3] & 31U) == type) return 1;
    if (i + 5 < size && data[i + 2] == 0 && data[i + 3] == 1 &&
        (data[i + 4] & 31U) == type)
      return 1;
  }
  return 0;
}

static int send_codec_config(int socket_fd, const capture_api *api,
                             void *handle, int audiovisual) {
  unsigned char non_vcl[0x1a8];
  memset(non_vcl, 0, sizeof(non_vcl));
  if (api->get_non_vcl(handle, non_vcl) < 0) return -1;

  uint32_t sps_size = *(uint32_t *)(non_vcl + 0x10);
  uint32_t pps_size = *(uint32_t *)(non_vcl + 0x90);
  static const unsigned char fallback_pps[4] = {0x68, 0xee, 0x38, 0x80};
  const unsigned char *pps = non_vcl + 0x98;
  if (!pps_size) {
    pps = fallback_pps;
    pps_size = sizeof(fallback_pps);
  }
  if (!sps_size || sps_size >= 128 || pps_size >= 128) return -1;

  unsigned char config[264];
  size_t used = 0;
  memcpy(config + used, "\0\0\0\1", 4);
  used += 4;
  memcpy(config + used, non_vcl + 0x18, sps_size);
  used += sps_size;
  memcpy(config + used, "\0\0\0\1", 4);
  used += 4;
  memcpy(config + used, pps, pps_size);
  used += pps_size;
  return send_media_packet(socket_fd, audiovisual,
                           STREAM8_PACKET_VIDEO_CONFIG, config,
                           (uint32_t)used);
}

static void *open_audio_capture(const capture_api *api) {
  if (!api->open_audio || !api->read_audio) return NULL;
  unsigned char parameters[0x38];
  memset(parameters, 0, sizeof(parameters));
  *(uint64_t *)(parameters + 0x00) = sizeof(parameters);
  *(uint64_t *)(parameters + 0x08) = 0x18;
  *(uint32_t *)(parameters + 0x10) = 2;
  *(uint64_t *)(parameters + 0x14) = 0x100000001ULL;
  *(uint32_t *)(parameters + 0x1c) = 0xc8;
  *(uint64_t *)(parameters + 0x20) = 0x10;
  *(uint32_t *)(parameters + 0x28) = 64000;
  void *handle = NULL;
  int result = api->open_audio(&handle, parameters, 1);
  return result >= 0 ? handle : NULL;
}

static void close_audio_capture(const capture_api *api, void **owned_handle) {
  void *handle = *owned_handle;
  *owned_handle = NULL;
  if (handle) api->close(handle);
}

/* Sleeps in short slices so a viewer disconnect is noticed quickly. */
static int wait_ms(int client, unsigned milliseconds) {
  for (unsigned waited = 0; waited < milliseconds; waited += 100) {
    if (client_disconnected(client)) return -1;
    usleep(100000);
  }
  return 0;
}

static int ensure_capture(int client, const capture_api *api, void **handle,
                          unsigned *backoff_ms) {
  while (!*handle) {
    if (client_disconnected(client)) return -1;
    *handle = open_capture(api);
    if (*handle) break;
    if (wait_ms(client, *backoff_ms)) return -1;
    if (*backoff_ms < STREAM8_BACKOFF_MAX_MS) *backoff_ms *= 2;
    if (*backoff_ms > STREAM8_BACKOFF_MAX_MS)
      *backoff_ms = STREAM8_BACKOFF_MAX_MS;
  }
  return 0;
}

static int stream_video(int client, const capture_api *api, void **handle,
                        void **audio_handle, int audiovisual) {
  const char *headers =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/octet-stream\r\n"
      "Cache-Control: no-store\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Connection: close\r\n\r\n";
  if (send_all(client, headers, strlen(headers))) return -1;

  /* SPS/PPS is sent together with the first real video frame of each
   * capture session, never on open: a replacement title that yields SPS/PPS
   * but no frames would otherwise make the viewer reset its decoder in a
   * loop while showing nothing. */
  int config_sent = 0;
  int waiting_for_idr = 1;
  int capture_has_frame = 0;
  int rate_applied = 0;
  unsigned rate_retry_frames = 0;
  unsigned bad_reads = 0;
  unsigned backoff_ms = STREAM8_BACKOFF_MIN_MS;
  uint64_t last_frame = monotonic_ms();
  uint64_t opened_at = last_frame;

  for (;;) {
    int reopening = !*handle;
    if (ensure_capture(client, api, handle, &backoff_ms)) return -1;
    if (reopening) {
      opened_at = last_frame = monotonic_ms();
      capture_has_frame = 0;
      rate_applied = 0;
      rate_retry_frames = 0;
      config_sent = 0;
      waiting_for_idr = 1;
    }
    if (audiovisual && !*audio_handle)
      *audio_handle = open_audio_capture(api);
    unsigned char info[0x68];
    memset(info, 0, sizeof(info));
    int result = api->read_video(*handle, video_data, sizeof(video_data), info,
                                 STREAM8_READ_NOWAIT);
    status_read_result = result;
    if (result > 0) {
      ++status_frames;
      last_frame = monotonic_ms();
      capture_has_frame = 1;
      bad_reads = 0;
      backoff_ms = STREAM8_BACKOFF_MIN_MS;
      if (!rate_applied &&
          (status_encode_attempts == 0 || ++rate_retry_frames >= 60)) {
        rate_applied = apply_runtime_encode(api, *handle);
        rate_retry_frames = 0;
      }
      if (client_disconnected(client)) return -1;
      if (!config_sent) {
        if (send_codec_config(client, api, *handle, audiovisual)) {
          if (client_disconnected(client)) return -1;
          close_capture(api, handle, 1);
          continue;
        }
        config_sent = 1;
      }
      if (STREAM8_REQUIRE_IDR && waiting_for_idr &&
          !contains_nal_type(video_data, (size_t)result, 5))
        continue;
      waiting_for_idr = 0;
      if (send_media_packet(client, audiovisual, STREAM8_PACKET_VIDEO,
                            video_data, (uint32_t)result))
        return -1;
    }

    /* ReadAudio is auto-started as part of GameLive's capture group on 13.60;
     * calling the generic Start export returns INVALID_STATE. Drain a bounded
     * number per video iteration so audio never starves video delivery. */
    if (audiovisual && *audio_handle) {
      for (unsigned count = 0; count < 8; ++count) {
        unsigned char audio_info[0x40];
        memset(audio_info, 0, sizeof(audio_info));
        int audio_result = api->read_audio(*audio_handle, audio_data,
                                           sizeof(audio_data), audio_info,
                                           STREAM8_READ_NOWAIT);
        if (audio_result <= 0) break;
        if (send_media_packet(client, 1, STREAM8_PACKET_AUDIO, audio_data,
                              (uint32_t)audio_result))
          return -1;
      }
    }

    if (result > 0) continue;

    uint32_t error = (uint32_t)result;
    if (capture_has_frame && result < 0 && error != AVCAP_ERR_EMPTY &&
        error != AVCAP_ERR_OVERRUN)
      ++bad_reads;
    else
      bad_reads = 0;

    uint64_t now = monotonic_ms();
    int established_stall = capture_has_frame && now && last_frame &&
                            now - last_frame >= STREAM8_RESTART_AFTER_MS;
    int initial_stall = !capture_has_frame && now && opened_at &&
                        now - opened_at >= STREAM8_INITIAL_GRACE_MS;
    if ((capture_has_frame && error == AVCAP_ERR_SESSION_END) ||
        bad_reads >= STREAM8_BAD_READ_LIMIT || established_stall ||
        initial_stall) {
      close_capture(api, handle, 1);
      close_audio_capture(api, audio_handle);
#if STREAM8_RECYCLE_PROXY
      recycle_proxy_context(api);
#endif
      /* A handle that spans a title transition can leave GameLive bound to
       * the old title. End this HTTP stream completely instead of repeatedly
       * reopening inside the same client session. The browser reconnects and
       * obtains a fresh handle after the replacement title is ready. */
      return -1;
    }
    if (client_disconnected(client)) return -1;
    usleep(2000);
  }
}

static const char page[] =
    "<!doctype html><meta charset=utf-8><title>Stream8 PS5</title>"
    "<style>body{background:#090b10;color:#fff;font:18px system-ui;"
    "display:grid;place-items:center;height:100vh;margin:0}</style>"
    "<p>Stream8 capture server is running. Open the PC bridge at "
    "<b>http://localhost:9090</b>.</p>";

static void send_status(int client) {
  char body[768];
  int body_length = snprintf(
      body, sizeof(body),
      "{\"profile\":%u,\"open_attempts\":%u,\"frames\":%u,"
      "\"initialize\":\"0x%08x\",\"rp_build\":\"0x%08x\","
      "\"allocate\":\"0x%08x\","
      "\"open\":\"0x%08x\",\"image\":\"0x%08x\","
      "\"image_fn\":\"0x%lx\",\"image_size\":%zu,"
      "\"memory_a\":[\"0x%lx\",\"0x%lx\",\"0x%lx\",\"0x%lx\"],"
      "\"memory_b\":[\"0x%lx\",\"0x%lx\",\"0x%lx\",\"0x%lx\"],"
      "\"start\":\"0x%08x\",\"encode_attempts\":%u,"
      "\"encode_buffer\":\"0x%08x\","
      "\"encode\":\"0x%08x\","
      "\"budget_before\":%u,\"budget_video_before\":%u,"
      "\"budget_audio_before\":%u,\"budget_after\":%u,"
      "\"budget_updated\":%d,"
      "\"read\":\"0x%08x\"}\n",
      STREAM8_AVCAP_PROFILE, status_open_attempts, status_frames,
      (uint32_t)status_init_result,
      (uint32_t)status_remoteplay_build_result,
      (uint32_t)status_allocate_result,
      (uint32_t)status_open_result, (uint32_t)status_image_result,
      (unsigned long)status_image_function_offset, status_image_size,
      (unsigned long)status_memory_a[0], (unsigned long)status_memory_a[1],
      (unsigned long)status_memory_a[2], (unsigned long)status_memory_a[3],
      (unsigned long)status_memory_b[0], (unsigned long)status_memory_b[1],
      (unsigned long)status_memory_b[2], (unsigned long)status_memory_b[3],
      (uint32_t)status_start_result, status_encode_attempts,
      (uint32_t)status_encode_buffer_result,
      (uint32_t)status_encode_result,
      status_budget_before, status_budget_video_before,
      status_budget_audio_before, status_budget_after,
      status_budget_updated,
      (uint32_t)status_read_result);
  char headers[256];
  int header_length = snprintf(
      headers, sizeof(headers),
      "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
      "Content-Length: %d\r\nCache-Control: no-store\r\n"
      "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
      body_length);
  send_all(client, headers, (size_t)header_length);
  send_all(client, body, (size_t)body_length);
}

static void send_shutdown_ack(int client) {
  const char *response =
      "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
      "Content-Length: 21\r\nConnection: close\r\n\r\n"
      "Stream8 shutting down";
  send_all(client, response, strlen(response));
}

int main(void) {
  signal(SIGPIPE, SIG_IGN);
  capture_api api;
  if (resolve_capture_api(&api)) return 1;
#if STREAM8_REMOTEPLAY_NATIVE
  if (prepare_remoteplay_context(&api)) return 4;
#endif
#if STREAM8_SOCIAL_NATIVE
  if (prepare_social_context(&api)) return 5;
#endif
#if STREAM8_AVCAP_PROFILE == 1 && !STREAM8_SOCIAL_NATIVE
  if (start_proxy_context(&api)) return 3;
#endif

  int server = socket(AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(STREAM8_PS5_PORT);
  if (bind(server, (void *)&address, sizeof(address)) < 0 ||
      listen(server, 4) < 0)
    return 2;

  void *capture_handle = NULL;
  void *audio_handle = NULL;
  int shutdown_requested = 0;
  for (;;) {
    int client = accept(server, NULL, NULL);
    if (client < 0) continue;
    /* Avoid a delayed-ACK/Nagle pause when the four-byte frame length is
     * followed by a large periodic IDR frame. */
    int tcp_no_delay = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &tcp_no_delay,
               sizeof(tcp_no_delay));
    int send_buffer = 1024 * 1024;
    setsockopt(client, SOL_SOCKET, SO_SNDBUF, &send_buffer,
               sizeof(send_buffer));
    struct timeval timeout = {2, 0};
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    char request[1024] = {0};
    recv(client, request, sizeof(request) - 1, 0);
    if (!strncmp(request, "GET /stream-av ", 15)) {
      stream_video(client, &api, &capture_handle, &audio_handle, 1);
      close_audio_capture(&api, &audio_handle);
      close_capture(&api, &capture_handle, capture_handle != NULL);
    } else if (!strncmp(request, "GET /stream ", 12)) {
      stream_video(client, &api, &capture_handle, &audio_handle, 0);
      /* Never retain a capture handle after its viewer has disconnected. */
      close_capture(&api, &capture_handle, capture_handle != NULL);
    } else if (!strncmp(request, "GET /status", 11)) {
      send_status(client);
    } else if (!strncmp(request, "GET /shutdown", 13)) {
      send_shutdown_ack(client);
      shutdown_requested = 1;
    } else {
      char headers[192];
      int length = snprintf(
          headers, sizeof(headers),
          "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
          "Content-Length: %zu\r\nCache-Control: no-store\r\n"
          "Connection: close\r\n\r\n",
          strlen(page));
      send_all(client, headers, (size_t)length);
      send_all(client, page, strlen(page));
    }
    close(client);
    if (shutdown_requested) break;
  }
  close_capture(&api, &capture_handle, capture_handle != NULL);
  close_audio_capture(&api, &audio_handle);
  close(server);
#if STREAM8_REMOTEPLAY_NATIVE
  release_remoteplay_context(&api);
#endif
#if STREAM8_SOCIAL_NATIVE
  release_social_context(&api);
#endif
  return 0;
}
