#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/core.hpp>
#include <wayfire/render.hpp>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-server.h>
#include <drm_fourcc.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <atomic>
#include <mutex>
#include <errno.h>
#include <iostream>
#include <syslog.h>
#include <string>
#include <fstream>

#define HYPRLAX_IPC_MAGIC 0x48595052
#define HYPRLAX_CMD_FRAME 0x01

struct hyprlax_frame_header {
    uint32_t magic;
    uint32_t command;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t size;
    int32_t fd;
};

class hyprlax_bg_node_t : public wf::scene::node_t {
public:
    wlr_texture *texture = nullptr;
    std::mutex tex_mutex;
    bool has_texture = false;
    wf::output_t *output;
    std::ofstream debug_log;
    // Pending frame data to upload on render thread
    std::vector<uint8_t> pending_pixels;
    uint32_t pending_width = 0;
    uint32_t pending_height = 0;
    uint32_t pending_stride = 0;
    bool has_pending = false;
    
    class render_instance_t : public wf::scene::simple_render_instance_t<hyprlax_bg_node_t> {
    public:
        using simple_render_instance_t::simple_render_instance_t;
        
        void render(const wf::scene::render_instruction_t& data) {
            // Create texture from pending frame only once (first frame)
            if (!self->texture && self->has_pending && !self->pending_pixels.empty()) {
                std::lock_guard<std::mutex> lock(self->tex_mutex);
                
                auto renderer = wf::get_core().renderer;
                if (!renderer) return;
                
                // Create texture just once from first frame
                self->texture = wlr_texture_from_pixels(renderer, DRM_FORMAT_ARGB8888,
                                                       self->pending_stride,
                                                       self->pending_width,
                                                       self->pending_height,
                                                       self->pending_pixels.data());
                
                if (self->texture) {
                    self->has_texture = true;
                    fprintf(stderr, "[HYPRLAX] Created texture from first frame\n");
                }
                
                self->pending_pixels.clear();
                self->pending_width = self->pending_height = self->pending_stride = 0;
                self->has_pending = false;
            }
            
            if (self->texture) {
                auto g = self->get_bounding_box();
                wf::texture_t tex{self->texture};
                data.pass->add_texture(tex, data.target, g, data.damage);
            }
            
            return;
            
            // TEST: Fix potential deadlock by releasing mutex before add_texture
            wlr_texture* tex_to_draw = nullptr;
            
            {
                std::lock_guard<std::mutex> lock(self->tex_mutex);
                
                // Upload pending frame on render thread if available
                if (self->has_pending && !self->pending_pixels.empty()) {
                    auto renderer = wf::get_core().renderer;
                    
                    // TEST: Create texture only once, don't destroy/recreate
                    // Comment out destruction to test if that's causing the lock
                    /*
                    if (self->texture) {
                        wlr_texture_destroy(self->texture);
                        self->texture = nullptr;
                    }
                    */
                    
                    // Only create texture if we don't have one
                    if (!self->texture) {
                        self->texture = wlr_texture_from_pixels(renderer, DRM_FORMAT_ARGB8888,
                                                               self->pending_stride,
                                                               self->pending_width,
                                                               self->pending_height,
                                                               self->pending_pixels.data());
                        if (!self->texture) {
                            self->texture = wlr_texture_from_pixels(renderer, DRM_FORMAT_XRGB8888,
                                                                   self->pending_stride,
                                                                   self->pending_width,
                                                                   self->pending_height,
                                                                   self->pending_pixels.data());
                        }
                        if (self->texture) {
                            self->has_texture = true;
                            fprintf(stderr, "[HYPRLAX-TEST] Created texture (will reuse for all frames)\n");
                        }
                    }
                    // For now, just clear pending without updating texture content
                    self->pending_pixels.clear();
                    self->pending_width = self->pending_height = self->pending_stride = 0;
                    self->has_pending = false;
                }
                
                if (self->has_texture && self->texture) {
                    tex_to_draw = self->texture;
                }
            } // Release mutex here
            
            // Draw without holding the mutex
            if (tex_to_draw) {
                auto g = self->get_bounding_box();
                
                // TEST: Only draw if we intersect with damage region
                wf::region_t bbox_region{g};
                bbox_region &= data.damage;
                if (bbox_region.empty()) {
                    return;  // Nothing to draw
                }
                
                wf::texture_t tex{tex_to_draw};
                data.pass->add_texture(tex, data.target, g, data.damage);
            }
            
            /* STILL COMMENTED OUT - drawing code
            if (!self->has_texture || !self->texture) {
                return;
            }

            auto g = self->get_bounding_box();
            wf::texture_t tex{self->texture};
            data.pass->add_texture(tex, data.target, g, data.damage);
            */
        }
    };
    
public:
    hyprlax_bg_node_t(wf::output_t *output) : node_t(false), output(output) {
        // Initialize with empty texture state
        has_texture = false;
        texture = nullptr;
    }
    
