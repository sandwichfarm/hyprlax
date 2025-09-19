/*
 * hyprlax-wayfire-plugin-minimal.cpp - Minimal working Wayfire plugin for Hyprlax
 * 
 * Creates a background layer for texture display.
 */

#define WAYFIRE_PLUGIN

#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/signal-definitions.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <memory>
#include <iostream>

/* Simple background node */
class hyprlax_bg_node_t : public wf::scene::node_t {
public:
    hyprlax_bg_node_t() : node_t(false) {}
    
    wf::geometry_t get_bounding_box() override {
        return {0, 0, 1920, 1080};  // Default size
    }
};

/* Per-output plugin instance */
class wayfire_hyprlax : public wf::per_output_plugin_instance_t {
private:
    std::shared_ptr<hyprlax_bg_node_t> bg_node;
    static int ipc_socket;
    static int client_socket;
    static wl_event_source *ipc_source;
    static bool ipc_initialized;
    
    static bool setup_ipc_socket() {
        if (ipc_initialized) return true;
        
        ipc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
        if (ipc_socket < 0) return false;
        
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (!runtime_dir) {
            close(ipc_socket);
            return false;
        }
        
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof(addr.sun_path), 
                "%s/hyprlax-wayfire.sock", runtime_dir);
        
        unlink(addr.sun_path);
        
        if (bind(ipc_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
            listen(ipc_socket, 1) < 0) {
            close(ipc_socket);
            return false;
        }
        
        ipc_source = wl_event_loop_add_fd(
            wf::get_core().ev_loop, ipc_socket, WL_EVENT_READABLE,
            [](int, uint32_t, void*) {
                client_socket = accept(ipc_socket, nullptr, nullptr);
                if (client_socket >= 0) {
                    std::cout << "[hyprlax] Client connected" << std::endl;
                }
                return 0;
            }, nullptr);
        
        ipc_initialized = true;
        std::cout << "[hyprlax] IPC ready at " << addr.sun_path << std::endl;
        return true;
    }
    
public:
    void init() override {
        std::cout << "[hyprlax] Plugin init for output" << std::endl;
        
        // Setup IPC (only once, shared between outputs)
        setup_ipc_socket();
        
        // Create background node
        bg_node = std::make_shared<hyprlax_bg_node_t>();
        
        // Add to background layer
        auto bg_layer = output->node_for_layer(wf::scene::layer::BACKGROUND);
        wf::scene::add_front(bg_layer, bg_node);
        
        std::cout << "[hyprlax] Background node added" << std::endl;
    }
    
    void fini() override {
        std::cout << "[hyprlax] Plugin fini" << std::endl;
        
        // Remove from scene
        if (bg_node) {
            wf::scene::remove_child(bg_node);
            bg_node.reset();
        }
    }
};

/* Static member definitions */
int wayfire_hyprlax::ipc_socket = -1;
int wayfire_hyprlax::client_socket = -1;
wl_event_source* wayfire_hyprlax::ipc_source = nullptr;
bool wayfire_hyprlax::ipc_initialized = false;

/* Plugin definition - using per_output_plugin_t template */
DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_hyprlax>);