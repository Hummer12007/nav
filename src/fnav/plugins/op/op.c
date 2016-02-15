#include <sys/wait.h>
#include <errno.h>
#include "fnav/plugins/op/op.h"
#include "fnav/log.h"
#include "fnav/table.h"
#include "fnav/model.h"
#include "fnav/event/hook.h"
#include "fnav/event/event.h"

//TODO: we want a single mpv process shared across all cntlr instances
//so basically, create an op_cntlr once on the first init call. return
//a pointer to this after. reference count on cleanup and do actual
//cleanup on count of 0. this mess can be properly managed when the real
//cntlr is made.

static Op *op_default;
static uv_process_t proc;
static uv_process_options_t opts;

static void exit_cb(uv_process_t *req, int64_t exit_status, int term_signal)
{
  log_msg("OP", "exit_cb");
  uv_close((uv_handle_t*) req, NULL);
  Op *op = (Op*)req->data;
  op->ready = true;
  system("pkill compton");
}

static void chld_handler(uv_signal_t *handle, int signum)
{
  log_msg("OP", "chldhand");
  int stat = 0;
  int pid;

  do {
    pid = waitpid(-1, &stat, WNOHANG);
  } while (pid < 0 && errno == EINTR);

  if (pid <= 0)
    return;
}

static void create_proc(Op *op, String path)
{
  log_msg("OP", "create_proc");
  opts.flags = UV_PROCESS_DETACHED;
  opts.exit_cb = exit_cb;
  char *rv[] = {"mpv", "--fs", path, NULL};
  opts.file = rv[0];
  opts.args = rv;
  proc.data = op;

  if (!op->ready) {
    log_msg("OP", "kill");
    uv_kill(proc.pid, SIGKILL);
    uv_close((uv_handle_t*)&proc, NULL);
    uv_run(eventloop(), UV_RUN_NOWAIT);
  }

  log_msg("OP", "spawn");
  uv_signal_start(&mainloop()->children_watcher, chld_handler, SIGCHLD);
  uv_disable_stdio_inheritance();
  int ret = uv_spawn(eventloop(), &proc, &opts);
  op->ready = false;

  if (ret < 0)
    log_msg("?", "file: |%s|, %s", path, uv_strerror(ret));

  uv_unref((uv_handle_t*) &proc);
}

const char* file_ext(const char *filename)
{
  const char *dot = strrchr(filename, '.');
  if (!dot || dot == filename)
    return "";
  return dot + 1;
}

static void fileopen_cb(Plugin *host, Plugin *caller, HookArg *hka)
{
  log_msg("OP", "fileopen_cb");
  Op *op = (Op*)caller->top;

  String path = model_curs_value(host->hndl->model, "fullpath");

  create_proc(op, path);
  system("mpv_i");
}

void op_new(Plugin *plugin, Buffer *buf, void *arg)
{
  log_msg("OP", "INIT");
  op_default = malloc(sizeof(Op));
  op_default->base = plugin;
  plugin->top = op_default;
  plugin->name = "op";
  op_default->ready = true;
  hook_init_host(plugin);
  hook_add_intl(plugin, plugin, fileopen_cb, "fileopen");
  hook_set_tmp("fileopen");
}

void op_delete(Plugin *cntlr)
{
  log_msg("OP", "op_cleanup");
}