    ~hyprlax_bg_node_t() {
        if (texture) {
            wlr_texture_destroy(texture);
        }
    }
    
    void update_texture(void *data, uint32_t width, uint32_t height, uint32_t stride) {
        std::lock_guard<std::mutex> lock(tex_mutex);
        
        // Do not touch wlroots/renderer here: IPC thread only
        
        /* glReadPixels gives us RGBA, convert to ARGB8888 (BGRA in memory) and flip */
        std::vector<uint8_t> converted;
        converted.resize(width * height * 4);
        const uint8_t *src = static_cast<const uint8_t*>(data);
        
        // Convert RGBA -> BGRA and flip vertically
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t *src_row = src + (height - 1 - y) * stride;
            uint8_t *dst_row = converted.data() + y * width * 4;
            for (uint32_t x = 0; x < width; ++x) {
                uint8_t r = src_row[x*4 + 0];
                uint8_t g = src_row[x*4 + 1];
                uint8_t b = src_row[x*4 + 2];
                uint8_t a = src_row[x*4 + 3];
                // ARGB8888 is BGRA in memory (little-endian)
                dst_row[x*4 + 0] = b;
                dst_row[x*4 + 1] = g;
                dst_row[x*4 + 2] = r;
                dst_row[x*4 + 3] = a;
            }
        }

        /* Debug: print sample pixels from source and converted data */
        {
            const uint8_t *src_sample = static_cast<const uint8_t*>(data);
            const uint8_t *dst_sample = converted.data();
            
            // Sample from middle of image
            int mid_y = height / 2;
            int mid_x = width / 2;
            const uint8_t *src_pixel = src_sample + mid_y * stride + mid_x * 4;
            const uint8_t *dst_pixel = dst_sample + mid_y * width * 4 + mid_x * 4;
            
            fprintf(stderr, "[HYPRLAX-DEBUG] Source pixel (RGBA from GL): R=%02x G=%02x B=%02x A=%02x\n", 
                    src_pixel[0], src_pixel[1], src_pixel[2], src_pixel[3]);
            fprintf(stderr, "[HYPRLAX-DEBUG] Converted pixel (BGRA for ARGB8888): B=%02x G=%02x R=%02x A=%02x\n",
                    dst_pixel[0], dst_pixel[1], dst_pixel[2], dst_pixel[3]);
            
            // Check if image is all black
            bool all_black = true;
            for (int i = 0; i < 100 && all_black; i++) {
                int check_y = (i * 13) % height;
                int check_x = (i * 17) % width;
                const uint8_t *check = src_sample + check_y * stride + check_x * 4;
                if (check[0] || check[1] || check[2]) {
                    all_black = false;
                    fprintf(stderr, "[HYPRLAX-DEBUG] Found non-black pixel at (%d,%d): R=%02x G=%02x B=%02x\n",
                            check_x, check_y, check[0], check[1], check[2]);
                }
            }
            if (all_black) {
                fprintf(stderr, "[HYPRLAX-DEBUG] WARNING: Source image appears to be all black!\n");
            }
        }

