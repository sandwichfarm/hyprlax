/*
 * hyprlax-wayfire-plugin.cpp - Wayfire plugin for Hyprlax
 * 
 * This plugin provides native Wayfire integration for hyprlax,
 * solving workspace persistence issues with the vswitch plugin.
 */

#define WAYFIRE_PLUGIN
#define WLR_USE_UNSTABLE

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <vector>
#include <GLES3/gl3.h>

/* Shared memory header for frame data */
struct hyprlax_frame_header {
    uint32_t width;
    uint32_t height;
    uint32_t format;  /* GL format */
    uint32_t stride;
    uint64_t timestamp;
    uint32_t frame_number;
};

/* Simple texture structure */
struct simple_texture_t {
    GLuint tex = (GLuint)-1;
    int width = 0;
    int height = 0;
};

/* Custom view for hyprlax wallpaper */
class hyprlax_view_t : public wf::color_rect_view_t {
private:
    simple_texture_t texture;
    int shm_fd = -1;
    void *shm_data = nullptr;
    size_t shm_size = 0;
    
public:
    hyprlax_view_t() {
        role = wf::VIEW_ROLE_DESKTOP_ENVIRONMENT;
        texture.tex = (GLuint)-1;
    }
    
    ~hyprlax_view_t() {
        cleanup_shm();
        if (texture.tex != (GLuint)-1) {
            GL_CALL(glDeleteTextures(1, &texture.tex));
        }
    }
    
    void set_output(wf::output_t *output) override {
        wf::color_rect_view_t::set_output(output);
        
        if (output) {
            /* Add to BACKGROUND layer */
            output->workspace->add_view(self(), wf::LAYER_BACKGROUND);
        }
    }
    
    void cleanup_shm() {
        if (shm_data) {
            munmap(shm_data, shm_size);
            shm_data = nullptr;
        }
        if (shm_fd >= 0) {
            close(shm_fd);
            shm_fd = -1;
        }
        shm_size = 0;
    }
    
    bool update_from_shm(int fd) {
        cleanup_shm();
        
        /* Get size of shared memory */
        struct stat sb;
        if (fstat(fd, &sb) == -1) {
            LOGE("Failed to stat shared memory");
            return false;
        }
        
        shm_size = sb.st_size;
        if (shm_size < sizeof(hyprlax_frame_header)) {
            LOGE("Shared memory too small");
            return false;
        }
        
        /* Map shared memory */
        shm_data = mmap(nullptr, shm_size, PROT_READ, MAP_SHARED, fd, 0);
        if (shm_data == MAP_FAILED) {
            LOGE("Failed to map shared memory");
            shm_data = nullptr;
            return false;
        }
        
        shm_fd = fd;
        
        /* Update texture from shared memory */
        return update_texture();
    }
    
    bool update_texture() {
        if (!shm_data) return false;
        
        auto *header = static_cast<hyprlax_frame_header*>(shm_data);
        auto *pixels = static_cast<uint8_t*>(shm_data) + sizeof(hyprlax_frame_header);
        
        texture.width = header->width;
        texture.height = header->height;
        
        /* Create or update OpenGL texture */
        OpenGL::render_begin();
        
        if (texture.tex == (GLuint)-1) {
            GL_CALL(glGenTextures(1, &texture.tex));
        }
        
        GL_CALL(glBindTexture(GL_TEXTURE_2D, texture.tex));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        
        /* Upload texture data */
        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, header->stride / 4)); /* Assuming RGBA */
        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 
                             header->width, header->height, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, pixels));
        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        
        OpenGL::render_end();
        
        damage();
        return true;
    }
    
    void simple_render(const wf::render_target_t &fb, int x, int y,
                       const wf::region_t &damage) override {
        if (texture.tex == (GLuint)-1) {
            /* No texture yet, render solid color */
            wf::color_rect_view_t::simple_render(fb, x, y, damage);
            return;
        }
        
        OpenGL::render_begin(fb);
        for (auto &box : damage) {
            fb.logic_scissor(wlr_box_from_pixman_box(box));
            
            /* Render background color if needed */
            wf::color_t premultiply{_color.r * _color.a, _color.g * _color.a, 
                                   _color.b * _color.a, _color.a};
            OpenGL::render_rectangle({x, y, fb.geometry.width, fb.geometry.height},
                                   premultiply, fb.get_orthographic_projection());
            
            /* Render texture */
            OpenGL::render_texture(wf::texture_t{texture.tex}, fb, 
                                 {x, y, fb.geometry.width, fb.geometry.height},
                                 glm::vec4(1, 1, 1, 1),
                                 OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
        }
        OpenGL::render_end();
    }
};

