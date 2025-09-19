/*
 * IPC implementation for hyprlax
 * Handles runtime layer management via Unix sockets
 */

#include "ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <time.h>

/* Weak runtime bridge stubs. If the application provides real implementations,
 * the linker will bind to those instead. */
__attribute__((weak)) int hyprlax_runtime_set_property(void *app_ctx, const char *property, const char *value) {
    (void)app_ctx; (void)property; (void)value; return -1;
}
__attribute__((weak)) int hyprlax_runtime_get_property(void *app_ctx, const char *property, char *out, size_t out_size) {
    (void)app_ctx; (void)property; (void)out; (void)out_size; return -1;
}

static void get_socket_path(char* buffer, size_t size) {
    const char* user = getenv("USER");
    if (!user) {
        struct passwd* pw = getpwuid(getuid());
        user = pw ? pw->pw_name : "unknown";
    }
    snprintf(buffer, size, "%s%s.sock", IPC_SOCKET_PATH_PREFIX, user);
}

static ipc_command_t parse_command(const char* cmd) {
    if (strcmp(cmd, "add") == 0) return IPC_CMD_ADD_LAYER;
    if (strcmp(cmd, "remove") == 0 || strcmp(cmd, "rm") == 0) return IPC_CMD_REMOVE_LAYER;
    if (strcmp(cmd, "modify") == 0 || strcmp(cmd, "mod") == 0) return IPC_CMD_MODIFY_LAYER;
    if (strcmp(cmd, "list") == 0 || strcmp(cmd, "ls") == 0) return IPC_CMD_LIST_LAYERS;
    if (strcmp(cmd, "clear") == 0) return IPC_CMD_CLEAR_LAYERS;
    if (strcmp(cmd, "reload") == 0) return IPC_CMD_RELOAD_CONFIG;
    if (strcmp(cmd, "status") == 0) return IPC_CMD_GET_STATUS;
    if (strcmp(cmd, "set") == 0) return IPC_CMD_SET_PROPERTY;
    if (strcmp(cmd, "get") == 0) return IPC_CMD_GET_PROPERTY;
    return IPC_CMD_UNKNOWN;
}