        // Stage for upload on render thread
        submit_frame(converted.data(), width, height, width * 4);
    }
    
    void set_test_color() {
        std::lock_guard<std::mutex> lock(tex_mutex);
        
        auto renderer = wf::get_core().renderer;
        if (!renderer) {
            return;
        }
        
        if (texture) {
            wlr_texture_destroy(texture);
        }
        
        /* Create a bright red test pattern using ARGB8888 like Cairo */
        int width = 1920, height = 1080;
        std::vector<uint8_t> pixels;
        pixels.resize(width * height * 4);
        for (int y = 0; y < height; ++y) {
            uint8_t *row = pixels.data() + y * width * 4;
            for (int x = 0; x < width; ++x) {
                // ARGB8888 in memory is B,G,R,A little-endian
                row[x*4 + 0] = 0x00; /* B */
                row[x*4 + 1] = 0x00; /* G */
                row[x*4 + 2] = 0xFF; /* R */
                row[x*4 + 3] = 0xFF; /* A */
            }
        }
        
        fprintf(stderr, "[HYPRLAX-DEBUG] Creating test texture: bright red %dx%d\n", width, height);
        texture = wlr_texture_from_pixels(renderer, DRM_FORMAT_ARGB8888,
                                         width * 4, width, height, pixels.data());
        
        if (texture) {
            fprintf(stderr, "[HYPRLAX-DEBUG] Test texture created successfully\n");
            has_texture = true;
            auto bbox = get_bounding_box();
            wf::scene::damage_node(shared_from_this(), bbox);
            if (output) {
                output->render->schedule_redraw();
                fprintf(stderr, "[HYPRLAX-DEBUG] Scheduled test redraw\n");
            }
        } else {
            fprintf(stderr, "[HYPRLAX-DEBUG] ERROR: Failed to create test texture\n");
        }
    }

    // Stage a frame from IPC thread for upload on render thread
    void submit_frame(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride) {
        {
            std::lock_guard<std::mutex> lock(tex_mutex);
            // Drop frame if one is already pending to avoid flooding and stalls
            if (has_pending) {
                return;
            }
            pending_pixels.assign(pixels, pixels + height * stride);
            pending_width = width;
            pending_height = height;
            pending_stride = stride;
            has_pending = true;
        }
        // Schedule redraw via Wayland main loop to avoid cross-thread calls
        auto loop = wf::get_core().ev_loop;
        wl_event_loop_add_idle(loop, [](void *data){
            auto self = static_cast<hyprlax_bg_node_t*>(data);
            if (self->output) {
                self->output->render->schedule_redraw();
            }
        }, this);
    }

    wf::geometry_t get_bounding_box() override {
        if (!output) {
            return {0, 0, 1920, 1080};
        }
        // Background should cover the entire output starting at (0,0)
        auto size = output->get_screen_size();
        return {0, 0, size.width, size.height};
    }
    
    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
                              wf::scene::damage_callback push_damage, 
                              wf::output_t *output) override {
        // Re-enable render instances
        instances.push_back(std::make_unique<render_instance_t>(this, push_damage, output));
    }
};

class wayfire_hyprlax : public wf::per_output_plugin_instance_t {
private:
    std::shared_ptr<hyprlax_bg_node_t> bg_node;
    int ipc_socket = -1;
    std::thread ipc_thread;
    std::atomic<bool> running;
    std::ofstream plugin_log;
    
    /* Internal shared buffer header must match hyprlax/src/include/shared_buffer.h */
    struct shared_buffer_header {
        uint32_t width;
        uint32_t height;
        uint32_t format;      /* GL_* format from hyprlax */
        uint32_t stride;      /* Bytes per row */
        uint64_t timestamp;   /* Frame timestamp */
        uint32_t frame_number;
    };

    /* No longer needed: we upload via WL_SHM_FORMAT_* after converting */

