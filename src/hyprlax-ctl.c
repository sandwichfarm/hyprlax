/*
 * hyprlax-ctl - Deprecated wrapper
 *
 * hyprlax-ctl is deprecated. Use `hyprlax ctl ...` instead.
 * This wrapper forwards all arguments to `hyprlax ctl`.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    fprintf(stderr, "[DEPRECATED] hyprlax-ctl is deprecated; use 'hyprlax ctl ...'\n");
    /* Build new argv: hyprlax ctl <original args...> */
    int new_argc = argc + 1; /* inserting 'ctl' after program */
    char **new_argv = (char**)calloc((size_t)new_argc + 1, sizeof(char*));
    if (!new_argv) {
        perror("alloc");
        return 1;
    }
    new_argv[0] = (char*)"hyprlax";
    new_argv[1] = (char*)"ctl";
    for (int i = 1; i < argc; i++) {
        new_argv[i + 1] = argv[i];
    }
    new_argv[new_argc] = NULL;
    execvp(new_argv[0], new_argv);
    perror("execvp hyprlax ctl");
    return 127;
}
