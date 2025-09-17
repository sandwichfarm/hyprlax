/*
 * hyprlax_ctl.c - Control interface for hyprlax daemon
 * 
 * Provides runtime control of hyprlax via IPC commands
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <pwd.h>
#include "ipc.h"

/* Note: socket operations are blocking for simplicity; timeout not used */

/* Connect to hyprlax daemon socket */
static int connect_to_daemon(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return -1;
    }
    
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    
    /* Try user-specific socket first */
    const char *username = getenv("USER");
    if (!username) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) username = pw->pw_name;
    }
    
    if (username) {
        snprintf(addr.sun_path, sizeof(addr.sun_path), "%s%s.sock", 
                 IPC_SOCKET_PATH_PREFIX, username);
    } else {
        snprintf(addr.sun_path, sizeof(addr.sun_path), "%s%d.sock", 
                 IPC_SOCKET_PATH_PREFIX, getuid());
    }
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to connect to hyprlax daemon at %s\n", addr.sun_path);
        fprintf(stderr, "Is hyprlax running?\n");
        close(sock);
        return -1;
    }
    
    return sock;
}

/* Send command and receive response */
static int send_command(int sock, const char *command) {
    if (send(sock, command, strlen(command), 0) < 0) {
        fprintf(stderr, "Failed to send command: %s\n", strerror(errno));
        return -1;
    }
    
    char response[IPC_MAX_MESSAGE_SIZE];
    ssize_t n = recv(sock, response, sizeof(response) - 1, 0);
    if (n < 0) {
        fprintf(stderr, "Failed to receive response: %s\n", strerror(errno));
        return -1;
    }
    
    response[n] = '\0';
    printf("%s", response);
    
    /* Check for success/error in response */
    if (strstr(response, "Error:") || strstr(response, "error:")) {
        return 1;
    }
    
    return 0;
}

/* Print help for ctl commands */
static void print_ctl_help(const char *prog) {
    printf("Usage: %s ctl <command> [arguments]\n\n", prog);
    printf("Layer Management Commands:\n");
    printf("  add <image> [shift] [opacity] [blur]\n");
    printf("      Add a new layer with the specified image\n");
    printf("      shift: parallax shift multiplier (default: 1.0)\n");
    printf("      opacity: layer opacity 0-1 (default: 1.0)\n");
    printf("      blur: blur amount (default: 0.0)\n\n");
    
    printf("  remove <id>\n");
    printf("      Remove layer with the specified ID\n\n");
    
    printf("  modify <id> <property> <value>\n");
    printf("      Modify a layer property\n");
    printf("      Properties: scale, opacity, x, y, z, visible\n\n");
    
    printf("  list\n");
    printf("      List all layers and their properties\n\n");
    
    printf("  clear\n");
    printf("      Remove all layers\n\n");
    
    printf("Runtime Settings Commands:\n");
    printf("  set <property> <value>\n");
    printf("      Set a runtime property\n");
    printf("      Properties: fps, shift, duration, easing,\n");
    printf("                 blur_passes, blur_size, debug\n\n");
    
    printf("  get <property>\n");
    printf("      Get current value of a property\n\n");
    
    printf("System Commands:\n");
    printf("  reload\n");
    printf("      Reload configuration file\n\n");
    
    printf("  status\n");
    printf("      Show daemon status and statistics\n\n");
    
    printf("Examples:\n");
    printf("  %s ctl add /path/to/wallpaper.jpg 1.5 0.9 10\n", prog);
    printf("  %s ctl remove 2\n", prog);
    printf("  %s ctl modify 1 opacity 0.5\n", prog);
    printf("  %s ctl set fps 120\n", prog);
    printf("  %s ctl set easing elastic\n", prog);
    printf("  %s ctl get fps\n", prog);
    printf("  %s ctl status\n", prog);
}

/* Main entry point for ctl subcommand */
int hyprlax_ctl_main(int argc, char **argv) {
    if (argc < 2) {
        print_ctl_help(argv[0]);
        return 1;
    }
    
    /* Handle help */
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_ctl_help("hyprlax");
        return 0;
    }
    
    /* Connect to daemon */
    int sock = connect_to_daemon();
    if (sock < 0) {
        return 1;
    }
    
    /* Build command string */
    char command[IPC_MAX_MESSAGE_SIZE];
    int offset = 0;
    
    for (int i = 1; i < argc; i++) {
        int len = strlen(argv[i]);
        if (offset + len + 2 >= IPC_MAX_MESSAGE_SIZE) {
            fprintf(stderr, "Command too long\n");
            close(sock);
            return 1;
        }
        
        if (offset > 0) {
            command[offset++] = ' ';
        }
        memcpy(command + offset, argv[i], len);
        offset += len;
    }
    command[offset] = '\n';
    command[offset + 1] = '\0';
    
    /* Send command and get response */
    int ret = send_command(sock, command);
    
    close(sock);
    return ret;
}
