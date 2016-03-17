#ifndef FN_UTIL_H
#define FN_UTIL_H

#include <ncurses.h>

typedef struct {
  char* (*expfn)(char*, char*);
  char *key;
} Exparg;

void set_exparg(Exparg *exparg);
bool exparg_isset();
void draw_wide(WINDOW *win, int row, int col, char *src, int max);
void readable_fs(double size/*in bytes*/, char buf[]);
void expand_escapes(char *dest, const char *src);
void del_param_list(char **params, int argc);
char* do_expansion(char *src, Exparg *arg);

#endif
