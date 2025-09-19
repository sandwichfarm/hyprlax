/* Simple test client for hyprlax-wayfire plugin */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

int main() {
    int sock;
    struct sockaddr_un addr;
    char buffer[256];
    
    // Create socket
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    
    // Connect to plugin
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir) {
        fprintf(stderr, "XDG_RUNTIME_DIR not set\n");
        return 1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path),
            "%s/hyprlax-wayfire.sock", runtime_dir);
    
    printf("Connecting to %s\n", addr.sun_path);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        printf("Is the Wayfire plugin loaded?\n");
        return 1;
    }
    
    printf("Connected!\n");
    
    // Send test message
    const char *msg = "Hello from test client";
    if (send(sock, msg, strlen(msg), 0) < 0) {
        perror("send");
        return 1;
    }
    
    printf("Sent: %s\n", msg);
    
    // Receive response
    ssize_t n = recv(sock, buffer, sizeof(buffer)-1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        printf("Received: %s\n", buffer);
    }
    
    close(sock);
    return 0;
}