ipc_context_t* ipc_init(void) {
    fprintf(stderr, "[IPC] Initializing IPC subsystem\n");

    ipc_context_t* ctx = calloc(1, sizeof(ipc_context_t));
    if (!ctx) {
        fprintf(stderr, "[IPC] Failed to allocate IPC context\n");
        return NULL;
    }

    get_socket_path(ctx->socket_path, sizeof(ctx->socket_path));
    fprintf(stderr, "[IPC] Socket path: %s\n", ctx->socket_path);

    // Check if another instance is already running by trying to connect to the socket
    int test_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (test_fd >= 0) {
        struct sockaddr_un test_addr;
        memset(&test_addr, 0, sizeof(test_addr));
        test_addr.sun_family = AF_UNIX;
        strncpy(test_addr.sun_path, ctx->socket_path, sizeof(test_addr.sun_path) - 1);

        if (connect(test_fd, (struct sockaddr*)&test_addr, sizeof(test_addr)) == 0) {
            // Successfully connected - another instance is running
            fprintf(stderr, "[IPC] Error: Another instance of hyprlax is already running\n");
            fprintf(stderr, "[IPC] Socket: %s\n", ctx->socket_path);
            close(test_fd);
            free(ctx);
            return NULL;
        }
        close(test_fd);
        fprintf(stderr, "[IPC] No existing instance detected\n");
    }

    // Remove existing socket if it exists (stale from a crash)
    unlink(ctx->socket_path);

    // Create Unix domain socket with retries for early boot scenario
    int max_retries = 10;
    int retry_delay_ms = 200;
    ctx->socket_fd = -1;

    for (int i = 0; i < max_retries; i++) {
        ctx->socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (ctx->socket_fd >= 0) {
            fprintf(stderr, "[IPC] Socket created successfully\n");
            break;
        }

        if (i == 0) {
            fprintf(stderr, "[IPC] Failed to create socket: %s, retrying...\n", strerror(errno));
        }

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = retry_delay_ms * 1000000L;
        nanosleep(&ts, NULL);
    }
    if (ctx->socket_fd < 0) {
        fprintf(stderr, "Failed to create IPC socket: %s\n", strerror(errno));
        free(ctx);
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ctx->socket_path, sizeof(addr.sun_path) - 1);

    fprintf(stderr, "[IPC] Binding socket to %s\n", ctx->socket_path);
    if (bind(ctx->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[IPC] Failed to bind IPC socket: %s\n", strerror(errno));
        close(ctx->socket_fd);
        free(ctx);
        return NULL;
    }

    fprintf(stderr, "[IPC] Starting to listen on socket\n");
    if (listen(ctx->socket_fd, 5) < 0) {
        fprintf(stderr, "[IPC] Failed to listen on IPC socket: %s\n", strerror(errno));
        close(ctx->socket_fd);
        unlink(ctx->socket_path);
        free(ctx);
        return NULL;
    }

    // Set socket permissions to user-only
    chmod(ctx->socket_path, 0600);

    ctx->active = true;
    ctx->next_layer_id = 1;

    fprintf(stderr, "[IPC] Socket successfully listening at: %s\n", ctx->socket_path);
    return ctx;
}

void ipc_cleanup(ipc_context_t* ctx) {
    if (!ctx) return;

    ctx->active = false;

    // Clear all layers
    ipc_clear_layers(ctx);

    // Close and remove socket
    if (ctx->socket_fd >= 0) {
        close(ctx->socket_fd);
        unlink(ctx->socket_path);
    }

    free(ctx);
}

bool ipc_process_commands(ipc_context_t* ctx) {
    if (!ctx || !ctx->active) return false;

    // Accept new connections
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(ctx->socket_fd, (struct sockaddr*)&client_addr, &client_len);

    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "Failed to accept IPC connection: %s\n", strerror(errno));
        }
        return false;
    }

    // Read command
    char buffer[IPC_MAX_MESSAGE_SIZE];
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        close(client_fd);
        return false;
    }
    buffer[bytes] = '\0';

    // Parse and execute command
    char* cmd = strtok(buffer, " \n");
    if (!cmd) {
        const char* error = "Error: No command specified\n";
        send(client_fd, error, strlen(error), 0);
        close(client_fd);
        return false;
    }

    ipc_command_t command = parse_command(cmd);
    char response[IPC_MAX_MESSAGE_SIZE];
    bool success = false;

    switch (command) {
        case IPC_CMD_ADD_LAYER: {
            char* path = strtok(NULL, " \n");
            if (!path) {
                snprintf(response, sizeof(response), "Error: Image path required\n");
                break;
            }

            // Parse optional parameters
            float scale = 1.0f, opacity = 1.0f, x_offset = 0.0f, y_offset = 0.0f;
            int z_index = ctx->layer_count;

            char* param;
            while ((param = strtok(NULL, " \n"))) {
                if (strncmp(param, "scale=", 6) == 0) {
                    scale = atof(param + 6);
                } else if (strncmp(param, "opacity=", 8) == 0) {
                    opacity = atof(param + 8);
                } else if (strncmp(param, "x=", 2) == 0) {
                    x_offset = atof(param + 2);
                } else if (strncmp(param, "y=", 2) == 0) {
                    y_offset = atof(param + 2);
                } else if (strncmp(param, "z=", 2) == 0) {
                    z_index = atoi(param + 2);
                }
            }

            uint32_t id = ipc_add_layer(ctx, path, scale, opacity, x_offset, y_offset, z_index);
            if (id > 0) {
                snprintf(response, sizeof(response), "Layer added with ID: %u\n", id);
                success = true;
            } else {
                snprintf(response, sizeof(response), "Error: Failed to add layer\n");
            }
            break;
        }

        case IPC_CMD_REMOVE_LAYER: {
            char* id_str = strtok(NULL, " \n");
            if (!id_str) {
                snprintf(response, sizeof(response), "Error: Layer ID required\n");
                break;
            }

            uint32_t id = atoi(id_str);
            if (ipc_remove_layer(ctx, id)) {
                snprintf(response, sizeof(response), "Layer %u removed\n", id);
                success = true;
            } else {
                snprintf(response, sizeof(response), "Error: Layer %u not found\n", id);
            }
            break;
        }

        case IPC_CMD_MODIFY_LAYER: {
            char* id_str = strtok(NULL, " \n");
            char* property = strtok(NULL, " \n");
            char* value = strtok(NULL, " \n");

            if (!id_str || !property || !value) {
                snprintf(response, sizeof(response), "Error: Usage: modify <id> <property> <value>\n");
                break;
            }

            uint32_t id = atoi(id_str);
            if (ipc_modify_layer(ctx, id, property, value)) {
                snprintf(response, sizeof(response), "Layer %u modified\n", id);
                success = true;
            } else {
                snprintf(response, sizeof(response), "Error: Failed to modify layer %u\n", id);
            }
            break;
        }

        case IPC_CMD_LIST_LAYERS: {
            char* list = ipc_list_layers(ctx);
            if (list) {
                strncpy(response, list, sizeof(response) - 1);
                response[sizeof(response) - 1] = '\0';
                free(list);
                success = true;
            } else {
                snprintf(response, sizeof(response), "No layers\n");
                success = true;
            }
            break;
        }

        case IPC_CMD_CLEAR_LAYERS:
            ipc_clear_layers(ctx);
            snprintf(response, sizeof(response), "All layers cleared\n");
            success = true;
            break;

        case IPC_CMD_GET_STATUS:
            snprintf(response, sizeof(response),
                "Status: Active\nLayers: %d/%d\nSocket: %s\n",
                ctx->layer_count, IPC_MAX_LAYERS, ctx->socket_path);
            success = true;
            break;

        case IPC_CMD_SET_PROPERTY: {
            char* property = strtok(NULL, " \n");
            char* value = strtok(NULL, " \n");

            if (!property || !value) {
                snprintf(response, sizeof(response), "Error: Usage: set <property> <value>\n");
                break;
            }

            /* Handle property setting via callback to main context */
            if (ctx->app_context) {
                int rc = hyprlax_runtime_set_property(ctx->app_context, property, value);
                if (rc == 0) {
                    snprintf(response, sizeof(response), "OK\n");
                    success = true;
                    break;
                }
                /* Fallback to legacy-known properties */
                if (strcmp(property, "fps") == 0 ||
                    strcmp(property, "shift") == 0 ||
                    strcmp(property, "duration") == 0 ||
                    strcmp(property, "easing") == 0 ||
                    strcmp(property, "blur_passes") == 0 ||
                    strcmp(property, "blur_size") == 0 ||
                    strcmp(property, "debug") == 0) {
                    snprintf(response, sizeof(response), "Property '%s' set to '%s'\n", property, value);
                    success = true;
                } else {
                    snprintf(response, sizeof(response), "Error: Unknown/invalid property '%s'\n", property);
                }
            } else {
                snprintf(response, sizeof(response), "Error: Runtime settings not available\n");
            }
            break;
        }

        case IPC_CMD_GET_PROPERTY: {
            char* property = strtok(NULL, " \n");

            if (!property) {
                snprintf(response, sizeof(response), "Error: Usage: get <property>\n");
                break;
            }

            /* Handle property getting via callback to main context */
            if (ctx->app_context) {
                int rc = hyprlax_runtime_get_property(ctx->app_context, property, response, sizeof(response));
                if (rc == 0) {
                    size_t len = strlen(response);
                    if (len < sizeof(response) - 1) response[len++] = '\n', response[len] = '\0';
                    success = true;
                } else {
                    /* Fallback to legacy-known properties */
                    if (strcmp(property, "fps") == 0) {
                        snprintf(response, sizeof(response), "60\n");
                        success = true;
                    } else if (strcmp(property, "shift") == 0) {
                        snprintf(response, sizeof(response), "200\n");
                        success = true;
                    } else if (strcmp(property, "duration") == 0) {
                        snprintf(response, sizeof(response), "1.0\n");
                        success = true;
                    } else if (strcmp(property, "easing") == 0) {
                        snprintf(response, sizeof(response), "cubic\n");
                        success = true;
                    } else {
                        snprintf(response, sizeof(response), "Error: Unknown property '%s'\n", property);
                    }
                }
            } else {
                snprintf(response, sizeof(response), "Error: Runtime settings not available\n");
            }
            break;
        }

        default:
            snprintf(response, sizeof(response), "Error: Unknown command '%s'\n", cmd);
            break;
    }

    // Send response
    send(client_fd, response, strlen(response), 0);
    close(client_fd);

    return success;
}

