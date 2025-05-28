#include "cpu-stats.h"
#include "cpu-stats.skel.h"
#include <argp.h>
#include <bpf/libbpf.h>
#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <time.h>

static struct env {
  bool verbose;
  long min_duration_ms;
} env;

const char *argp_program_version = "cpu-stats 0.0";
const char *argp_program_bug_address = "<varunrmallya@gmail.com>";
const char argp_program_doc[] =
    "BPF based CPU statistics tracer.\n"
    "\n"
    "Traces CPU information found in /proc/stat \n"
    "\n"
    "USAGE: ./cpu-stats [-d <min-duration-ms>] [-v]\n";

static const struct argp_option opts[] = {
    {"verbose", 'v', NULL, 0, "Verbose debug output"},
    {"duration", 'd', "DURATION-MS", 0,
     "Minimum process duration (ms) to report"},
    {},
};

static error_t parse_arg(int key, char *arg, struct argp_state *state) {
  switch (key) {
  case 'v':
    env.verbose = true;
    break;
  case 'd':
    errno = 0;
    env.min_duration_ms = strtol(arg, NULL, 10);
    if (errno || env.min_duration_ms <= 0) {
      fprintf(stderr, "Invalid duration: %s\n", arg);
      argp_usage(state);
    }
    break;
  case ARGP_KEY_ARG:
    argp_usage(state);
    break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static const struct argp argp = {
    .options = opts,
    .parser = parse_arg,
    .doc = argp_program_doc,
};

static int libbpf_print_fn(enum libbpf_print_level level, const char *format,
                           va_list args) {
  if (level == LIBBPF_DEBUG && !env.verbose)
    return 0;
  return vfprintf(stderr, format, args);
}

static volatile bool exiting = false;

static void sig_handler(int sig) { exiting = true; }

static int handle_event(void *ctx, void *data, size_t data_sz) {
  const struct cpu_stat_s *e = data;
  struct tm *tm;
  char ts[32];
  time_t t;

  time(&t);
  tm = localtime(&t);
  strftime(ts, sizeof(ts), "%H:%M:%S", tm);

  if (data_sz < sizeof(*e)) {
    fprintf(stderr, "Received event with invalid size: %zu\n", data_sz);
    return 0;
  }

  printf("%-10lld %-15lld %-15lld %-15lld %-15lld %-15lld %-15lld %-15lld "
         "%-15lld %-15lld %-15lld\n",
         e->cpu, e->user, e->nice, e->sys, e->idle, e->iowait, e->irq,
         e->softirq, e->steal, e->guest, e->guest_nice);

  return 0;
}

int main(int argc, char **argv) {
  struct ring_buffer *rb = NULL;
  struct cpu_stats_bpf *skel;
  int err;

  /* Parse command line arguments */
  err = argp_parse(&argp, argc, argv, 0, NULL, NULL);
  if (err)
    return err;

  /* Set up libbpf errors and debug info callback */
  libbpf_set_print(libbpf_print_fn);

  /* Cleaner handling of Ctrl-C */
  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  /* Load and verify BPF application */
  skel = cpu_stats_bpf__open();
  if (!skel) {
    fprintf(stderr, "Failed to open and load BPF skeleton\n");
    return 1;
  }

  /* Parameterize BPF code with minimum duration parameter */
  skel->rodata->min_duration_ns = env.min_duration_ms * 1000000ULL;

  /* Load & verify BPF programs */
  err = cpu_stats_bpf__load(skel);
  if (err) {
    fprintf(stderr, "Failed to load and verify BPF skeleton\n");
    goto cleanup;
  }

  /* Attach tracepoints */
  err = cpu_stats_bpf__attach(skel);
  if (err) {
    fprintf(stderr, "Failed to attach BPF skeleton\n");
    goto cleanup;
  }

  /* Set up ring buffer polling */
  rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), handle_event, NULL, NULL);
  if (!rb) {
    err = -1;
    fprintf(stderr, "Failed to create ring buffer\n");
    goto cleanup;
  }

  /* Process events */
  printf("%-10s %-15s %-15s %-15s %-15s %-15s %-15s %-15s %-15s %-15s %-15s\n",
         "CPU", "USER", "NICE", "SYS", "IDLE", "IOWAIT", "IRQ", "SOFTIRQ",
         "STEAL", "GUEST", "GUEST_NICE");

  while (!exiting) {
    err = ring_buffer__poll(rb, 100 /* timeout, ms */);
    /* Ctrl-C will cause -EINTR */
    if (err == -EINTR) {
      err = 0;
      break;
    }
    if (err < 0) {
      printf("Error polling perf buffer: %d\n", err);
      break;
    }
  }

cleanup:
  /* Clean up */
  ring_buffer__free(rb);
  cpu_stats_bpf__destroy(skel);

  return err < 0 ? -err : 0;
}
