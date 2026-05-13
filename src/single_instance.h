#pragma once

#include <stdbool.h>

bool br_single_instance_acquire(int *listen_fd_out);
void br_single_instance_release(int listen_fd);
