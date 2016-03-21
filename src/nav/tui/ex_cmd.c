#include "nav/tui/ex_cmd.h"
#include "nav/nav.h"
#include "nav/cmdline.h"
#include "nav/regex.h"
#include "nav/ascii.h"
#include "nav/log.h"
#include "nav/tui/layout.h"
#include "nav/tui/window.h"
#include "nav/tui/history.h"
#include "nav/tui/menu.h"
#include "nav/option.h"
#include "nav/event/input.h"
#include "nav/util.h"

static void cmdline_draw();
static void ex_esc();
static void ex_car();
static void ex_tab();
static void ex_spc();
static void ex_bckspc();
static void ex_killword();
static void ex_killline();
static void ex_hist();
static void ex_cmdinvert();
static void ex_menuhints();

#define STACK_MIN 10
#define CURS_MIN -1
#define EXCMD_HIST() ((ex_state) ? (hist_cmds) : (hist_regs))

static fn_key key_defaults[] = {
  {ESC,      ex_esc,           0,       0},
  {TAB,      ex_tab,           0,       FORWARD},
  {K_S_TAB,  ex_tab,           0,       BACKWARD},
  {' ',      ex_spc,           0,       0},
  {CAR,      ex_car,           0,       0},
  {BS,       ex_bckspc,        0,       0},
  {Ctrl_P,   ex_hist,          0,       BACKWARD},
  {Ctrl_N,   ex_hist,          0,       FORWARD},
  {Ctrl_W,   ex_killword,      0,       0},
  {Ctrl_U,   ex_killline,      0,       0},
  {Ctrl_L,   ex_cmdinvert,     0,       0},
  {Ctrl_G,   ex_menuhints,     0,       0},
};
static fn_keytbl key_tbl;
static short cmd_idx[LENGTH(key_defaults)];

static cmd_part **cmd_stack;
static int cur_part;
static int max_part;

static WINDOW *nc_win;
static int curpos;
static int maxpos;
static Cmdline cmd;
static char *line;
static char *fmt_out;
static Menu *menu;
static fn_hist *hist_cmds;
static fn_hist *hist_regs;
static LineMatch *lm;

static char state_symbol;
static int ex_state;
static int col_text;
static int mflag;

void ex_cmd_init()
{
  key_tbl.tbl = key_defaults;
  key_tbl.cmd_idx = cmd_idx;
  key_tbl.maxsize = LENGTH(key_defaults);
  input_setup_tbl(&key_tbl);
  hist_cmds = hist_new();
  hist_regs = hist_new();
  menu = menu_new();
}

void ex_cmd_cleanup()
{
  log_msg("CLEANUP", "ex_cmd_cleanup");
  hist_delete(hist_cmds);
  hist_delete(hist_regs);
  menu_delete(menu);
}

void start_ex_cmd(char symbol, int state)
{
  log_msg("EXCMD", "start");
  ex_state = state;
  state_symbol = symbol;
  pos_T max = layout_size();
  nc_win = newwin(1, 0, max.lnum - 1, 0);
  curpos = CURS_MIN;
  maxpos = max.col;
  mflag = 0;
  fmt_out = malloc(1);
  col_text = attr_color("ComplText");

  if (state == EX_REG_STATE) {
    Buffer *buf = window_get_focus();
    lm = buf->matches;
    regex_mk_pivot(lm);
  }
  else {
    cmd_stack = malloc(STACK_MIN * sizeof(cmd_part*));
    cur_part = -1;
    max_part = STACK_MIN;
    menu_start(menu);
  }
  line = calloc(max.col, sizeof(char*));
  cmd.cmds = NULL;
  cmd.line = NULL;
  hist_push(EXCMD_HIST(), &cmd);
  window_req_draw(NULL, NULL);
}

void stop_ex_cmd()
{
  log_msg("EXCMD", "stop_ex_cmd");
  if (ex_state == EX_CMD_STATE) {
    ex_cmd_pop(-1);
    free(cmd_stack);
    menu_stop(menu);
  }
  lm = NULL;
  free(line);
  free(fmt_out);
  cmdline_cleanup(&cmd);
  werase(nc_win);
  wnoutrefresh(nc_win);
  delwin(nc_win);
  curs_set(0);
  doupdate();
  ex_state = EX_OFF_STATE;
  window_ex_cmd_end();
}

static void str_ins(char *str, const char *ins, int pos, int ofs)
{
  char *buf = strdup(str);
  strncpy(str, buf, pos);
  strcpy(str+pos, ins);
  strcpy(str+strlen(str), buf+pos+ofs);
  free(buf);
}

static void gen_output_str()
{
  char ch;
  int i, j;

  free(fmt_out);
  fmt_out = calloc(maxpos+1, sizeof(char*));

  i = j = 0;
  while ((ch = line[i])) {
    fmt_out[j] = ch;

    if (ch == '%') {
      j++;
      fmt_out[j] = ch;
    }
    i++; j++;
  }
}

