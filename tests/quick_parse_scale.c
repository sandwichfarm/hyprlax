#include <stdio.h>
#include <stdlib.h>
#include "include/config_toml.h"
#include "include/core.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s file.toml\n", argv[0]);
        return 2;
    }
    config_t cfg; 
    config_set_defaults(&cfg);
    int rc = config_load_toml(&cfg, argv[1]);
    if (rc != 0) {
        fprintf(stderr, "load rc=%d\n", rc);
        return 1;
    }
    printf("scale_factor=%.3f\n", cfg.scale_factor);
    printf("shift_percent=%.4f\n", cfg.shift_percent);
    printf("shift_pixels=%.1f\n", cfg.shift_pixels);
    return 0;
}