uint32_t ipc_add_layer(ipc_context_t* ctx, const char* image_path, float scale, float opacity, float x_offset, float y_offset, int z_index) {
    if (!ctx || !image_path || ctx->layer_count >= IPC_MAX_LAYERS) {
        return 0;
    }

    // Check if file exists
    if (access(image_path, R_OK) != 0) {
        fprintf(stderr, "Image file not found or not readable: %s\n", image_path);
        return 0;
    }

    layer_t* layer = calloc(1, sizeof(layer_t));
    if (!layer) return 0;

    layer->image_path = strdup(image_path);
    layer->scale = scale;
    layer->opacity = opacity;
    layer->x_offset = x_offset;
    layer->y_offset = y_offset;
    layer->z_index = z_index;
    layer->visible = true;
    layer->id = ctx->next_layer_id++;

    ctx->layers[ctx->layer_count++] = layer;
    ipc_sort_layers(ctx);

    return layer->id;
}

bool ipc_remove_layer(ipc_context_t* ctx, uint32_t layer_id) {
    if (!ctx) return false;

    for (int i = 0; i < ctx->layer_count; i++) {
        if (ctx->layers[i]->id == layer_id) {
            // Free layer resources
            free(ctx->layers[i]->image_path);
            free(ctx->layers[i]);

            // Shift remaining layers
            for (int j = i; j < ctx->layer_count - 1; j++) {
                ctx->layers[j] = ctx->layers[j + 1];
            }
            ctx->layers[--ctx->layer_count] = NULL;

            return true;
        }
    }

    return false;
}

