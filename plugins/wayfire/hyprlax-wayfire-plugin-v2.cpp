/*
 * hyprlax-wayfire-plugin-v2.cpp - Wayfire plugin for Hyprlax with rendering
 * 
 * Uses Wayfire's scene graph to display textures from shared memory.
 */

#define WAYFIRE_PLUGIN

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <vector>
#include <iostream>
#include <GLES3/gl3.h>

/* Shared memory header for frame data */
struct hyprlax_frame_header {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t stride;
    uint64_t timestamp;
    uint32_t frame_number;
};

/* Texture node for displaying hyprlax frames */
class hyprlax_texture_node_t : public wf::scene::node_t {
private:
    class render_instance_t : public wf::scene::simple_render_instance_t<hyprlax_texture_node_t> {
    public:
        using simple_render_instance_t::simple_render_instance_t;

        void render(const wf::scene::render_instruction_t& data) {
            if (self->texture.tex_id == (GLuint)-1) {
                return;
            }
            
            auto g = self->get_bounding_box();
            wf::texture_t tex;
            tex.tex = self->texture.tex_id;
            tex.target = GL_TEXTURE_2D;
            tex.type = wf::TEXTURE_TYPE_RGBA;
            
            // Add texture to render pass
            data.pass->add_texture(tex, data.target, g, data.damage);
        }
    };

    wf::gles_texture_t texture;
    wf::geometry_t geometry;
    
public:
    hyprlax_texture_node_t() : node_t(false) {
        texture.tex_id = (GLuint)-1;
        texture.target = GL_TEXTURE_2D;
        texture.type = wf::TEXTURE_TYPE_RGBA;
    }
    
    ~hyprlax_texture_node_t() {
        if (texture.tex_id != (GLuint)-1) {
            GL_CALL(glDeleteTextures(1, &texture.tex_id));
        }
    }
    
    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
                            wf::scene::damage_callback push_damage, wf::output_t *output) override {
        instances.push_back(std::make_unique<render_instance_t>(this, push_damage, output));
    }
    
    wf::geometry_t get_bounding_box() override {
        return geometry;
    }
    
    void set_geometry(wf::geometry_t g) {
        wf::scene::damage_node(shared_from_this(), geometry);
        geometry = g;
        wf::scene::damage_node(shared_from_this(), geometry);
    }
    
    bool update_texture(const void* data, uint32_t width, uint32_t height, uint32_t stride) {
        if (texture.tex_id == (GLuint)-1) {
            GL_CALL(glGenTextures(1, &texture.tex_id));
        }
        
        GL_CALL(glBindTexture(GL_TEXTURE_2D, texture.tex_id));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4));
        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                            GL_RGBA, GL_UNSIGNED_BYTE, data));
        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        
        // Damage the area to trigger repaint
        wf::scene::damage_node(shared_from_this(), geometry);
        
        return true;
    }
};

/* Main plugin class */
class wayfire_hyprlax : public wf::per_output_plugin_instance_t {
private:
    std::shared_ptr<hyprlax_texture_node_t> texture_node;
    int ipc_socket = -1;
    int client_socket = -1;
    wl_event_source *ipc_source = nullptr;
    wl_event_source *client_source = nullptr;
    
    /* Handle workspace changes to keep wallpaper in place */
    wf::signal::connection_t<wf::workspace_changed_signal> workspace_changed{
        [this](wf::workspace_changed_signal *data) {
            // Update position to compensate for workspace movement
            if (texture_node) {
                auto screen = output->get_screen_size();
                auto dx = (data->old_viewport.x - data->new_viewport.x) * screen.width;
                auto dy = (data->old_viewport.y - data->new_viewport.y) * screen.height;
                
                auto g = texture_node->get_bounding_box();
                g.x += dx;
                g.y += dy;
                texture_node->set_geometry(g);
            }
        }
    };
    
    static int handle_client_message(int fd, uint32_t mask, void *data) {
        (void)fd;
        auto *plugin = static_cast<wayfire_hyprlax*>(data);
        
        if (mask & WL_EVENT_HANGUP) {
            std::cout << "[hyprlax] Client disconnected" << std::endl;
            plugin->disconnect_client();
            return 0;
        }
        
        if (mask & WL_EVENT_READABLE) {
            plugin->process_client_message();
        }
        
        return 0;
    }
    
