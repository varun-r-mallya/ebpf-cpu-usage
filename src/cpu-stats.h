#ifndef __CPU_STATS_H
#define __CPU_STATS_H

struct cpu_stat_s {
  unsigned long long cpu;
  unsigned long long user;
  unsigned long long nice;
  unsigned long long sys;
  unsigned long long idle;
  unsigned long long iowait;
  unsigned long long irq;
  unsigned long long softirq;
  unsigned long long steal;
  unsigned long long guest;
  unsigned long long guest_nice;
};

#endif /* __CPU_STATS_H */
