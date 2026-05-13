#include "single_instance.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static char *g_sock_path;

static bool bind_listen(int *out_fd) {
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return false;
  }
  struct sockaddr_un sun = {0};
  sun.sun_family = AF_UNIX;
  if (g_strlcpy(sun.sun_path, g_sock_path, sizeof sun.sun_path) >= sizeof sun.sun_path) {
    close(fd);
    errno = ENAMETOOLONG;
    return false;
  }
  if (bind(fd, (struct sockaddr *)&sun, sizeof sun) < 0) {
    int e = errno;
    close(fd);
    errno = e;
    return false;
  }
  if (listen(fd, 8) < 0) {
    int e = errno;
    close(fd);
    unlink(g_sock_path);
    errno = e;
    return false;
  }
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl >= 0) {
    (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  }
  *out_fd = fd;
  return true;
}

static bool client_toggle(void) {
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return false;
  }
  struct sockaddr_un sun = {0};
  sun.sun_family = AF_UNIX;
  g_strlcpy(sun.sun_path, g_sock_path, sizeof sun.sun_path);
  if (connect(fd, (struct sockaddr *)&sun, sizeof sun) < 0) {
    close(fd);
    return false;
  }
  char z = 0;
  (void)write(fd, &z, 1);
  shutdown(fd, SHUT_RDWR);
  close(fd);
  return true;
}

bool br_single_instance_acquire(int *listen_fd_out) {
  *listen_fd_out = -1;
  g_clear_pointer(&g_sock_path, g_free);
  const char *rt = g_get_user_runtime_dir();
  if (!rt || !*rt) {
    return true;
  }
  g_sock_path = g_build_filename(rt, "bookrunner.sock", NULL);

  for (int attempt = 0; attempt < 2; attempt++) {
    if (bind_listen(listen_fd_out)) {
      return true;
    }
    if (errno == EADDRINUSE && attempt == 0) {
      if (client_toggle()) {
        return false;
      }
      unlink(g_sock_path);
      continue;
    }
    break;
  }
  *listen_fd_out = -1;
  return true;
}

void br_single_instance_release(int listen_fd) {
  if (listen_fd >= 0) {
    close(listen_fd);
  }
  if (g_sock_path) {
    unlink(g_sock_path);
  }
  g_clear_pointer(&g_sock_path, g_free);
}