static void cmdline_draw()
{
  log_msg("EXCMD", "cmdline_draw");
  werase(nc_win);

  if (ex_state == EX_CMD_STATE)
    menu_draw(menu);

  wattron(nc_win, COLOR_PAIR(col_text));
  mvwaddch(nc_win, 0, 0, state_symbol);
  gen_output_str();
  mvwprintw(nc_win, 0, 1, fmt_out);
  wattroff(nc_win, COLOR_PAIR(col_text));

  wmove(nc_win, 0, curpos + 2);
  doupdate();
  curs_set(1);
  wnoutrefresh(nc_win);
}

void cmdline_refresh()
{
  pos_T max = layout_size();
  delwin(nc_win);
  nc_win = newwin(1, 0, max.lnum - 1, 0);
  maxpos = max.col;
  cmdline_draw();
}

static void ex_esc()
{
  if (ex_state == EX_REG_STATE)
    regex_pivot(lm);

  hist_save(EXCMD_HIST());
  mflag = EX_QUIT;
}

static void ex_tab(void *none, Keyarg *arg)
{
  log_msg("EXCMD", "TAB");
  const char *str = menu_next(menu, arg->arg);
  if (!str)
    return;

  cmd_part *part = cmd_stack[cur_part];
  int st = part->st;
  int ed = curpos + 1;
  if (st > 0)
    st++;

  char key[strlen(str) + 2];
  expand_escapes(key, str);
  int len = strlen(key);

  if (st + len >= maxpos - 1) {
    char *newline = strdup(line);
    maxpos = 2 * (len+maxpos);
    line = realloc(line, maxpos);
    memset(line, '\0', maxpos);
    strcpy(line, newline);
    free(newline);
  }

  int i;
  for (i = st; i < ed + 2; i++)
    line[i] = ' ';
  for (i = 0; i < len; i++)
    line[st + i] = key[i];

  curpos = st + i - 1;
  mflag = EX_CYCLE;
}

static void ex_hist(void *none, Keyarg *arg)
{
  const char *ret = NULL;

  if (arg->arg == BACKWARD)
    ret = hist_prev(EXCMD_HIST());
  if (arg->arg == FORWARD)
    ret = hist_next(EXCMD_HIST());
  if (ret) {
    ex_cmd_populate(ret);
    mflag = EX_HIST;
  }
}

void ex_cmd_populate(const char *newline)
{
  if (ex_state == EX_CMD_STATE) {
    ex_cmd_pop(-1);
    menu_rebuild(menu);
  }
  free(line);
  if (strlen(newline) < 1)
    line = calloc(maxpos, sizeof(char*));
  else
    line = strdup(newline);

  curpos = strlen(line) - 1;
}

static void ex_car()
{
  log_msg("EXCMD", "excar %s", line);
  if (ex_state == EX_CMD_STATE) {
    cmd_eval(NULL, line);
    cmd_flush();
  }

  hist_save(EXCMD_HIST());
  mflag = EX_QUIT;
}

static void ex_spc()
{
  log_msg("EXCMD", "ex_spc");
  curpos++;
  mflag |= EX_RIGHT;
  mflag &= ~(EX_FRESH|EX_NEW);
  if (curpos >= maxpos - 1) {
    char *newline = strdup(line);
    line = realloc(line, maxpos *= 2);
    memset(line, '\0', maxpos);
    strcpy(line, newline);
    free(newline);
  }

  line[curpos] = ' ';
}

static void ex_bckspc()
{
  log_msg("EXCMD", "bkspc");
  //FIXME: this will split line when cursor movement added
  line[curpos] = '\0';
  curpos--;
  mflag |= EX_LEFT;
  if (curpos < CURS_MIN)
    curpos = CURS_MIN;

  if (ex_state == EX_CMD_STATE) {
    if (curpos < cmd_stack[cur_part]->st)
      mflag |= EX_EMPTY;
  }
  mflag &= ~EX_FRESH;
}

static void ex_killword()
{
  //FIXME: regex_state does not build cmdline
  Token *last = cmdline_last(&cmd);
  if (!last)
    return;

  int st = last->start;
  int ed = last->end;
  for (int i = st; i < ed; i++)
    line[i] = ' ';

  curpos = st;
  ex_bckspc();
}

static void ex_killline()
{
  free(line);
  line = calloc(maxpos, sizeof(char*));
  curpos = -1;

  if (ex_state == EX_CMD_STATE) {
    ex_cmd_pop(-1);
    menu_restart(menu);
  }
  mflag = 0;
}

static void ex_cmdinvert()
{
  List *list = ex_cmd_curlist();
  if (!list || utarray_len(list->items) < 1)
    return;
  Token *word0 = tok_arg(list, 0);
  Token *word1 = cmdline_tokbtwn(&cmd, word0->end, word0->end+1);
  char *excl = token_val(word1, VAR_STRING);
  if (excl && excl[0] == '!') {
    str_ins(line, "", word1->start, 1);
    curpos--;
  }
  else {
    str_ins(line, "!", word0->end, 0);
    curpos++;
  }
}

static void ex_menuhints()
{
  menu_toggle_hints(menu);
}

