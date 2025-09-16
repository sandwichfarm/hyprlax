/*
 * compositor.c - Compositor adapter management
 * 
 * Handles creation and management of compositor adapters.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include "../include/compositor.h"
#include "../include/hyprlax_internal.h"

/* Utility function for connecting to Unix socket with retries
 * Used by all compositors to wait for compositor readiness at startup
 */
int compositor_connect_socket_with_retry(const char *socket_path, 
                                         const char *compositor_name,
                                         int max_retries,
                                         int retry_delay_ms) {
    if (!socket_path) return -1;
    
    bool first_attempt = true;
    
    for (int i = 0; i < max_retries; i++) {
        /* Try to connect */
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            if (i < max_retries - 1) {
                usleep(retry_delay_ms * 1000);
                continue;
            }
            return -1;
        }
        
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
        
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            /* Success */
            if (!first_attempt && compositor_name) {
                fprintf(stderr, "Connected to %s after %d retries\n", 
                        compositor_name, i);
            }
            return fd;
        }
        
        /* Connection failed */
        close(fd);
        
        if (first_attempt && compositor_name) {
            fprintf(stderr, "Waiting for %s to be ready...\n", compositor_name);
            first_attempt = false;
        }
        
        if (i < max_retries - 1) {
            usleep(retry_delay_ms * 1000);
        }
    }
    
    return -1;
}

/* Detect compositor type */
compositor_type_t compositor_detect(void) {
    /* Check in order of specificity */
    
    /* Hyprland */
    if (compositor_hyprland_ops.detect && compositor_hyprland_ops.detect()) {
        DEBUG_LOG("Detected Hyprland compositor");
        return COMPOSITOR_HYPRLAND;
    }
    
    /* Wayfire (2D workspace grid) */
    if (compositor_wayfire_ops.detect && compositor_wayfire_ops.detect()) {
        DEBUG_LOG("Detected Wayfire compositor");
        return COMPOSITOR_WAYFIRE;
    }
    
    /* Niri (scrollable workspaces) */
    if (compositor_niri_ops.detect && compositor_niri_ops.detect()) {
        DEBUG_LOG("Detected Niri compositor");
        return COMPOSITOR_NIRI;
    }
    
    /* Sway */
    if (compositor_sway_ops.detect && compositor_sway_ops.detect()) {
        DEBUG_LOG("Detected Sway compositor");
        return COMPOSITOR_SWAY;
    }
    
    /* River */
    if (compositor_river_ops.detect && compositor_river_ops.detect()) {
        DEBUG_LOG("Detected River compositor");
        return COMPOSITOR_RIVER;
    }
    
    /* X11/EWMH */
    if (compositor_x11_ewmh_ops.detect && compositor_x11_ewmh_ops.detect()) {
        DEBUG_LOG("Detected X11/EWMH environment");
        return COMPOSITOR_X11_EWMH;
    }
    
    /* Generic Wayland (fallback) */
    if (compositor_generic_wayland_ops.detect && compositor_generic_wayland_ops.detect()) {
        DEBUG_LOG("Detected generic Wayland compositor");
        return COMPOSITOR_GENERIC_WAYLAND;
    }
    
    fprintf(stderr, "Warning: Could not detect compositor type\n");
    return COMPOSITOR_GENERIC_WAYLAND;
}

/* Create compositor adapter instance */
int compositor_create(compositor_adapter_t **out_adapter, compositor_type_t type) {
    if (!out_adapter) {
        return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    compositor_adapter_t *adapter = calloc(1, sizeof(compositor_adapter_t));
    if (!adapter) {
        return HYPRLAX_ERROR_NO_MEMORY;
    }
    
    /* Auto-detect if requested */
    if (type == COMPOSITOR_AUTO) {
        type = compositor_detect();
    }
    
    /* Select adapter based on type */
    switch (type) {
        case COMPOSITOR_HYPRLAND:
            adapter->ops = &compositor_hyprland_ops;
            adapter->type = COMPOSITOR_HYPRLAND;
            break;
            
        case COMPOSITOR_WAYFIRE:
            adapter->ops = &compositor_wayfire_ops;
            adapter->type = COMPOSITOR_WAYFIRE;
            break;
            
        case COMPOSITOR_NIRI:
            adapter->ops = &compositor_niri_ops;
            adapter->type = COMPOSITOR_NIRI;
            break;
            
        case COMPOSITOR_SWAY:
            adapter->ops = &compositor_sway_ops;
            adapter->type = COMPOSITOR_SWAY;
            break;
            
        case COMPOSITOR_RIVER:
            adapter->ops = &compositor_river_ops;
            adapter->type = COMPOSITOR_RIVER;
            break;
            
        case COMPOSITOR_GENERIC_WAYLAND:
            adapter->ops = &compositor_generic_wayland_ops;
            adapter->type = COMPOSITOR_GENERIC_WAYLAND;
            break;
            
        case COMPOSITOR_X11_EWMH:
            adapter->ops = &compositor_x11_ewmh_ops;
            adapter->type = COMPOSITOR_X11_EWMH;
            break;
            
        default:
            free(adapter);
            return HYPRLAX_ERROR_INVALID_ARGS;
    }
    
    adapter->initialized = false;
    adapter->connected = false;
    *out_adapter = adapter;
    
    DEBUG_LOG("Created compositor adapter for %s", 
              adapter->ops->get_name ? adapter->ops->get_name() : "unknown");
    
    return HYPRLAX_SUCCESS;
}

/* Destroy compositor adapter instance */
void compositor_destroy(compositor_adapter_t *adapter) {
    if (!adapter) return;
    
    if (adapter->connected && adapter->ops && adapter->ops->disconnect_ipc) {
        adapter->ops->disconnect_ipc();
    }
    
    if (adapter->initialized && adapter->ops && adapter->ops->destroy) {
        adapter->ops->destroy();
    }
    
    free(adapter);
}