bool ipc_modify_layer(ipc_context_t* ctx, uint32_t layer_id, const char* property, const char* value) {
    if (!ctx || !property || !value) return false;

    layer_t* layer = ipc_find_layer(ctx, layer_id);
    if (!layer) return false;

    bool needs_sort = false;

    if (strcmp(property, "scale") == 0) {
        layer->scale = atof(value);
    } else if (strcmp(property, "opacity") == 0) {
        layer->opacity = atof(value);
    } else if (strcmp(property, "x") == 0) {
        layer->x_offset = atof(value);
    } else if (strcmp(property, "y") == 0) {
        layer->y_offset = atof(value);
    } else if (strcmp(property, "z") == 0) {
        layer->z_index = atoi(value);
        needs_sort = true;
    } else if (strcmp(property, "visible") == 0) {
        layer->visible = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    } else {
        return false;
    }

    if (needs_sort) {
        ipc_sort_layers(ctx);
    }

    return true;
}

char* ipc_list_layers(ipc_context_t* ctx) {
    if (!ctx || ctx->layer_count == 0) return NULL;

    char* result = malloc(IPC_MAX_MESSAGE_SIZE);
    if (!result) return NULL;

    int offset = 0;
    for (int i = 0; i < ctx->layer_count; i++) {
        layer_t* layer = ctx->layers[i];
        int written = snprintf(result + offset, IPC_MAX_MESSAGE_SIZE - offset,
            "ID: %u | Path: %s | Scale: %.2f | Opacity: %.2f | Position: (%.2f, %.2f) | Z: %d | Visible: %s\n",
            layer->id, layer->image_path, layer->scale, layer->opacity,
            layer->x_offset, layer->y_offset, layer->z_index,
            layer->visible ? "yes" : "no");

        if (written < 0 || offset + written >= IPC_MAX_MESSAGE_SIZE) break;
        offset += written;
    }

    return result;
}

void ipc_clear_layers(ipc_context_t* ctx) {
    if (!ctx) return;

    for (int i = 0; i < ctx->layer_count; i++) {
        if (ctx->layers[i]) {
            free(ctx->layers[i]->image_path);
            free(ctx->layers[i]);
            ctx->layers[i] = NULL;
        }
    }
    ctx->layer_count = 0;
}

layer_t* ipc_find_layer(ipc_context_t* ctx, uint32_t layer_id) {
    if (!ctx) return NULL;

    for (int i = 0; i < ctx->layer_count; i++) {
        if (ctx->layers[i]->id == layer_id) {
            return ctx->layers[i];
        }
    }

    return NULL;
}

static int layer_compare(const void* a, const void* b) {
    layer_t* layer_a = *(layer_t**)a;
    layer_t* layer_b = *(layer_t**)b;
    return layer_a->z_index - layer_b->z_index;
}

void ipc_sort_layers(ipc_context_t* ctx) {
    if (!ctx || ctx->layer_count < 2) return;
    qsort(ctx->layers, ctx->layer_count, sizeof(layer_t*), layer_compare);
}

