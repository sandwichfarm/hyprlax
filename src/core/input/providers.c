#include "core/input/input_manager.h"
#include "include/log.h"

const input_provider_ops_t* input_workspace_provider_ops(void);
const input_provider_ops_t* input_cursor_provider_ops(void);
const input_provider_ops_t* input_window_provider_ops(void);

void input_register_builtin_providers(void) {
    const input_provider_ops_t *workspace = input_workspace_provider_ops();
    if (workspace) {
        input_register_provider(workspace, INPUT_WORKSPACE);
    } else {
        LOG_WARN("input_manager: workspace provider unavailable");
    }

    const input_provider_ops_t *cursor = input_cursor_provider_ops();
    if (cursor) {
        input_register_provider(cursor, INPUT_CURSOR);
    } else {
        LOG_WARN("input_manager: cursor provider unavailable");
    }

    const input_provider_ops_t *window = input_window_provider_ops();
    if (window) {
        input_register_provider(window, INPUT_WINDOW);
    } else {
        LOG_WARN("input_manager: window provider unavailable");
    }
}
