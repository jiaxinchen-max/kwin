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

// All types are defined in termux-render library headers
// This file only provides C++ convenience wrappers

// All API functions are declared in termux-render library headers
// No need to redeclare them here

// Mutex helpers are defined in termux-render library headers

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