    bool handle_frame(int client_fd) {
        hyprlax_frame_header header;
        
        // avoid Wayfire logger from non-main thread
        // fprintf(stderr, "[HYPRLAX-DEBUG] Waiting for frame fd=%d\n", client_fd);
        if (plugin_log.is_open()) plugin_log << "Waiting for frame on fd " << client_fd << "\n";
        
        struct msghdr msg = {0};
        struct iovec iov = {&header, sizeof(header)};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        
        char control[CMSG_SPACE(sizeof(int))];
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        
        ssize_t n = recvmsg(client_fd, &msg, 0);  /* Block waiting for data */
        // fprintf(stderr, "[HYPRLAX-DEBUG] recvmsg=%zd\n", n);
        if (plugin_log.is_open()) plugin_log << "recvmsg returned " << n << " bytes\n";
        
        if (n != (ssize_t)sizeof(header)) {
            if (n == 0 || (n == -1 && errno == ECONNRESET)) {
                // fprintf(stderr, "[HYPRLAX-DEBUG] Client disconnected\n");
            } else if (n == -1) {
                fprintf(stderr, "[HYPRLAX-DEBUG] recv header failed: %s\n", strerror(errno));
            } else {
                fprintf(stderr, "[HYPRLAX-DEBUG] incomplete header: %zd/%zu\n", n, sizeof(header));
            }
            if (plugin_log.is_open()) plugin_log << "Header receive failed, closing connection\n";
            return false;
        }
        
        // fprintf(stderr, "[HYPRLAX-DEBUG] header: magic=0x%08x cmd=0x%02x %dx%d fmt=%u size=%u\n",
        //         header.magic, header.command, header.width, header.height, header.format, header.size);
        if (plugin_log.is_open()) plugin_log << "Header: w=" << header.width << ", h=" << header.height
                                             << ", fmt=" << header.format << ", size=" << header.size << "\n";
        
        if (header.magic != HYPRLAX_IPC_MAGIC || header.command != HYPRLAX_CMD_FRAME) {
            fprintf(stderr, "[HYPRLAX-DEBUG] invalid header magic/cmd\n");
            if (plugin_log.is_open()) plugin_log << "Invalid header magic/cmd, closing\n";
            return false;  /* Invalid frame */
        }
        
        int fd = -1;
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        if (cmsg && cmsg->cmsg_level == SOL_SOCKET && 
            cmsg->cmsg_type == SCM_RIGHTS && cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
            fd = *((int *)CMSG_DATA(cmsg));
        }
        
        if (fd == -1) {
            fprintf(stderr, "[HYPRLAX-DEBUG] no fd in message\n");
            return false;  /* No file descriptor */
        }
        // fprintf(stderr, "[HYPRLAX-DEBUG] map fd=%d size=%u\n", fd, header.size);
        if (plugin_log.is_open()) plugin_log << "Mapping received fd " << fd << " size=" << header.size << "\n";

        void *data = mmap(nullptr, header.size, PROT_READ, MAP_SHARED, fd, 0);
        if (data == MAP_FAILED) {
            fprintf(stderr, "[HYPRLAX-DEBUG] mmap failed: %s\n", strerror(errno));
            if (plugin_log.is_open()) plugin_log << "mmap failed: " << strerror(errno) << "\n";
            close(fd);
            return false;  /* Failed to map memory */
        }

        /* Skip the shared buffer header at the beginning of the mapped region */
        auto *shdr = reinterpret_cast<shared_buffer_header*>(data);
        uint32_t w = shdr->width ? shdr->width : header.width;
        uint32_t h = shdr->height ? shdr->height : header.height;
        uint32_t stride = shdr->stride ? shdr->stride : w * 4;

        void *pixels = reinterpret_cast<uint8_t*>(data) + sizeof(shared_buffer_header);

        // fprintf(stderr, "[HYPRLAX-DEBUG] upload %ux%u stride=%u\n", w, h, stride);
        if (plugin_log.is_open()) plugin_log << "Updating texture: " << w << "x" << h
                                             << ", stride=" << stride << ", upload=DRM_FORMAT_XRGB8888(flipped)\n";

        // TEST: Re-enable update_texture but keep render disabled
        bg_node->update_texture(pixels, w, h, stride);
        fprintf(stderr, "[HYPRLAX-TEST] Frame received %ux%u, called update_texture\n", w, h);

        munmap(data, header.size);
        close(fd);
        // fprintf(stderr, "[HYPRLAX-DEBUG] frame processed\n");
        if (plugin_log.is_open()) { plugin_log << "Frame processed successfully\n"; plugin_log.flush(); }
        return true;  /* Frame handled successfully */
    }
    
