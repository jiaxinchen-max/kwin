/*
 * SPDX-FileCopyrightText: 2024 Termux LibInput Implementation
 * SPDX-License-Identifier: MIT
 */

#ifndef TERMUX_INPUT_BRIDGE_H
#define TERMUX_INPUT_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

struct libinput;

/**
 * Initialize the termux input bridge
 * 
 * @param libinput The libinput context
 * @return 0 on success, -1 on error
 */
int termux_input_bridge_init(struct libinput *libinput);

/**
 * Dispatch events from termux-display-client fd
 * Called when KWin detects the termux fd is ready
 * 
 * @param termux_fd The termux-display-client event fd
 * @return number of events processed, or -1 on error
 */
int termux_input_bridge_dispatch(int termux_fd);

/**
 * Destroy the termux input bridge
 */
void termux_input_bridge_destroy(void);

/**
 * Check if the bridge is connected to termux-display-client
 * 
 * @return 1 if connected, 0 if not connected
 */
int termux_input_bridge_is_connected(void);


#ifdef __cplusplus
}
#endif

#endif /* TERMUX_INPUT_BRIDGE_H */