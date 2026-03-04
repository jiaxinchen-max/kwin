/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

/**
 * @file termux_display_api.h
 * @brief Wrapper header for termux-display-client API
 * 
 * This header provides type-safe C++ wrappers around the C API
 * provided by termux-display-client library.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <pthread.h>

// Forward declarations from termux-display-client
struct AHardwareBuffer;

/**
 * LorieBuffer descriptor
 * Contains information about the shared buffer
 */
typedef struct {
    int32_t width;
    int32_t height;
    int32_t stride;
    int8_t format;
    int8_t type;
    struct AHardwareBuffer* buffer;
    void* data;
} LorieBuffer_Desc;

/**
 * LorieBuffer handle
 * Opaque type representing a shared buffer
 */
typedef struct LorieBuffer LorieBuffer;

/**
 * Shared server state
 * Used for synchronization between client and server
 */
typedef struct {
    pthread_mutex_t lock;
    pid_t lockingPid;
    pthread_cond_t cond;
    volatile uint8_t drawRequested;
    volatile uint8_t surfaceAvailable;
    volatile uint8_t waitForNextFrame;
    // Cursor data follows...
} lorie_shared_server_state;

/**
 * Event types
 */
enum {
    EVENT_SCREEN_CONFIG = 1,
    EVENT_TOUCH = 2,
    EVENT_MOUSE = 3,
    EVENT_KEY = 4,
    EVENT_CLIPBOARD = 5,
    EVENT_ADD_BUFFER = 6,
    EVENT_APPLY_BUFFER = 7,
    EVENT_APPLY_SHARED_SERVER_STATE = 8,
};

/**
 * Touch event data
 */
typedef struct {
    int32_t type;  // 0=down, 1=up, 2=motion
    int32_t id;
    float x;
    float y;
} TouchEvent;

/**
 * Mouse event data
 */
typedef struct {
    float x;
    float y;
    uint8_t down;
    uint8_t relative;
    uint8_t detail;
} MouseEvent;

/**
 * Key event data
 */
typedef struct {
    uint32_t key;
    uint8_t state;  // 0=released, 1=pressed
} KeyEvent;

/**
 * Screen config data
 */
typedef struct {
    int32_t width;
    int32_t height;
    int32_t format;
    int32_t type;
} ScreenConfig;

/**
 * Event union
 */
typedef union {
    uint8_t type;
    struct {
        uint8_t eventType;
        TouchEvent touch;
    } touchEvent;
    struct {
        uint8_t eventType;
        MouseEvent mouse;
    } mouseEvent;
    struct {
        uint8_t eventType;
        KeyEvent key;
    } keyEvent;
    struct {
        uint8_t eventType;
        ScreenConfig screenSize;
    } screenEvent;
} lorieEvent;

// API functions from termux-display-client

/**
 * Set screen configuration
 * Must be called before connectToRender()
 */
void setScreenConfig(int width, int height, int refreshRate);

/**
 * Connect to the display server
 * @return 0 on success, -1 on failure
 */
int connectToRender();

/**
 * Get the LorieBuffer handle
 * Valid after successful connectToRender()
 */
LorieBuffer* get_lorieBuffer();

/**
 * Get the shared server state
 * Valid after successful connectToRender()
 */
lorie_shared_server_state* get_serverState();

/**
 * Get the connection file descriptor
 * Used for reading input events
 */
int get_conn_fd();

/**
 * Get buffer description
 */
const LorieBuffer_Desc* LorieBuffer_description(LorieBuffer* buffer);

// Mutex helpers for cross-process synchronization

/**
 * Lock the server state mutex
 */
static inline void lorie_mutex_lock(pthread_mutex_t* lock, pid_t* lockingPid) {
    pthread_mutex_lock(lock);
    *lockingPid = getpid();
}

/**
 * Unlock the server state mutex
 */
static inline void lorie_mutex_unlock(pthread_mutex_t* lock, pid_t* lockingPid) {
    *lockingPid = 0;
    pthread_mutex_unlock(lock);
}

#ifdef __cplusplus
}

// C++ convenience wrappers
namespace Termux {

/**
 * RAII wrapper for server state lock
 */
class ServerStateLock {
public:
    explicit ServerStateLock(lorie_shared_server_state* state)
        : m_state(state)
    {
        if (m_state) {
            lorie_mutex_lock(&m_state->lock, &m_state->lockingPid);
        }
    }
    
    ~ServerStateLock() {
        if (m_state) {
            lorie_mutex_unlock(&m_state->lock, &m_state->lockingPid);
        }
    }
    
    // Non-copyable
    ServerStateLock(const ServerStateLock&) = delete;
    ServerStateLock& operator=(const ServerStateLock&) = delete;
    
private:
    lorie_shared_server_state* m_state;
};

/**
 * Signal frame ready to display server
 */
inline void signalFrameReady(lorie_shared_server_state* state) {
    if (!state) return;
    
    ServerStateLock lock(state);
    state->drawRequested = 1;
    pthread_cond_signal(&state->cond);
}

} // namespace Termux

#endif // __cplusplus
