// model
// stream and table callbacks should be directed here for
// managing data structures read by attached buffers.
#include <malloc.h>

#include "fnav/lib/utarray.h"

#include "fnav/model.h"
#include "fnav/log.h"
#include "fnav/table.h"
#include "fnav/tui/buffer.h"
#include "fnav/tui/sbuffer.h"

static void refind_line(fn_lis *lis);
static void generate_lines(Model *m);

struct fn_line {
  fn_rec *rec;
};

struct Model {
  fn_handle *hndl;
  fn_lis *lis;
  fn_rec *cur;
  ventry *head;
  bool dirty;
  UT_array *lines;
};

UT_icd intpair_icd = {sizeof(fn_line), NULL };

void model_init(fn_handle *hndl)
{
  Model *model = malloc(sizeof(Model));
  model->hndl = hndl;
  hndl->model = model;
  utarray_new(model->lines, &intpair_icd);
}

void model_open(fn_handle *hndl)
{
  tbl_add_lis(hndl->tn, hndl->key_fld, hndl->key);
}

void model_close(fn_handle *hndl)
{
  Model *m = hndl->model;
  Buffer *b = hndl->buf;
  lis_save(m->lis, buf_top(b), buf_line(b));
  utarray_clear(m->lines);
}

void model_read_entry(Model *m, fn_lis *lis, ventry *head)
{
  log_msg("MODEL", "model_read_entry");
  fn_handle *h = m->hndl;
  if (!lis->ent) {
    lis->ent = lis_set_val(lis, h->fname);
  }

  m->head = head;
  m->cur = lis->ent->next->rec;
  h->model->lis = lis;
  generate_lines(m);
  refind_line(m->lis);
  buf_full_invalidate(h->buf, m->lis->index, m->lis->lnum);
}

size_t model_read_stream(Model *m, char *output, size_t remaining,
    bool to_buffer, bool eof)
{
  if (!output) {
    return 0;
  }
  sbuf_write(m->hndl->buf, output, remaining);
  // write to sbuffer
  return (size_t)(output);
}

static void generate_lines(Model *m)
{
  /* generate hash set of index,line. */
  ventry *it = m->head->next;
  for (int i = 0; i < tbl_ent_count(m->head); ++i) {
    fn_line ln;
    ln.rec = it->rec;
    utarray_push_back(m->lines, &ln);
    it = it->next;
  }
}

String model_str_line(Model *m, int index)
{
  fn_line *res = (fn_line*)utarray_eltptr(m->lines, index);
  return res ? rec_fld(res->rec, m->hndl->fname) : NULL;
}

int model_count(Model *m)
{
  return utarray_len(m->lines);
}

void* model_curs_value(Model *m, String field)
{
  return rec_fld(m->cur, field);
}

void model_set_curs(Model *m, int index)
{
  fn_line *res = (fn_line*)utarray_eltptr(m->lines, index);
  m->cur = res->rec;
}

static void refind_line(fn_lis *lis)
{
  log_msg("MODEL", "rewind");
  int count;
  for(count = lis->lnum; count > 0; --count) {
    if (lis->ent->head) break;
    lis->ent = lis->ent->prev;
  }
  lis->lnum = count;
}