    void ipc_worker() {
        fprintf(stderr, "[HYPRLAX-PLUGIN] IPC worker thread started, socket fd=%d\n", ipc_socket);
        
        while (running) {
            struct sockaddr_un addr;
            socklen_t len = sizeof(addr);
            
            // fprintf(stderr, "[HYPRLAX-PLUGIN] accept() on fd %d\n", ipc_socket);
            int client_fd = accept(ipc_socket, (struct sockaddr *)&addr, &len);
            fprintf(stderr, "[HYPRLAX-PLUGIN] accept() returned %d\n", client_fd);
            
            if (client_fd == -1) {
                if (errno == EINTR) continue;
                fprintf(stderr, "[HYPRLAX-DEBUG] accept failed: %s\n", strerror(errno));
                break;
            }
            
            fprintf(stderr, "[HYPRLAX-DEBUG] Client connected fd=%d\n", client_fd);
            if (plugin_log.is_open()) plugin_log << "Client connected fd=" << client_fd << "\n";

            int frame_count = 0;
            while (running) {
                if (!handle_frame(client_fd)) {
                    // fprintf(stderr, "[HYPRLAX-DEBUG] handled %d frames\n", frame_count);
                    if (plugin_log.is_open()) plugin_log << "Client closed after " << frame_count << " frames\n";
                    break;
                }
                frame_count++;
                if (frame_count % 300 == 0) {
                    if (plugin_log.is_open()) plugin_log << "Received " << frame_count << " frames\n";
                }
            }
            
            close(client_fd);
        }
    }
    
