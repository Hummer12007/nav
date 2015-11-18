#ifndef FN_TUI_LAYOUT_H
#define FN_TUI_LAYOUT_H

#include "fnav/tui/buffer.h"

typedef struct Container Container;
typedef struct {
  Container *c;
} Layout;

void layout_init(Layout *layout);
void layout_add_buffer(Layout *layout, Buffer *next, enum move_dir dir);
void layout_remove_buffer(Layout *layout);

void layout_movement(Layout *layout, Layout *root, enum move_dir dir);
Buffer* layout_buf(Layout *layout);

void layout_set_status(Layout *layout, String label);
void layout_refresh(Layout *layout);

pos_T layout_size();

#endif