/* Main plugin class */
class wayfire_hyprlax : public wf::plugin_interface_t {
private:
    std::vector<nonstd::observer_ptr<hyprlax_view_t>> views;
    int ipc_socket = -1;
    int client_socket = -1;
    wl_event_source *ipc_source = nullptr;
    
    /* Handle workspace changes to keep wallpaper in place */
    wf::signal_connection_t workspace_changed{[this](wf::signal_data_t *sigdata) {
        auto *data = static_cast<wf::workspace_changed_signal*>(sigdata);
        auto screen = output->get_screen_size();
        
        /* Calculate movement delta */
        auto dx = (data->old_viewport.x - data->new_viewport.x) * screen.width;
        auto dy = (data->old_viewport.y - data->new_viewport.y) * screen.height;
        
        /* Move views to compensate for workspace switch */
        for (auto view : views) {
            auto vg = view->get_wm_geometry();
            view->move(vg.x + dx, vg.y + dy);
        }
    }};
    
    /* Handle workspace grid changes */
    wf::signal_connection_t workspace_grid_changed{[this](wf::signal_data_t *sigdata) {
        auto *data = static_cast<wf::workspace_grid_changed_signal*>(sigdata);
        recreate_views(data->new_grid_size);
    }};
    
    /* Handle output configuration changes */
    wf::signal_connection_t output_configuration_changed{[this](wf::signal_data_t *sigdata) {
        auto *data = static_cast<wf::output_configuration_changed_signal*>(sigdata);
        
        if (!data->changed_fields || (data->changed_fields & wf::OUTPUT_SOURCE_CHANGE)) {
            return;
        }
        
        update_view_geometries();
    }};
    
    static int handle_ipc_message(int fd, uint32_t mask, void *data) {
        auto *plugin = static_cast<wayfire_hyprlax*>(data);
        
        if (mask & WL_EVENT_HANGUP) {
            LOGI("Hyprlax client disconnected");
            plugin->disconnect_client();
            return 0;
        }
        
        if (mask & WL_EVENT_READABLE) {
            plugin->process_ipc_message();
        }
        
        return 0;
    }
    
    void process_ipc_message() {
        /* Simple protocol: receive shared memory fd for frame buffer */
        char buf[256];
        struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
        
        union {
            struct cmsghdr cm;
            char control[CMSG_SPACE(sizeof(int))];
        } control_un = {};
        
        struct msghdr msg = {
            .msg_iov = &iov,
            .msg_iovlen = 1,
            .msg_control = control_un.control,
            .msg_controllen = sizeof(control_un.control)
        };
        
        ssize_t n = recvmsg(client_socket, &msg, 0);
        if (n <= 0) {
            LOGE("Failed to receive IPC message");
            disconnect_client();
            return;
        }
        
        /* Extract file descriptor from ancillary data */
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        if (cmsg && cmsg->cmsg_level == SOL_SOCKET && 
            cmsg->cmsg_type == SCM_RIGHTS) {
            int fd = *((int*)CMSG_DATA(cmsg));
            
            /* Update view with new frame */
            if (!views.empty()) {
                views[0]->update_from_shm(fd);
            }
            
            /* Send acknowledgment */
            const char *ack = "OK";
            send(client_socket, ack, strlen(ack), 0);
        }
    }
    
    void disconnect_client() {
        if (ipc_source) {
            wl_event_source_remove(ipc_source);
            ipc_source = nullptr;
        }
        if (client_socket >= 0) {
            close(client_socket);
            client_socket = -1;
        }
    }
    
