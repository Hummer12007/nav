#include <unistd.h>
#include <wordexp.h>

#include "nav/log.h"
#include "nav/config.h"
#include "nav/option.h"
#include "nav/cmdline.h"
#include "nav/cmd.h"
#include "nav/compl.h"
#include "nav/info.h"
#include "nav/event/input.h"
#include "nav/event/fs.h"
#include "nav/event/hook.h"

static Cmdret conf_autocmd();
static Cmdret conf_setting();
static Cmdret conf_color();
static Cmdret conf_syntax();
static Cmdret conf_variable();
static Cmdret conf_mapping();
static Cmdret conf_op();
static Cmdret conf_source();

static const Cmd_T cmdtable[] = {
  {"autocmd","au",   conf_autocmd,  0},
  {"set",0,          conf_setting,  0},
  {"highlight","hi", conf_color,    0},
  {"syntax","syn",   conf_syntax,   0},
  {"let",0,          conf_variable, 0},
  {"local",0,        conf_variable, 1},
  {"map",0,          conf_mapping,  0},
  {"op",0,           conf_op,       0},
  {"source","so",    conf_source,   0},
};

static char *compl_cmds[] = {
  "set;option:string:options",
  "hi;group:string:groups",
  "op;group:string:groups",
};

static const char *config_paths[] = {
  "$HOME/.navrc",
  "$HOME/.nav/.navrc",
  "/etc/nav/navrc",
};

static const char *info_paths[] = {
  "$HOME/.navinfo",
  "$HOME/.nav/.navinfo",
};

void config_init()
{
  for (int i = 0; i < LENGTH(cmdtable); i++)
    cmd_add(&cmdtable[i]);
  for (int i = 0; i < LENGTH(compl_cmds); i++)
    compl_add_context(compl_cmds[i]);
}

static bool file_exists(const char *path)
{
  return access(path, R_OK) != -1;
}

char* strip_whitespace(char *_str)
{
  if (*_str == '\0')
    return _str;

  char *strold = _str;
  while (*_str == ' ' || *_str == '\t')
    _str++;

  char *str = strdup(_str);
  free(strold);
  int i;

  /* seek forwards */
  for (i = 0; str[i] != '\0'; ++i);

  while (i >= 0 && (str[i] == ' ' || str[i] == '\t'))
    i--;

  str[i] = '\0';
  return str;
}

static char* get_config_path(const char **paths, ssize_t len)
{
  wordexp_t p;
  char *path;

  int i;
  for (i = 0; i < (int)(len / sizeof(char *)); ++i) {
    if (wordexp(paths[i], &p, 0) == 0) {
      path = p.we_wordv[0];
      if (file_exists(path)) {
        path = strdup(path);
        wordfree(&p);
        return path;
      }
      wordfree(&p);
    }
  }
  return NULL;
}

static char* read_line(FILE *file)
{
  int length = 0, size = 128;
  char *string = malloc(size);
  if (!string)
    return NULL;

  while (1) {
    int c = getc(file);
    if (c == EOF || c == '\n' || c == '\0')
      break;
    if (c == '\r')
      continue;

    if (length == size) {
      char *new_string = realloc(string, size *= 2);
      if (!new_string) {
        free(string);
        return NULL;
      }
      string = new_string;
    }
    string[length++] = c;
  }
  if (length + 1 == size) {
    char *new_string = realloc(string, length + 1);
    if (!new_string) {
      free(string);
      return NULL;
    }
    string = new_string;
  }
  string[length] = '\0';
  return string;
}

static FILE* config_open(const char *file, const char **defaults, char *mode)
{
  log_msg("CONFIG", "config_open");
  char *path;
  if (file)
    path = strdup(file);
  else
    path = get_config_path(defaults, sizeof(defaults));

  if (!path) {
    log_msg("CONFIG", "Unable to find a config file!");
    return NULL;
  }

  FILE *f = fopen(path, mode);
  free(path);
  if (!f) {
    fprintf(stderr, "Unable to open '%s' for reading", path);
    return NULL;
  }
  return f;
}

void config_load(const char *file)
{
  FILE *f = config_open(file, config_paths, "r");
  if (!f)
    return;
  config_read(f);
  fclose(f);
}

void config_start(char *config_path)
{
  config_load(config_path);
  FILE *f = config_open(NULL, info_paths, "r");
  log_msg("CONFIG", "%p", f);
  if (!f) {
    char *path = fs_expand_path(info_paths[0]);
    f = fopen(path, "w+");
    if (f)
      fclose(f);
    free(path);
    return;
  }
  info_read(f);
  fclose(f);
}

void config_write_info()
{
  log_msg("CONFIG", "config_write_info");
  FILE *f = config_open(NULL, info_paths, "w");
  if (!f)
    return;
  info_write_file(f);
  fclose(f);
}

bool info_read(FILE *file)
{
  int line_number = 0;
  char *line;
  while (!feof(file)) {
    line = read_line(file);
    line_number++;
    if (line[0] == '#' || !strpbrk(&line[0], "?/:'@")) {
      free(line);
      continue;
    }
    info_parse(line);
    free(line);
  }
  return 1;
}

bool config_read(FILE *file)
{
  int line_number = 0;
  char *line;
  while (!feof(file)) {
    line = read_line(file);
    line_number++;
    line = strip_whitespace(line);
    if (line[0] == '#' || strlen(line) < 1) {
      free(line);
      continue;
    }
    cmd_eval(NULL, line);
    free(line);
  }
  cmd_flush();
  return 1;
}

