#ifndef FN_TUI_EX_CMD_H
#define FN_TUI_EX_CMD_H

#include "fnav/tui/buffer.h"

void cmdline_init();
void window_ex_cmd();
void stop_ex_cmd();
void ex_input(BufferNode *bn, int key);

#endif
