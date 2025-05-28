#include "../bootstrap.h"
#include "vmlinux.h"
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 8192);
  __type(key, pid_t);
  __type(value, u64);
} exec_start SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, 256 * 1024);
} rb SEC(".maps");

const volatile unsigned long long min_duration_ns = 0;

// // Fentry program for kcpustat_cpu_fetch
// SEC("fentry/kcpustat_cpu_fetch")
// int BPF_PROG(fentry_kcpustat_cpu_fetch, struct kernel_cpustat *kcpustat, int
// cpu)
// {
//     // Print CPU number and header
//     bpf_printk("=== CPU %d Statistics (fentry) ===", cpu);

//     // Extract and print each CPU statistic

//     return 0;
// }

SEC("fexit/kcpustat_cpu_fetch")
int BPF_PROG(fexit_kcpustat_cpu_fetch, struct kernel_cpustat *kcpustat,
             int cpu) {
  struct cpu_stat_s *stat;

  stat = bpf_ringbuf_reserve(&rb, sizeof(*stat), 0);
  if (!stat)
    return 0;

  stat->cpu = cpu;
  stat->user = BPF_CORE_READ(kcpustat, cpustat[CPUTIME_USER]);
  stat->nice = BPF_CORE_READ(kcpustat, cpustat[CPUTIME_NICE]);
  stat->sys = BPF_CORE_READ(kcpustat, cpustat[CPUTIME_SYSTEM]);
  stat->idle = BPF_CORE_READ(kcpustat, cpustat[CPUTIME_IDLE]);
  stat->iowait = BPF_CORE_READ(kcpustat, cpustat[CPUTIME_IOWAIT]);
  stat->irq = BPF_CORE_READ(kcpustat, cpustat[CPUTIME_IRQ]);
  stat->softirq = BPF_CORE_READ(kcpustat, cpustat[CPUTIME_SOFTIRQ]);
  stat->steal = BPF_CORE_READ(kcpustat, cpustat[CPUTIME_STEAL]);
  stat->guest = BPF_CORE_READ(kcpustat, cpustat[CPUTIME_GUEST]);
  stat->guest_nice = BPF_CORE_READ(kcpustat, cpustat[CPUTIME_GUEST_NICE]);
  bpf_ringbuf_submit(stat, 0);

  return 0;
}