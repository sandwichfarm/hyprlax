/*
 * hyprlax-wayfire-plugin.cpp - Simplified Wayfire plugin for Hyprlax
 * 
 * This plugin provides native Wayfire integration for hyprlax.
 * Currently provides IPC communication; rendering to be added.
 */

#define WAYFIRE_PLUGIN

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

/* Main plugin class */
class wayfire_hyprlax : public wf::plugin_interface_t {
private:
    int ipc_socket = -1;
    int client_socket = -1;
    wl_event_source *ipc_source = nullptr;
    wl_event_source *client_source = nullptr;
    
    static int handle_client_message(int fd, uint32_t mask, void *data) {
        auto *plugin = static_cast<wayfire_hyprlax*>(data);
        
        if (mask & WL_EVENT_HANGUP) {
            std::cout << "[hyprlax] Client disconnected" << std::endl;
            plugin->disconnect_client();
            return 0;
        }
        
        if (mask & WL_EVENT_READABLE) {
            char buf[256];
            ssize_t n = recv(plugin->client_socket, buf, sizeof(buf)-1, 0);
            if (n > 0) {
                buf[n] = '\0';
                std::cout << "[hyprlax] Received: " << buf << std::endl;
                
                // Send acknowledgment
                const char *ack = "OK";
                send(plugin->client_socket, ack, strlen(ack), 0);
            }
        }
        
        return 0;
    }
    
    static int handle_ipc_connection(int fd, uint32_t mask, void *data) {
        auto *plugin = static_cast<wayfire_hyprlax*>(data);
        
        if (mask & WL_EVENT_READABLE) {
            // Accept connection
            plugin->client_socket = accept(plugin->ipc_socket, nullptr, nullptr);
            if (plugin->client_socket >= 0) {
                std::cout << "[hyprlax] Client connected" << std::endl;
                
                // Remove the accept handler
                if (plugin->ipc_source) {
                    wl_event_source_remove(plugin->ipc_source);
                }
                
                // Add client handler
                plugin->client_source = wl_event_loop_add_fd(
                    wf::get_core().ev_loop,
                    plugin->client_socket,
                    WL_EVENT_READABLE | WL_EVENT_HANGUP,
                    handle_client_message,
                    plugin
                );
            }
        }
        
        return 0;
    }
    
    void disconnect_client() {
        if (client_source) {
            wl_event_source_remove(client_source);
            client_source = nullptr;
        }
        if (client_socket >= 0) {
            close(client_socket);
            client_socket = -1;
        }
        
        // Re-add accept handler
        if (ipc_socket >= 0) {
            ipc_source = wl_event_loop_add_fd(
                wf::get_core().ev_loop,
                ipc_socket,
                WL_EVENT_READABLE,
                handle_ipc_connection,
                this
            );
        }
    }
    
    bool setup_ipc_socket() {
        // Create Unix domain socket
        ipc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
        if (ipc_socket < 0) {
            std::cerr << "[hyprlax] Failed to create IPC socket" << std::endl;
            return false;
        }
        
        // Create socket path
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (!runtime_dir) {
            std::cerr << "[hyprlax] XDG_RUNTIME_DIR not set" << std::endl;
            close(ipc_socket);
            ipc_socket = -1;
            return false;
        }
        
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof(addr.sun_path), 
                "%s/hyprlax-wayfire.sock", runtime_dir);
        
        // Remove existing socket
        unlink(addr.sun_path);
        
        // Bind socket
        if (bind(ipc_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[hyprlax] Failed to bind IPC socket" << std::endl;
            close(ipc_socket);
            ipc_socket = -1;
            return false;
        }
        
        // Listen for connections
        if (listen(ipc_socket, 1) < 0) {
            std::cerr << "[hyprlax] Failed to listen on IPC socket" << std::endl;
            close(ipc_socket);
            ipc_socket = -1;
            return false;
        }
        
        // Add to event loop
        ipc_source = wl_event_loop_add_fd(
            wf::get_core().ev_loop,
            ipc_socket,
            WL_EVENT_READABLE,
            handle_ipc_connection,
            this
        );
        
        std::cout << "[hyprlax] IPC socket ready at " << addr.sun_path << std::endl;
        return true;
    }
    
public:
    void init() override {
        std::cout << "[hyprlax] Plugin initializing" << std::endl;
        
        // Setup IPC for hyprlax communication
        if (!setup_ipc_socket()) {
            std::cerr << "[hyprlax] Failed to setup IPC" << std::endl;
        }
        
        std::cout << "[hyprlax] Plugin initialized" << std::endl;
    }
    
    void fini() override {
        std::cout << "[hyprlax] Plugin shutting down" << std::endl;
        
        // Cleanup IPC
        disconnect_client();
        
        if (ipc_source) {
            wl_event_source_remove(ipc_source);
            ipc_source = nullptr;
        }
        
        if (ipc_socket >= 0) {
            close(ipc_socket);
            ipc_socket = -1;
        }
        
        std::cout << "[hyprlax] Plugin shutdown complete" << std::endl;
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_hyprlax);