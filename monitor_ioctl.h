#ifndef MONITOR_IOCTL_H
#define MONITOR_IOCTL_H

#include <linux/ioctl.h>

#define MONITOR_MAGIC 'k'

struct container_config {
  int pid;
  unsigned long soft_limit_mb;
  unsigned long hard_limit_mb;
};

#define MONITOR_REGISTER _IOW(MONITOR_MAGIC, 1, struct container_config)
#define MONITOR_UNREGISTER _IOW(MONITOR_MAGIC, 2, int)

#endif