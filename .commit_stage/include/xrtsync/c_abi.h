// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Cansheng LIN
//
// Stable C ABI surface for embedding xrt-sync into Unity, Unreal, WebXR
// runtimes, and other host environments that cannot consume C++ headers
// directly.
//
// All identifiers are prefixed with xrtsync_. All functions return an int
// status code matching xrtsync::Status (cast to int); zero indicates
// success. Strings are null-terminated UTF-8.

#ifndef XRTSYNC_C_ABI_H_
#define XRTSYNC_C_ABI_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(XRTSYNC_SHARED)
#  ifdef XRTSYNC_BUILDING
#    define XRTSYNC_API __declspec(dllexport)
#  else
#    define XRTSYNC_API __declspec(dllimport)
#  endif
#else
#  define XRTSYNC_API
#endif

typedef struct xrtsync_session xrtsync_session_t;

typedef struct {
  const char* endpoint;
  uint32_t max_in_flight;
  uint32_t send_rate_hz;
  uint32_t stale_threshold_us;
  int32_t is_host;  // non-zero = host
  const char* preshared_key;  // may be NULL
} xrtsync_config_t;

typedef struct {
  const char channel[16];
  uint32_t origin;
  int64_t timestamp_us;
  uint32_t sequence;
  const void* payload;
  size_t payload_size;
} xrtsync_update_t;

typedef void (*xrtsync_update_cb)(const xrtsync_update_t* update,
                                  void* user_data);
typedef void (*xrtsync_event_cb)(uint32_t participant_id,
                                 int event,
                                 void* user_data);

XRTSYNC_API int xrtsync_session_create(const xrtsync_config_t* config,
                                       xrtsync_session_t** out_session);

XRTSYNC_API void xrtsync_session_destroy(xrtsync_session_t* session);

XRTSYNC_API int xrtsync_session_send(xrtsync_session_t* session,
                                     const char channel[16],
                                     const void* payload,
                                     size_t payload_size);

XRTSYNC_API int xrtsync_session_set_update_handler(xrtsync_session_t* session,
                                                   const char channel[16],
                                                   xrtsync_update_cb cb,
                                                   void* user_data);

XRTSYNC_API int xrtsync_session_set_event_handler(xrtsync_session_t* session,
                                                  xrtsync_event_cb cb,
                                                  void* user_data);

XRTSYNC_API int xrtsync_session_tick(xrtsync_session_t* session,
                                     uint64_t budget_us);

XRTSYNC_API int xrtsync_session_run(xrtsync_session_t* session);

XRTSYNC_API void xrtsync_session_stop(xrtsync_session_t* session);

XRTSYNC_API uint32_t xrtsync_session_local_id(const xrtsync_session_t* session);

XRTSYNC_API const char* xrtsync_status_string(int status);

XRTSYNC_API const char* xrtsync_version_string(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // XRTSYNC_C_ABI_H_