    void process_client_message() {
        // Receive shared memory fd
        char buf[256];
        struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
        
        union {
            struct cmsghdr cm;
            char control[CMSG_SPACE(sizeof(int))];
        } control_un = {};
        
        struct msghdr msg = {
            .msg_name = nullptr,
            .msg_namelen = 0,
            .msg_iov = &iov,
            .msg_iovlen = 1,
            .msg_control = control_un.control,
            .msg_controllen = sizeof(control_un.control),
            .msg_flags = 0
        };
        
        ssize_t n = recvmsg(client_socket, &msg, 0);
        if (n <= 0) {
            disconnect_client();
            return;
        }
        
        // Extract file descriptor
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        if (cmsg && cmsg->cmsg_level == SOL_SOCKET && 
            cmsg->cmsg_type == SCM_RIGHTS) {
            int fd = *((int*)CMSG_DATA(cmsg));
            update_from_shm(fd);
            close(fd);
            
            // Send acknowledgment
            const char *ack = "OK";
            send(client_socket, ack, strlen(ack), 0);
        }
    }
    
    void update_from_shm(int fd) {
        struct stat sb;
        if (fstat(fd, &sb) == -1) {
            return;
        }
        
        if ((size_t)sb.st_size < sizeof(hyprlax_frame_header)) {
            return;
        }
        
        void *shm_data = mmap(nullptr, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
        if (shm_data == MAP_FAILED) {
            return;
        }
        
        auto *header = static_cast<hyprlax_frame_header*>(shm_data);
        auto *pixels = static_cast<uint8_t*>(shm_data) + sizeof(hyprlax_frame_header);
        
        if (texture_node) {
            texture_node->update_texture(pixels, header->width, header->height, header->stride);
        }
        
        munmap(shm_data, sb.st_size);
    }
    
    static int handle_ipc_connection(int fd, uint32_t mask, void *data) {
        (void)fd;
        auto *plugin = static_cast<wayfire_hyprlax*>(data);
        
        if (mask & WL_EVENT_READABLE) {
            // Accept connection
            plugin->client_socket = accept(plugin->ipc_socket, nullptr, nullptr);
            if (plugin->client_socket >= 0) {
                std::cout << "[hyprlax] Client connected" << std::endl;
                
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
    }
    
    bool setup_ipc_socket() {
        ipc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
        if (ipc_socket < 0) {
            return false;
        }
        
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (!runtime_dir) {
            close(ipc_socket);
            ipc_socket = -1;
            return false;
        }
        
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof(addr.sun_path), 
                "%s/hyprlax-wayfire.sock", runtime_dir);
        
        unlink(addr.sun_path);
        
        if (bind(ipc_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(ipc_socket);
            ipc_socket = -1;
            return false;
        }
        
        if (listen(ipc_socket, 1) < 0) {
            close(ipc_socket);
            ipc_socket = -1;
            return false;
        }
        
        ipc_source = wl_event_loop_add_fd(
            wf::get_core().ev_loop,
            ipc_socket,
            WL_EVENT_READABLE,
            handle_ipc_connection,
            this
        );
        
        std::cout << "[hyprlax] IPC socket ready" << std::endl;
        return true;
    }
    
public:
    void init() override {
        std::cout << "[hyprlax] Plugin initializing for output " << output->to_string() << std::endl;
        
        // Create texture node
        texture_node = std::make_shared<hyprlax_texture_node_t>();
        
        // Set initial geometry to cover the output
        auto og = output->get_layout_geometry();
        texture_node->set_geometry({0, 0, og.width, og.height});
        
        // Add to scene graph at background layer
        auto root = output->node_for_layer(wf::scene::layer::BACKGROUND);
        wf::scene::add_front(root, texture_node);
        
        // Connect signals
        output->connect(&workspace_changed);
        
        // Setup IPC
        if (!setup_ipc_socket()) {
            std::cerr << "[hyprlax] Failed to setup IPC" << std::endl;
        }
        
        std::cout << "[hyprlax] Plugin initialized" << std::endl;
    }
    
    void fini() override {
        std::cout << "[hyprlax] Plugin shutting down" << std::endl;
        
        // Remove from scene graph
        if (texture_node) {
            wf::scene::remove_child(texture_node);
            texture_node.reset();
        }
        
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

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_hyprlax>);