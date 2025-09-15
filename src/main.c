/*
 * main.c - Application entry point
 * 
 * Simple entry point that uses the modular hyprlax system.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "include/hyprlax.h"

/* Global context for signal handling */
static hyprlax_context_t *g_ctx = NULL;

/* Signal handler for clean shutdown */
static void signal_handler(int sig) {
    (void)sig;
    if (g_ctx) {
        g_ctx->running = false;
    }
}

int main(int argc, char **argv) {
    /* Create application context */
    hyprlax_context_t *ctx = hyprlax_create();
    if (!ctx) {
        fprintf(stderr, "Failed to create application context\n");
        return 1;
    }
    
    /* Set up signal handlers */
    g_ctx = ctx;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Initialize application */
    int ret = hyprlax_init(ctx, argc, argv);
    if (ret != 0) {
        hyprlax_destroy(ctx);
        return 1;
    }
    
    /* Run main loop */
    ret = hyprlax_run(ctx);
    
    /* Clean up */
    hyprlax_destroy(ctx);
    g_ctx = NULL;
    
    return ret;
}