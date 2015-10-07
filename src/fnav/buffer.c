#define _GNU_SOURCE
#include <ncurses.h>
#include <limits.h>

#include "fnav/pane.h"
#include "fnav/log.h"
#include "fnav/table.h"
#include "fnav/buffer.h"
#include "fnav/loop.h"
#include "fnav/fs.h"

struct fn_buf {
  WINDOW *nc_win;
  Pane *pane;
  pos_T b_size;
  pos_T nc_size;

  fn_handle *hndl;
  String fname;
  bool dirty;
};

#define SET_POS(pos,x,y)       \
  (pos.col) = (x);             \
  (pos.lnum) = (y);            \

#define INC_POS(pos,x,y)       \
  (pos.col) += (x);            \
  (pos.lnum) += (y);           \

#define DRAW_AT(win,pos,str)   \
  mvwprintw(win, pos.lnum, pos.col, str);

void buf_listen(fn_handle *hndl);

fn_buf* buf_init()
{
  log_msg("INIT", "BUFFER");
  fn_buf *buf = malloc(sizeof(fn_buf));
  SET_POS(buf->b_size,0,0);
  getmaxyx(stdscr, buf->nc_size.lnum, buf->nc_size.col);
  buf->nc_win = newwin(buf->nc_size.lnum, buf->nc_size.col, 0,0);
  buf->dirty = false;
  return buf;
}

void buf_set(fn_handle *hndl, String fname)
{
  log_msg("BUFFER", "set");
  fn_buf *buf = hndl->buf;
  buf->fname = fname;
  buf->hndl = hndl;
  tbl_listener(hndl, buf_listen);
}

void buf_resize(fn_buf *buf, int w, int h)
{
  SET_POS(buf->b_size, w, h);
}

void buf_listen(fn_handle *hndl)
{
  log_msg("BUFFER", "listen cb");
  buf_refresh(hndl->buf);
}

void buf_draw(void **argv)
{
  log_msg("BUFFER", "druh");
  fn_buf *buf = (fn_buf*)argv[0];

  wclear(buf->nc_win);
  log_msg("BUFFER", "__druh");
  pos_T p = {.lnum = 0, .col = 0};
  listener *lis = buf->hndl->lis;
  ventry *it = lis->ent;
  if (!it) return;
  int i;
  for(i = 0; i < lis->pos; i++) {
    if (it->prev->head) break;
    it = it->prev;
    if (!it->rec) break;
  }
  for(int i = 0; i < buf->nc_size.lnum; i++) {
    if (!it->rec) break;
    String n = (String)rec_fld(it->rec, buf->fname);
    if (isdir(it->rec)) {
      wattron(buf->nc_win, COLOR_PAIR(1));
      DRAW_AT(buf->nc_win, p, n);
      wattroff(buf->nc_win, COLOR_PAIR(1));
    }
    else {
      wattron(buf->nc_win, COLOR_PAIR(2));
      DRAW_AT(buf->nc_win, p, n);
      wattroff(buf->nc_win, COLOR_PAIR(2));
    }
    it = it->next;
    if (it->prev->head) break;
    INC_POS(p,0,1);
  }
  log_msg("BUFFER", "+__druh");
  wmove(buf->nc_win, lis->pos, 0);
  wchgat(buf->nc_win, -1, A_REVERSE, 1, NULL);
  //TODO: use wnoutrefresh here and refresh once on timer
  wnoutrefresh(buf->nc_win);
  buf->dirty = false;
}

String buf_val(fn_handle *hndl, String fname) {
  return rec_fld(hndl->lis->ent->rec, fname);
}

fn_rec* buf_rec(fn_handle *hndl) {
  return hndl->lis->ent->rec;
}

int buf_pgsize(fn_handle *hndl) {
  return hndl->buf->nc_size.col / 3;
}

void buf_refresh(fn_buf *buf)
{
  log_msg("BUFFER", "refresh");
  if (buf->dirty)
    return;
  buf->dirty = true;
  pane_req_draw(buf, buf_draw);
}

void buf_mv(fn_buf *buf, int x, int y)
{
  int *pos = &buf->hndl->lis->pos;
  int *ofs = &buf->hndl->lis->ofs;
  for (int i = 0; i < abs(y); i++) {
    ventry *ent = buf->hndl->lis->ent;
    int d = FN_MV(ent, y);

    if (d == 1) {
      if (*pos < buf->nc_size.lnum * .8)
        (*pos)++;
      else
        (*ofs)++;
      buf->hndl->lis->ent = ent->next;
    }
    else if (d == -1) {
      if (*pos > buf->nc_size.lnum * .2)
        (*pos)--;
      else {
        if (*ofs > 0)
          (*ofs)--;
        else
          (*pos)--;
      }
      buf->hndl->lis->ent = ent->prev;
    }
  }
  buf_refresh(buf);
}