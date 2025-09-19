/*
 * shared_buffer.c - Shared memory buffer implementation
 */

#define _GNU_SOURCE  /* For memfd_create */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/memfd.h>
#include "../include/shared_buffer.h"
#include "../include/log.h"

/* Create a new shared buffer */
shared_buffer_t* shared_buffer_create(uint32_t width, uint32_t height, uint32_t format) {
    shared_buffer_t *buffer = calloc(1, sizeof(shared_buffer_t));
    if (!buffer) {
        LOG_ERROR("Failed to allocate shared buffer");
        return NULL;
    }
    
    /* Calculate buffer size */
    uint32_t stride = width * 4;  /* Assuming RGBA for now */
    size_t pixel_size = stride * height;
    buffer->size = sizeof(shared_buffer_header_t) + pixel_size;
    
    /* Create shared memory */
    buffer->fd = memfd_create("hyprlax-frame", MFD_CLOEXEC);
    if (buffer->fd < 0) {
        LOG_ERROR("Failed to create shared memory: %s", strerror(errno));
        free(buffer);
        return NULL;
    }
    
    /* Set size */
    if (ftruncate(buffer->fd, buffer->size) < 0) {
        LOG_ERROR("Failed to set shared memory size: %s", strerror(errno));
        close(buffer->fd);
        free(buffer);
        return NULL;
    }
    
    /* Map memory */
    buffer->data = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE, 
                       MAP_SHARED, buffer->fd, 0);
    if (buffer->data == MAP_FAILED) {
        LOG_ERROR("Failed to map shared memory: %s", strerror(errno));
        close(buffer->fd);
        free(buffer);
        return NULL;
    }
    
    /* Setup pointers */
    buffer->header = (shared_buffer_header_t*)buffer->data;
    buffer->pixels = (uint8_t*)buffer->data + sizeof(shared_buffer_header_t);
    
    /* Initialize header */
    buffer->header->width = width;
    buffer->header->height = height;
    buffer->header->format = format;
    buffer->header->stride = stride;
    buffer->header->timestamp = 0;
    buffer->header->frame_number = 0;
    
    /* Noisy during rendering: make this TRACE-level */
    LOG_TRACE("Created shared buffer %dx%d, fd=%d", width, height, buffer->fd);
    return buffer;
}

/* Destroy a shared buffer */
void shared_buffer_destroy(shared_buffer_t *buffer) {
    if (!buffer) return;
    
    if (buffer->data && buffer->data != MAP_FAILED) {
        munmap(buffer->data, buffer->size);
    }
    
    if (buffer->fd >= 0) {
        close(buffer->fd);
    }
    
    free(buffer);
}

/* Update buffer with new frame data */
bool shared_buffer_update(shared_buffer_t *buffer, const void *pixels,
                         uint32_t width, uint32_t height, uint32_t stride) {
    if (!buffer || !pixels) return false;
    
    /* Check if dimensions match */
    if (width != buffer->header->width || height != buffer->header->height) {
        LOG_WARN("Buffer size mismatch: %dx%d vs %dx%d", 
                width, height, buffer->header->width, buffer->header->height);
        return false;
    }
    
    /* Update header */
    buffer->header->stride = stride;
    buffer->header->frame_number++;
    
    /* Copy pixel data */
    if (stride == buffer->header->stride) {
        /* Simple case: same stride */
        memcpy(buffer->pixels, pixels, stride * height);
    } else {
        /* Different stride: copy row by row */
        const uint8_t *src = pixels;
        uint8_t *dst = buffer->pixels;
        uint32_t copy_width = (width * 4 < stride) ? width * 4 : stride;
        
        for (uint32_t y = 0; y < height; y++) {
            memcpy(dst, src, copy_width);
            src += stride;
            dst += buffer->header->stride;
        }
    }
    
    return true;
}

/* Get file descriptor for sending to plugin */
int shared_buffer_get_fd(shared_buffer_t *buffer) {
    return buffer ? buffer->fd : -1;
}

/* Send file descriptor over Unix socket */
bool shared_buffer_send_fd(int socket, int fd, const char *message) {
    struct iovec iov;
    struct msghdr msg;
    union {
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(int))];
    } control_un;
    struct cmsghdr *cmsg;
    
    /* Setup message */
    iov.iov_base = (void*)message;
    iov.iov_len = strlen(message) + 1;
    
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    /* Setup control message for fd passing */
    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);
    
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    
    *((int*)CMSG_DATA(cmsg)) = fd;
    
    /* Send message */
    if (sendmsg(socket, &msg, 0) < 0) {
        LOG_ERROR("Failed to send fd: %s", strerror(errno));
        return false;
    }
    
    return true;
}

/* Receive file descriptor over Unix socket */
int shared_buffer_receive_fd(int socket, char *message, size_t message_size) {
    struct iovec iov;
    struct msghdr msg;
    union {
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(int))];
    } control_un;
    struct cmsghdr *cmsg;
    
    /* Setup message */
    iov.iov_base = message;
    iov.iov_len = message_size;
    
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);
    
    /* Receive message */
    ssize_t n = recvmsg(socket, &msg, 0);
    if (n <= 0) {
        LOG_ERROR("Failed to receive fd: %s", strerror(errno));
        return -1;
    }
    
    /* Extract file descriptor */
    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && 
        cmsg->cmsg_type == SCM_RIGHTS) {
        return *((int*)CMSG_DATA(cmsg));
    }
    
    return -1;
}