    void recreate_views(wf::dimensions_t grid_size) {
        /* Clear existing views */
        for (auto &view : views) {
            view->close();
        }
        views.clear();
        
        /* Create new views for each workspace */
        auto og = output->get_relative_geometry();
        auto ws = output->workspace->get_current_workspace();
        
        int count = grid_size.width * grid_size.height;
        views.resize(count);
        
        for (int i = 0; i < count; i++) {
            int x = i % grid_size.width;
            int y = i / grid_size.width;
            
            auto view = std::make_unique<hyprlax_view_t>();
            views[i] = nonstd::observer_ptr<hyprlax_view_t>{view};
            view->set_geometry({
                (x - ws.x) * og.width,
                (y - ws.y) * og.height,
                og.width,
                og.height
            });
            wf::get_core().add_view(std::move(view));
        }
    }
    
    void update_view_geometries() {
        auto og = output->get_relative_geometry();
        auto ws = output->workspace->get_current_workspace();
        auto grid = output->workspace->get_workspace_grid_size();
        
        for (int i = 0; i < (int)views.size(); i++) {
            int x = i % grid.width;
            int y = i / grid.width;
            views[i]->set_geometry({
                (x - ws.x) * og.width,
                (y - ws.y) * og.height,
                og.width,
                og.height
            });
            views[i]->damage();
        }
    }
    
    bool setup_ipc_socket() {
        /* Create Unix domain socket for IPC */
        ipc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
        if (ipc_socket < 0) {
            LOGE("Failed to create IPC socket");
            return false;
        }
        
        /* Create socket path */
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (!runtime_dir) {
            LOGE("XDG_RUNTIME_DIR not set");
            close(ipc_socket);
            ipc_socket = -1;
            return false;
        }
        
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof(addr.sun_path), 
                "%s/hyprlax-wayfire.sock", runtime_dir);
        
        /* Remove existing socket */
        unlink(addr.sun_path);
        
        /* Bind socket */
        if (bind(ipc_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            LOGE("Failed to bind IPC socket");
            close(ipc_socket);
            ipc_socket = -1;
            return false;
        }
        
        /* Listen for connections */
        if (listen(ipc_socket, 1) < 0) {
            LOGE("Failed to listen on IPC socket");
            close(ipc_socket);
            ipc_socket = -1;
            return false;
        }
        
        /* Add to event loop */
        ipc_source = wl_event_loop_add_fd(wf::get_core().ev_loop, ipc_socket,
                                         WL_EVENT_READABLE,
                                         [](int fd, uint32_t mask, void *data) {
            auto *plugin = static_cast<wayfire_hyprlax*>(data);
            
            /* Accept connection */
            plugin->client_socket = accept(plugin->ipc_socket, nullptr, nullptr);
            if (plugin->client_socket < 0) {
                LOGE("Failed to accept IPC connection");
                return 0;
            }
            
            LOGI("Hyprlax client connected");
            
            /* Add client to event loop */
            plugin->ipc_source = wl_event_loop_add_fd(wf::get_core().ev_loop,
                                                     plugin->client_socket,
                                                     WL_EVENT_READABLE | WL_EVENT_HANGUP,
                                                     handle_ipc_message, plugin);
            return 0;
        }, this);
        
        LOGI("Hyprlax Wayfire plugin IPC socket ready");
        return true;
    }
    
public:
    void init() override {
        LOGI("Hyprlax Wayfire plugin initializing");
        
        /* Create views for current workspace grid */
        auto grid = output->workspace->get_workspace_grid_size();
        recreate_views(grid);
        
        /* Connect signals */
        output->connect_signal("workspace-changed", &workspace_changed);
        output->connect_signal("workspace-grid-changed", &workspace_grid_changed);
        output->connect_signal("output-configuration-changed", &output_configuration_changed);
        
        /* Setup IPC for hyprlax communication */
        if (!setup_ipc_socket()) {
            LOGE("Failed to setup IPC, plugin will not function");
        }
        
        LOGI("Hyprlax Wayfire plugin initialized");
    }
    
    void fini() override {
        LOGI("Hyprlax Wayfire plugin shutting down");
        
        /* Cleanup IPC */
        disconnect_client();
        
        if (ipc_source) {
            wl_event_source_remove(ipc_source);
            ipc_source = nullptr;
        }
        
        if (ipc_socket >= 0) {
            close(ipc_socket);
            ipc_socket = -1;
        }
        
        /* Cleanup views */
        for (auto &view : views) {
            view->close();
        }
        views.clear();
        
        LOGI("Hyprlax Wayfire plugin shutdown complete");
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_hyprlax);