    bool setup_ipc_socket() {
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (!runtime_dir) {
            fprintf(stderr, "[HYPRLAX-DEBUG] XDG_RUNTIME_DIR not set\n");
            if (plugin_log.is_open()) plugin_log << "XDG_RUNTIME_DIR not set\n";
            return false;
        }
        
        char socket_path[256];
        snprintf(socket_path, sizeof(socket_path), "%s/hyprlax-wayfire.sock", runtime_dir);
        
        unlink(socket_path);
        if (plugin_log.is_open()) plugin_log << "Unlinked old socket (if any): " << socket_path << "\n";
        
        ipc_socket = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (ipc_socket == -1) {
            fprintf(stderr, "[HYPRLAX-DEBUG] socket() failed: %s\n", strerror(errno));
            if (plugin_log.is_open()) plugin_log << "socket() failed: " << strerror(errno) << "\n";
            return false;
        }
        if (plugin_log.is_open()) plugin_log << "socket fd=" << ipc_socket << " created\n";
        
        struct sockaddr_un addr = {0};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
        
        if (bind(ipc_socket, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            fprintf(stderr, "[HYPRLAX-DEBUG] bind() failed: %s\n", strerror(errno));
            if (plugin_log.is_open()) plugin_log << "bind() failed: " << strerror(errno) << "\n";
            close(ipc_socket);
            ipc_socket = -1;
            return false;
        }
        if (plugin_log.is_open()) plugin_log << "bind() ok at " << socket_path << "\n";
        
        if (listen(ipc_socket, 1) == -1) {
            fprintf(stderr, "[HYPRLAX-DEBUG] listen() failed: %s\n", strerror(errno));
            if (plugin_log.is_open()) plugin_log << "listen() failed: " << strerror(errno) << "\n";
            close(ipc_socket);
            ipc_socket = -1;
            unlink(socket_path);
            return false;
        }
        if (plugin_log.is_open()) plugin_log << "listen() ok on fd=" << ipc_socket << "\n";
        fprintf(stderr, "[HYPRLAX-DEBUG] listening on %s\n", socket_path);
        return true;
    }
    
public:
    void init() override {
        openlog("hyprlax-plugin", LOG_PID | LOG_CONS, LOG_USER);
        syslog(LOG_INFO, "============ Plugin init() called ============");
        
        fprintf(stderr, "[HYPRLAX-DEBUG] plugin init\n");
        /* Open a log file we can inspect easily */
        plugin_log.open("/tmp/hyprlax-wayfire.log", std::ios::app);
        if (plugin_log.is_open()) plugin_log << "==== init() ====\n";
        
        if (!setup_ipc_socket()) {
            syslog(LOG_ERR, "Failed to setup IPC socket");
            fprintf(stderr, "[HYPRLAX-DEBUG] setup_ipc_socket failed\n");
            if (plugin_log.is_open()) plugin_log << "Failed to setup IPC socket" << std::endl;
            return;
        }
        
        syslog(LOG_INFO, "IPC socket setup complete, fd=%d", ipc_socket);
        fprintf(stderr, "[HYPRLAX-DEBUG] ipc socket fd=%d\n", ipc_socket);
        if (plugin_log.is_open()) plugin_log << "IPC socket fd=" << ipc_socket << std::endl;
        
        // Create node for this output
        bg_node = std::make_shared<hyprlax_bg_node_t>(output);
        
        // Get the background layer and add our node to it
        auto bg_layer = output->node_for_layer(wf::scene::layer::BACKGROUND);
        wf::scene::add_back(bg_layer, bg_node);
        
        fprintf(stderr, "[HYPRLAX-DEBUG] Attached to BACKGROUND layer using add_child\n");
        
        // No test color - use actual frames
        // bg_node->set_test_color();
        
        running = true;
        ipc_thread = std::thread(&wayfire_hyprlax::ipc_worker, this);
        
        syslog(LOG_INFO, "IPC thread started");
        fprintf(stderr, "[HYPRLAX-DEBUG] ipc thread started\n");
        syslog(LOG_INFO, "Plugin initialized for output");
        if (plugin_log.is_open()) { plugin_log << "IPC thread started\n"; plugin_log.flush(); }
    }
    
    void fini() override {
        // Log cleanup steps to detect where it locks
        char log_path[256];
        snprintf(log_path, sizeof(log_path), "%s/hyprlax-fini-crash.log", getenv("HOME"));
        FILE* fini_log = fopen(log_path, "a");
        if (fini_log) {
            fprintf(fini_log, "[%ld] fini() called\n", time(nullptr));
            fflush(fini_log);
        }
        
        running = false;
        
        if (fini_log) { fprintf(fini_log, "[%ld] Set running=false\n", time(nullptr)); fflush(fini_log); }
        
        // First remove from scene graph before destroying resources
        if (bg_node) {
            if (fini_log) { fprintf(fini_log, "[%ld] Removing bg_node from scene\n", time(nullptr)); fflush(fini_log); }
            wf::scene::remove_child(bg_node);
            
            if (fini_log) { fprintf(fini_log, "[%ld] Resetting bg_node\n", time(nullptr)); fflush(fini_log); }
            bg_node.reset();
            if (fini_log) { fprintf(fini_log, "[%ld] bg_node reset complete\n", time(nullptr)); fflush(fini_log); }
        }
        
        if (ipc_socket != -1) {
            if (fini_log) { fprintf(fini_log, "[%ld] Shutting down IPC socket\n", time(nullptr)); fflush(fini_log); }
            
            // Wake up the accept() call by connecting to ourselves
            int wake_fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (wake_fd >= 0) {
                struct sockaddr_un addr = {};
                addr.sun_family = AF_UNIX;
                snprintf(addr.sun_path, sizeof(addr.sun_path), "/run/user/%d/hyprlax-wayfire.sock", getuid());
                connect(wake_fd, (struct sockaddr*)&addr, sizeof(addr));
                close(wake_fd);
                if (fini_log) { fprintf(fini_log, "[%ld] Sent wake signal to accept()\n", time(nullptr)); fflush(fini_log); }
            }
            
            shutdown(ipc_socket, SHUT_RDWR);
            close(ipc_socket);
            ipc_socket = -1;
            
            // Remove the socket file
            char socket_path[256];
            snprintf(socket_path, sizeof(socket_path), "/run/user/%d/hyprlax-wayfire.sock", getuid());
            unlink(socket_path);
            if (fini_log) { fprintf(fini_log, "[%ld] Removed socket file\n", time(nullptr)); fflush(fini_log); }
        }
        
        if (ipc_thread.joinable()) {
            if (fini_log) { fprintf(fini_log, "[%ld] Joining IPC thread\n", time(nullptr)); fflush(fini_log); }
            ipc_thread.join();
            if (fini_log) { fprintf(fini_log, "[%ld] IPC thread joined\n", time(nullptr)); fflush(fini_log); }
        }
        
        if (fini_log) { 
            fprintf(fini_log, "[%ld] fini() completed successfully\n", time(nullptr)); 
            fclose(fini_log);
        }
        
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (runtime_dir) {
            char socket_path[256];
            snprintf(socket_path, sizeof(socket_path), "%s/hyprlax-wayfire.sock", runtime_dir);
            unlink(socket_path);
        }
        
        fprintf(stderr, "[HYPRLAX-DEBUG] plugin fini\n");
        if (plugin_log.is_open()) { plugin_log << "fini()" << std::endl; plugin_log.close(); }
    }
};

DECLARE_WAYFIRE_PLUGIN((wf::per_output_plugin_t<wayfire_hyprlax>));