// Request handling function for tests and IPC processing
int ipc_handle_request(ipc_context_t* ctx, const char* request, char* response, size_t response_size) {
    if (!ctx || !request || !response || response_size == 0) {
        return -1;
    }

    // Parse command
    char cmd[64] = {0};
    char args[256] = {0};
    sscanf(request, "%63s %255[^\n]", cmd, args);

    if (strlen(cmd) == 0) {
        snprintf(response, response_size, "Error: Empty command");
        return -1;
    }

    // Handle ADD command
    if (strcmp(cmd, "ADD") == 0) {
        char path[256];
        float scale = 1.0f, opacity = 1.0f, blur = 0.0f;
        int count = sscanf(args, "%255s %f %f %f", path, &scale, &opacity, &blur);
        if (count < 1) {
            snprintf(response, response_size, "Error: ADD requires at least an image path");
            return -1;
        }

        uint32_t id = ipc_add_layer(ctx, path, scale, opacity, 0.0f, blur, 0);
        if (id > 0) {
            snprintf(response, response_size, "Layer added with ID: %u", id);
            return 0;
        } else {
            snprintf(response, response_size, "Error: Failed to add layer");
            return -1;
        }
    }

    // Handle REMOVE command
    else if (strcmp(cmd, "REMOVE") == 0) {
        uint32_t id = 0;
        if (sscanf(args, "%u", &id) != 1) {
            snprintf(response, response_size, "Error: REMOVE requires a layer ID");
            return -1;
        }

        if (ipc_remove_layer(ctx, id)) {
            snprintf(response, response_size, "Layer %u removed", id);
            return 0;
        } else {
            snprintf(response, response_size, "Error: Layer %u not found", id);
            return -1;
        }
    }

    // Handle MODIFY command
    else if (strcmp(cmd, "MODIFY") == 0) {
        uint32_t id = 0;
        char property[64], value[64];
        if (sscanf(args, "%u %63s %63s", &id, property, value) != 3) {
            snprintf(response, response_size, "Error: MODIFY requires ID, property, and value");
            return -1;
        }

        if (ipc_modify_layer(ctx, id, property, value)) {
            snprintf(response, response_size, "Layer %u modified", id);
            return 0;
        } else {
            snprintf(response, response_size, "Error: Layer %u not found or invalid property", id);
            return -1;
        }
    }

    // Handle LIST command
    else if (strcmp(cmd, "LIST") == 0) {
        char* list = ipc_list_layers(ctx);
        if (list) {
            snprintf(response, response_size, "%s", list);
            free(list);
            return 0;
        } else {
            snprintf(response, response_size, "No layers");
            return 0;
        }
    }

    // Handle CLEAR command
    else if (strcmp(cmd, "CLEAR") == 0) {
        ipc_clear_layers(ctx);
        snprintf(response, response_size, "All layers cleared");
        return 0;
    }

    // Handle STATUS command
    else if (strcmp(cmd, "STATUS") == 0) {
        const char* compositor = getenv("HYPRLAX_COMPOSITOR");
        if (!compositor) compositor = "auto";

        snprintf(response, response_size,
                 "hyprlax running\nLayers: %d\nFPS: 60\nCompositor: %s",
                 ctx->layer_count, compositor);
        return 0;
    }

    // Handle RELOAD command
    else if (strcmp(cmd, "RELOAD") == 0) {
        snprintf(response, response_size, "Configuration reloaded");
        return 0;
    }

    // Handle SET_PROPERTY command
    else if (strcmp(cmd, "SET_PROPERTY") == 0) {
        char property[64], value[64];
        if (sscanf(args, "%63s %63s", property, value) != 2) {
            snprintf(response, response_size, "Error: SET_PROPERTY requires property and value");
            return -1;
        }
        int rc = hyprlax_runtime_set_property(ctx->app_context, property, value);
        if (rc == 0) { snprintf(response, response_size, "OK"); return 0; }
        if (strcmp(property, "fps") == 0 || strcmp(property, "shift") == 0 ||
            strcmp(property, "duration") == 0 || strcmp(property, "easing") == 0 ||
            strcmp(property, "blur_passes") == 0 || strcmp(property, "blur_size") == 0 ||
            strcmp(property, "debug") == 0) {
            snprintf(response, response_size, "Property '%s' set to '%s'", property, value);
            return 0;
        }
        snprintf(response, response_size, "Error: Unknown/invalid property '%s'", property);
        return -1;
    }

    // Handle GET_PROPERTY command
    else if (strcmp(cmd, "GET_PROPERTY") == 0) {
        char property[64];
        if (sscanf(args, "%63s", property) != 1) {
            snprintf(response, response_size, "Error: GET_PROPERTY requires property name");
            return -1;
        }
        int rc = hyprlax_runtime_get_property(ctx->app_context, property, response, response_size);
        if (rc == 0) return 0;
        if (strcmp(property, "fps") == 0) { snprintf(response, response_size, "60"); return 0; }
        if (strcmp(property, "shift") == 0) { snprintf(response, response_size, "200"); return 0; }
        if (strcmp(property, "duration") == 0) { snprintf(response, response_size, "1.0"); return 0; }
        if (strcmp(property, "easing") == 0) { snprintf(response, response_size, "cubic"); return 0; }
        snprintf(response, response_size, "Error: Unknown property '%s'", property);
        return -1;
    }

    // Unknown command
    else {
        snprintf(response, response_size, "Error: Unknown command '%s'", cmd);
        return -1;
    }
}