Cmdret conf_autocmd(List *args, Cmdarg *ca)
{
  log_msg("CONFIG", "autocmd");
  int len = utarray_len(args->items);
  char *event = list_arg(args, 1, VAR_STRING);
  int pos = len > 3 ? 2 : -1;
  int rem = len > 3 ? 3 : 2;
  char *pat = list_arg(args, pos, VAR_STRING);
  char *cur = cmdline_line_after(ca->cmdline, rem-1);

  if (event && ca->cmdstr->rev)
    hook_remove(event, pat);
  else if (event && cur)
    hook_add(event, pat, cur+1);
  return NORET;
}

static Cmdret conf_setting(List *args, Cmdarg *ca)
{
  log_msg("CONFIG", "conf_setting");
  char *name = list_arg(args, 1, VAR_STRING);
  Token *oper = tok_arg(args, 2);
  Token *rhs = tok_arg(args, 3);
  if (!name|| !oper || !rhs)
    return NORET;
  char *value = cmdline_line_from(ca->cmdline, 3);
  set_opt(name, value);
  return NORET;
}

static Cmdret conf_color(List *args, Cmdarg *ca)
{
  log_msg("CONFIG", "conf_color");
  if (utarray_len(args->items) < 3)
    return NORET;

  int fg, bg;
  int ret = 0;
  ret += str_num(list_arg(args, 2, VAR_STRING), &fg);
  ret += str_num(list_arg(args, 3, VAR_STRING), &bg);
  char *group = list_arg(args, 1, VAR_STRING);
  if (!ret || !group)
    return NORET;

  fn_group *grp = get_group(group);
  if (!grp)
    return NORET;
  //TODO: error msg
  set_color(grp, fg, bg);
  //TODO: refresh cached colors
  return NORET;
}

static Cmdret conf_syntax(List *args)
{
  log_msg("CONFIG", "conf_syntax");
  char *group = list_arg(args, 1, VAR_STRING);
  List *words = list_arg(args, 2, VAR_LIST);
  char *single = list_arg(args, 2, VAR_STRING);
  if (!group && (!words || !single))
    return NORET;

  fn_group *grp = get_group(group);
  if (!grp)
    grp = set_group(group);

  if (single) {
    fn_syn syn = {
      .key = strdup(single),
      .group = grp,
    };
    set_syn(&syn);
    return NORET;
  }

  log_msg("CONFIG", "# %d", utarray_len(words->items));
  for (int i = 0; i < utarray_len(words->items); i++) {
    fn_syn syn = {
      .key = strdup(list_arg(words, i, VAR_STRING)),
      .group = grp,
    };
    set_syn(&syn);
  }
  return NORET;
}

static Cmdret conf_variable(List *args, Cmdarg *ca)
{
  log_msg("CONFIG", "conf_variable");
  log_err("CONFIG", "%s", ca->cmdline->line);

  /* statement needs valid name */
  Token *lhs = tok_arg(args, 1);
  char *key = list_arg(args, 1, VAR_STRING);
  if (!lhs || !key)
    return NORET;

  /* find where '=' delimits statement */
  int delm = -1;
  for (int i = 0; i < utarray_len(args->items); i++) {
    char *op = list_arg(args, i, VAR_STRING);
    if (op && !strcmp(op, "=")) {
      delm = i+1;
      break;
    }
  }
  if (delm == -1 || delm >= utarray_len(args->items))
    return NORET;

  char *expr = cmdline_line_from(ca->cmdline, delm);
  fn_var var = {
    .key = strdup(key),
    .var = strdup(expr),
  };

  /* when delm isn't 3 the key should be expanded */
  if (delm != 3) {
    char *pvar = opt_var(key, cmd_callstack());
    if (!pvar)
      goto cleanup;

    char *newexpr = repl_elem(pvar, expr, args, 2, delm - 1);
    if (!newexpr)
      goto cleanup;
    SWAP_ALLOC_PTR(var.var, newexpr);
  }

  fn_func *blk = cmd_callstack();
  if (!ca->flags)
    blk = NULL;
  set_var(&var, blk);

  return NORET;
cleanup:
  free(var.key);
  free(var.var);
  return NORET;
}

static Cmdret conf_mapping(List *args, Cmdarg *ca)
{
  if (strlen(ca->cmdline->line) < strlen("map ") + 3)
    return NORET;
  char *line = ca->cmdline->line + strlen("map ");
  if (!line)
    return NORET;
  char *from = strdup(line);
  char *to = strchr(from, ' ');
  *to = '\0';
  log_msg("CONFIG", "%s -> %s", from, to);
  set_map(from, to+1);
  free(from);
  return NORET;
}

static Cmdret conf_op(List *args, Cmdarg *ca)
{
  log_msg("CONFIG", "conf_op");
  char *group  = list_arg(args, 1, VAR_STRING);
  char *before = list_arg(args, 2, VAR_STRING);
  char *after  = list_arg(args, 3, VAR_STRING);
  if (!group || !before)
    return NORET;
  if (!after)
    after = "";

  fn_group *grp = get_group(group);
  if (!grp)
    return NORET;

  grp->opgrp = op_newgrp(before, after);
  return NORET;
}

static Cmdret conf_source(List *args, Cmdarg *ca)
{
  char *file = cmdline_line_after(ca->cmdline, 0);
  if (!file)
    return NORET;

  char *path = fs_expand_path(file);
  if (path && file_exists(path))
    config_load(path);
  if (path)
    free(path);

  return NORET;
}
