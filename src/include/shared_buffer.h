/*
 * shared_buffer.h - Shared memory buffer interface for compositor plugins
 *
 * Provides shared memory buffers for zero-copy frame transfer between
 * hyprlax and compositor plugins.
 */

#ifndef HYPRLAX_SHARED_BUFFER_H
#define HYPRLAX_SHARED_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

/* Shared memory frame header - must match plugin definition */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t format;      /* OpenGL format (GL_RGBA, etc) */
    uint32_t stride;      /* Bytes per row */
    uint64_t timestamp;   /* Frame timestamp */
    uint32_t frame_number;
} shared_buffer_header_t;

/* Shared buffer handle */
typedef struct shared_buffer {
    int fd;                    /* File descriptor for shared memory */
    void *data;               /* Mapped memory */
    size_t size;              /* Total size */
    shared_buffer_header_t *header;  /* Header pointer */
    uint8_t *pixels;          /* Pixel data pointer */
} shared_buffer_t;

/* Create a new shared buffer */
shared_buffer_t* shared_buffer_create(uint32_t width, uint32_t height, uint32_t format);

/* Destroy a shared buffer */
void shared_buffer_destroy(shared_buffer_t *buffer);

/* Update buffer with new frame data */
bool shared_buffer_update(shared_buffer_t *buffer, const void *pixels, 
                         uint32_t width, uint32_t height, uint32_t stride);

/* Get file descriptor for sending to plugin */
int shared_buffer_get_fd(shared_buffer_t *buffer);

/* IPC helpers for plugin communication */
bool shared_buffer_send_fd(int socket, int fd, const char *message);
int shared_buffer_receive_fd(int socket, char *message, size_t message_size);

#endif /* HYPRLAX_SHARED_BUFFER_H */