static void ex_check_pipe()
{
  Cmdstr *cur = ex_cmd_curcmd();
  if (cur && cur->exec) {
    mflag = EX_EXEC;
    return;
  }
  if (mflag & EX_EXEC) {
    ex_cmd_pop(-1);
    menu_restart(menu);
    mflag = 0;
  }
  Cmdstr *prev = ex_cmd_prevcmd();
  if (prev && prev->flag) {
    menu_restart(menu);
    mflag = 0;
  }
}

static void check_new_state()
{
  if ((mflag & (EX_FRESH|EX_HIST)))
    return;
  ex_check_pipe();

  Token *tok = cmdline_last(&cmd);
  if (!tok)
    return;
  if (curpos + 1 > tok->end && curpos > 0)
    mflag |= EX_NEW;
}

static void ex_onkey()
{
  log_msg("EXCMD", "##%d", ex_cmd_state());
  if (ex_state == EX_CMD_STATE) {
    cmdline_cleanup(&cmd);
    cmdline_build(&cmd, line);
    check_new_state();
    menu_update(menu, &cmd);
  }
  else {
    if (window_focus_attached() && curpos > CURS_MIN) {
      regex_build(lm, line);
      regex_pivot(lm);
      regex_hover(lm);
    }
  }
  mflag &= ~EX_CLEAR;
  window_req_draw(NULL, NULL);
}

void ex_input(int key)
{
  log_msg("EXCMD", "input");
  if (menu_hints_enabled(menu))
    return menu_input(menu, key);

  Keyarg ca;
  int idx = find_command(&key_tbl, key);
  ca.arg = key_defaults[idx].cmd_arg;
  if (idx >= 0)
    key_defaults[idx].cmd_func(NULL, &ca);
  else {
    curpos++;
    mflag |= EX_RIGHT;
    mflag &= ~(EX_FRESH|EX_NEW);
    if (curpos >= maxpos)
      line = realloc(line, maxpos *= 2);

    // FIXME: wide char support
    line[curpos] = key;
  }

  if (mflag & EX_QUIT)
    stop_ex_cmd();
  else
    ex_onkey();
}

void ex_cmd_push(fn_context *cx)
{
  log_msg("EXCMD", "ex_cmd_push");
  cur_part++;

  if (cur_part >= max_part) {
    max_part *= 2;
    cmd_stack = realloc(cmd_stack, max_part*sizeof(cmd_part*));
  }

  cmd_stack[cur_part] = malloc(sizeof(cmd_part));
  cmd_stack[cur_part]->cx = cx;

  int st = 0;
  if (cur_part > 0) {
    st = curpos;
    if (line[st] == '|')
      st++;
  }
  cmd_stack[cur_part]->st = st;
  mflag &= ~EX_NEW;
  mflag |= EX_FRESH;
}

cmd_part* ex_cmd_pop(int count)
{
  log_msg("EXCMD", "ex_cmd_pop");
  if (cur_part < 0)
    return NULL;

  if (cur_part - count < 0)
    count = cur_part;
  else if (count == -1)
    count = cur_part + 1;

  while (count > 0) {
    compl_destroy(cmd_stack[cur_part]->cx);
    free(cmd_stack[cur_part]);
    cur_part--;
    count--;
  }
  if (cur_part < 0)
    return NULL;
  return cmd_stack[cur_part];
}

char ex_cmd_curch()
{
  return line[curpos];
}

int ex_cmd_curpos()
{
  return curpos;
}

Token* ex_cmd_curtok()
{
  cmd_part *part = cmd_stack[cur_part];
  int st = part->st;
  int ed = curpos + 1;
  Token *tok = cmdline_tokbtwn(&cmd, st, ed);
  return tok;
}

char* ex_cmd_line()
{
  return line;
}

char* ex_cmd_curstr()
{
  Token *tok = ex_cmd_curtok();
  if (tok)
    return token_val(tok, VAR_STRING);
  return "";
}

int ex_cmd_state()
{
  return mflag;
}

void ex_cmd_set(int pos)
{
  curpos = pos;
}

Cmdstr* ex_cmd_prevcmd()
{
  if (cur_part < 1)
    return NULL;
  cmd_part *part = cmd_stack[cur_part - 1];
  int st = part->st;
  int ed = curpos + 1;
  return cmdline_cmdbtwn(&cmd, st, ed);
}

Cmdstr* ex_cmd_curcmd()
{
  cmd_part *part = cmd_stack[cur_part];
  int st = part->st + 1;
  int ed = curpos;
  return cmdline_cmdbtwn(&cmd, st, ed);
}

List* ex_cmd_curlist()
{
  if (ex_state == EX_OFF_STATE)
    return NULL;
  if (!cmd.cmds)
    return NULL;

  Cmdstr *cmdstr = ex_cmd_curcmd();
  if (!cmdstr)
    cmdstr = (Cmdstr*)utarray_front(cmd.cmds);
  List *list = token_val(&cmdstr->args, VAR_LIST);
  return list;
}

int ex_cmd_curidx(List *list)
{
  cmd_part *part = cmd_stack[cur_part];
  int st = part->st;
  int ed = curpos + 1;
  return utarray_eltidx(list->items, list_tokbtwn(list, st, ed));
}
