/*
 * IPC interface for hyprlax
 * Handles runtime layer management via Unix sockets
 */

#ifndef HYPRLAX_IPC_H
#define HYPRLAX_IPC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define IPC_SOCKET_PATH_PREFIX "/tmp/hyprlax-"
#define IPC_MAX_MESSAGE_SIZE 4096
#define IPC_MAX_LAYERS 32
/* Validation limits for IPC tokens */
#define IPC_MAX_PROP_LEN   64
#define IPC_MAX_VALUE_LEN  512

typedef enum {
    IPC_CMD_ADD_LAYER,
    IPC_CMD_REMOVE_LAYER,
    IPC_CMD_MODIFY_LAYER,
    IPC_CMD_LAYER_FRONT,
    IPC_CMD_LAYER_BACK,
    IPC_CMD_LAYER_UP,
    IPC_CMD_LAYER_DOWN,
    IPC_CMD_LIST_LAYERS,
    IPC_CMD_CLEAR_LAYERS,
    IPC_CMD_RELOAD_CONFIG,
    IPC_CMD_GET_STATUS,
    IPC_CMD_SET_PROPERTY,
    IPC_CMD_GET_PROPERTY,
    IPC_CMD_DIAG,
    IPC_CMD_COMPUTED,
    IPC_CMD_UNKNOWN
} ipc_command_t;

typedef struct {
    char* image_path;
    float scale;
    float opacity;
    float x_offset;
    float y_offset;
    int z_index;
    bool visible;
    /* Optional tint for fallback mode */
    float tint_r;
    float tint_g;
    float tint_b;
    float tint_strength;
    uint32_t id;
} layer_t;

typedef struct {
    int socket_fd;
    char socket_path[256];
    bool active;
    layer_t* layers[IPC_MAX_LAYERS];
    int layer_count;
    uint32_t next_layer_id;
    void* app_context;  /* Pointer to hyprlax_context_t for runtime settings */
} ipc_context_t;

// IPC lifecycle functions
ipc_context_t* ipc_init(void);
void ipc_cleanup(ipc_context_t* ctx);
bool ipc_process_commands(ipc_context_t* ctx);

// Layer management functions
uint32_t ipc_add_layer(ipc_context_t* ctx, const char* image_path, float scale, float opacity, float x_offset, float y_offset, int z_index);
bool ipc_remove_layer(ipc_context_t* ctx, uint32_t layer_id);
bool ipc_modify_layer(ipc_context_t* ctx, uint32_t layer_id, const char* property, const char* value);
char* ipc_list_layers(ipc_context_t* ctx);
void ipc_clear_layers(ipc_context_t* ctx);

// Helper functions
layer_t* ipc_find_layer(ipc_context_t* ctx, uint32_t layer_id);
void ipc_sort_layers(ipc_context_t* ctx);

// Request handling function for tests
int ipc_handle_request(ipc_context_t* ctx, const char* request, char* response, size_t response_size);

#endif // HYPRLAX_IPC_H
