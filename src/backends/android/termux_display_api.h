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
// Note: lorie_shared_server_state and lorieEvent are defined in termux-render headers

// All types are defined in termux-render library headers
// This file only provides C++ convenience wrappers

// All API functions are declared in termux-render library headers
// No need to redeclare them here

// Mutex helpers are defined in termux-render library headers

#ifdef __cplusplus
}
#endif // __cplusplus
