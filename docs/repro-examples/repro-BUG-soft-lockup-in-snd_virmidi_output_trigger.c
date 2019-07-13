// See https://lkml.org/lkml/2018/7/26/73
// autogenerated by syzkaller (http://github.com/google/syzkaller)

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <linux/futex.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/if_arp.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

__attribute__((noreturn)) static void doexit(int status)
{
  volatile unsigned i;
  syscall(__NR_exit_group, status);
  for (i = 0;; i++) {
  }
}
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const int kFailStatus = 67;
const int kRetryStatus = 69;
const int kHypercallFail = 70;

static void fail(const char* msg, ...)
{
  int e = errno;
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
  fprintf(stderr, " (errno %d)\n", e);
  doexit((e == ENOMEM || e == EAGAIN) ? kRetryStatus : kFailStatus);
}

void mylogf(const char* msg, ...)
{
  va_list args;
  va_start(args, msg);
  vfprintf(stdout, msg, args);
  va_end(args);
  fprintf(stdout, "\n");
  fflush(stdout);
}

static int flag_debug;

static void debug(const char* msg, ...)
{
  if (!flag_debug)
    return;
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
  fflush(stderr);
}

#define __HYPERCALL_H

#define SYS_hypercall 335

enum {
  CMD_START = 0,
  CMD_END = 1,
  CMD_REFRESH = 2,
};

static __thread int skip_segv;
static __thread jmp_buf segv_env;

static void segv_handler(int sig, siginfo_t* info, void* uctx)
{
  uintptr_t addr = (uintptr_t)info->si_addr;
  const uintptr_t prog_start = 1 << 20;
  const uintptr_t prog_end = 100 << 20;
  if (__atomic_load_n(&skip_segv, __ATOMIC_RELAXED) &&
      (addr < prog_start || addr > prog_end)) {
    debug("SIGSEGV on %p, skipping\n", (void*)addr);
    _longjmp(segv_env, 1);
  }
  debug("SIGSEGV on %p, exiting\n", (void*)addr);
  doexit(sig);
}

static void install_segv_handler()
{
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_IGN;
  syscall(SYS_rt_sigaction, 0x20, &sa, NULL, 8);
  syscall(SYS_rt_sigaction, 0x21, &sa, NULL, 8);

  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = segv_handler;
  sa.sa_flags = SA_NODEFER | SA_SIGINFO;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
}

#define NONFAILING(...)                                                        \
  {                                                                            \
    __atomic_fetch_add(&skip_segv, 1, __ATOMIC_SEQ_CST);                       \
    if (_setjmp(segv_env) == 0) {                                              \
      __VA_ARGS__;                                                             \
    }                                                                          \
    __atomic_fetch_sub(&skip_segv, 1, __ATOMIC_SEQ_CST);                       \
  }

static void use_temporary_dir()
{
  char tmpdir_template[] = "./syzkaller.XXXXXX";
  char* tmpdir = mkdtemp(tmpdir_template);
  if (!tmpdir)
    fail("failed to mkdtemp");
  if (chmod(tmpdir, 0777))
    fail("failed to chmod");
  if (chdir(tmpdir))
    fail("failed to chdir");
}

static void vsnprintf_check(char* str, size_t size, const char* format,
                            va_list args)
{
  int rv;

  rv = vsnprintf(str, size, format, args);
  if (rv < 0)
    fail("tun: snprintf failed");
  if ((size_t)rv >= size)
    fail("tun: string '%s...' doesn't fit into buffer", str);
}

static void snprintf_check(char* str, size_t size, const char* format, ...)
{
  va_list args;

  va_start(args, format);
  vsnprintf_check(str, size, format, args);
  va_end(args);
}

#define COMMAND_MAX_LEN 128
#define PATH_PREFIX                                                            \
  "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin "
#define PATH_PREFIX_LEN (sizeof(PATH_PREFIX) - 1)

static void execute_command(bool panic, const char* format, ...)
{
  va_list args;
  char command[PATH_PREFIX_LEN + COMMAND_MAX_LEN];
  int rv;

  va_start(args, format);
  memcpy(command, PATH_PREFIX, PATH_PREFIX_LEN);
  vsnprintf_check(command + PATH_PREFIX_LEN, COMMAND_MAX_LEN, format, args);
  va_end(args);
  rv = system(command);
  if (rv) {
    if (panic)
      fail("command '%s' failed: %d", &command[0], rv);
    debug("command '%s': %d\n", &command[0], rv);
  }
}

static int tunfd = -1;
static int tun_frags_enabled;

#define SYZ_TUN_MAX_PACKET_SIZE 1000

#define TUN_IFACE "syz_tun"

#define LOCAL_MAC "aa:aa:aa:aa:aa:aa"
#define REMOTE_MAC "aa:aa:aa:aa:aa:bb"

#define LOCAL_IPV4 "172.20.20.170"
#define REMOTE_IPV4 "172.20.20.187"

#define LOCAL_IPV6 "fe80::aa"
#define REMOTE_IPV6 "fe80::bb"

#define IFF_NAPI 0x0010
#define IFF_NAPI_FRAGS 0x0020

static bool should_hypercall = false;
static uint64_t is_race[2];
static void* execute_thr(void* arg);

pthread_barrier_t race_barrier;

static inline uint64_t repro_hypercall(long id, long cmd, unsigned long bp,
                                       int sched)
{
  uint64_t ret = syscall(SYS_hypercall, id, cmd, bp, sched);
  return ret;
}

#define RACE_INIT 0xffff
#define RACE_ERR (uint64_t) - 1
#define RACE_FAIL 0
#define RACE_OK 1

static long bps[2];

void race_hcall_prepare(long arg)
{
  if (should_hypercall) {
    repro_hypercall((long)arg, CMD_REFRESH, 0, 0);
  }
}

void race_hcall_setup(long arg, long sched_order)
{
  if (should_hypercall) {
    uint64_t res_hcall =
        repro_hypercall((long)arg, CMD_START, bps[(int)arg], sched_order);
    if (res_hcall == (uint64_t)-1) {
      debug("Failed to setup breakpoint using hypercall (%x)\n", arg);
      exit(0);
    }
    is_race[(int)arg] = RACE_INIT;
  }
}

void race_hcall_mark_and_check(long arg)
{
  if (should_hypercall) {
    pthread_barrier_wait(&race_barrier);
    uint64_t res_hcall = repro_hypercall((long)arg, CMD_END, 0, 0);
    is_race[(int)arg] = res_hcall;

    pthread_barrier_wait(&race_barrier);

    if (arg == 0) {
      if ((is_race[0] == RACE_FAIL) && (is_race[0] == RACE_FAIL)) {
      } else if ((is_race[0] == RACE_OK) || (is_race[1] == RACE_OK)) {
        debug("[OK] race occured\n");
      } else {
        debug("[ERROR] race error\n");
      }
    }
    pthread_barrier_wait(&race_barrier);
  }
}

static void initialize_tun(void)
{
  tunfd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
  if (tunfd == -1) {
    printf("tun: can't open /dev/net/tun: please enable CONFIG_TUN=y\n");
    printf("otherwise fuzzing or reproducing might not work as intended\n");
    return;
  }
  const int kTunFd = 252;
  if (dup2(tunfd, kTunFd) < 0)
    fail("dup2(tunfd, kTunFd) failed");
  close(tunfd);
  tunfd = kTunFd;

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, TUN_IFACE, IFNAMSIZ);
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_NAPI | IFF_NAPI_FRAGS;
  if (ioctl(tunfd, TUNSETIFF, (void*)&ifr) < 0) {
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (ioctl(tunfd, TUNSETIFF, (void*)&ifr) < 0)
      fail("tun: ioctl(TUNSETIFF) failed");
  }
  if (ioctl(tunfd, TUNGETIFF, (void*)&ifr) < 0)
    fail("tun: ioctl(TUNGETIFF) failed");
  tun_frags_enabled = (ifr.ifr_flags & IFF_NAPI_FRAGS) != 0;
  debug("tun_frags_enabled=%d\n", tun_frags_enabled);

  execute_command(1, "sysctl -w net.ipv6.conf.%s.accept_dad=0", TUN_IFACE);

  execute_command(1, "sysctl -w net.ipv6.conf.%s.router_solicitations=0",
                  TUN_IFACE);

  execute_command(1, "ip link set dev %s address %s", TUN_IFACE, LOCAL_MAC);
  execute_command(1, "ip addr add %s/24 dev %s", LOCAL_IPV4, TUN_IFACE);
  execute_command(1, "ip -6 addr add %s/120 dev %s", LOCAL_IPV6, TUN_IFACE);
  execute_command(1, "ip neigh add %s lladdr %s dev %s nud permanent",
                  REMOTE_IPV4, REMOTE_MAC, TUN_IFACE);
  execute_command(1, "ip -6 neigh add %s lladdr %s dev %s nud permanent",
                  REMOTE_IPV6, REMOTE_MAC, TUN_IFACE);
  execute_command(1, "ip link set dev %s up", TUN_IFACE);
}

#define DEV_IPV4 "172.20.20.%d"
#define DEV_IPV6 "fe80::%02hx"
#define DEV_MAC "aa:aa:aa:aa:aa:%02hx"

static void initialize_netdevices(void)
{
  unsigned i;
  const char* devtypes[] = {"ip6gretap", "bridge", "vcan", "bond", "team"};
  const char* devnames[] = {"lo",
                            "sit0",
                            "bridge0",
                            "vcan0",
                            "tunl0",
                            "gre0",
                            "gretap0",
                            "ip_vti0",
                            "ip6_vti0",
                            "ip6tnl0",
                            "ip6gre0",
                            "ip6gretap0",
                            "erspan0",
                            "bond0",
                            "veth0",
                            "veth1",
                            "team0",
                            "veth0_to_bridge",
                            "veth1_to_bridge",
                            "veth0_to_bond",
                            "veth1_to_bond",
                            "veth0_to_team",
                            "veth1_to_team"};
  const char* devmasters[] = {"bridge", "bond", "team"};

  for (i = 0; i < sizeof(devtypes) / (sizeof(devtypes[0])); i++)
    execute_command(0, "ip link add dev %s0 type %s", devtypes[i], devtypes[i]);
  execute_command(0, "ip link add type veth");

  for (i = 0; i < sizeof(devmasters) / (sizeof(devmasters[0])); i++) {
    execute_command(
        0, "ip link add name %s_slave_0 type veth peer name veth0_to_%s",
        devmasters[i], devmasters[i]);
    execute_command(
        0, "ip link add name %s_slave_1 type veth peer name veth1_to_%s",
        devmasters[i], devmasters[i]);
    execute_command(0, "ip link set %s_slave_0 master %s0", devmasters[i],
                    devmasters[i]);
    execute_command(0, "ip link set %s_slave_1 master %s0", devmasters[i],
                    devmasters[i]);
    execute_command(0, "ip link set veth0_to_%s up", devmasters[i]);
    execute_command(0, "ip link set veth1_to_%s up", devmasters[i]);
  }
  execute_command(0, "ip link set bridge_slave_0 up");
  execute_command(0, "ip link set bridge_slave_1 up");

  for (i = 0; i < sizeof(devnames) / (sizeof(devnames[0])); i++) {
    char addr[32];
    snprintf_check(addr, sizeof(addr), DEV_IPV4, i + 10);
    execute_command(0, "ip -4 addr add %s/24 dev %s", addr, devnames[i]);
    snprintf_check(addr, sizeof(addr), DEV_IPV6, i + 10);
    execute_command(0, "ip -6 addr add %s/120 dev %s", addr, devnames[i]);
    snprintf_check(addr, sizeof(addr), DEV_MAC, i + 10);
    execute_command(0, "ip link set dev %s address %s", devnames[i], addr);
    execute_command(0, "ip link set dev %s up", devnames[i]);
  }
}

static uintptr_t syz_open_dev(uintptr_t a0, uintptr_t a1, uintptr_t a2)
{
  if (a0 == 0xc || a0 == 0xb) {
    char buf[128];
    sprintf(buf, "/dev/%s/%d:%d", a0 == 0xc ? "char" : "block", (uint8_t)a1,
            (uint8_t)a2);
    return open(buf, O_RDWR, 0);
  } else {
    char buf[1024];
    char* hash;
    NONFAILING(strncpy(buf, (char*)a0, sizeof(buf)));
    buf[sizeof(buf) - 1] = 0;
    while ((hash = strchr(buf, '#'))) {
      *hash = '0' + (char)(a1 % 10);
      a1 /= 10;
    }
    return open(buf, a2, 0);
  }
}

static bool write_file(const char* file, const char* what, ...)
{
  char buf[1024];
  va_list args;
  va_start(args, what);
  vsnprintf(buf, sizeof(buf), what, args);
  va_end(args);
  buf[sizeof(buf) - 1] = 0;
  int len = strlen(buf);

  int fd = open(file, O_WRONLY | O_CLOEXEC);
  if (fd == -1)
    return false;
  if (write(fd, buf, len) != len) {
    int err = errno;
    close(fd);
    errno = err;
    return false;
  }
  close(fd);
  return true;
}

static void loop();

static void sandbox_common()
{
  prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
  setpgrp();
  setsid();

  struct rlimit rlim;
  rlim.rlim_cur = rlim.rlim_max = 160 << 20;
  setrlimit(RLIMIT_AS, &rlim);
  rlim.rlim_cur = rlim.rlim_max = 8 << 20;
  setrlimit(RLIMIT_MEMLOCK, &rlim);
  rlim.rlim_cur = rlim.rlim_max = 136 << 20;
  setrlimit(RLIMIT_FSIZE, &rlim);
  rlim.rlim_cur = rlim.rlim_max = 1 << 20;
  setrlimit(RLIMIT_STACK, &rlim);
  rlim.rlim_cur = rlim.rlim_max = 0;
  setrlimit(RLIMIT_CORE, &rlim);

  if (unshare(CLONE_NEWNS)) {
    debug("unshare(CLONE_NEWNS): %d\n", errno);
  }
  if (unshare(CLONE_NEWIPC)) {
    debug("unshare(CLONE_NEWIPC): %d\n", errno);
  }
  if (unshare(0x02000000)) {
    debug("unshare(CLONE_NEWCGROUP): %d\n", errno);
  }
  if (unshare(CLONE_NEWUTS)) {
    debug("unshare(CLONE_NEWUTS): %d\n", errno);
  }
  if (unshare(CLONE_SYSVSEM)) {
    debug("unshare(CLONE_SYSVSEM): %d\n", errno);
  }
}

static int real_uid;
static int real_gid;
__attribute__((aligned(64 << 10))) static char sandbox_stack[1 << 20];

static int namespace_sandbox_proc(void* arg)
{
  sandbox_common();

  write_file("/proc/self/setgroups", "deny");
  if (!write_file("/proc/self/uid_map", "0 %d 1\n", real_uid))
    fail("write of /proc/self/uid_map failed");
  if (!write_file("/proc/self/gid_map", "0 %d 1\n", real_gid))
    fail("write of /proc/self/gid_map failed");

  if (unshare(CLONE_NEWNET))
    fail("unshare(CLONE_NEWNET)");

  mkdir("./syz-tmp", 0777);

  if (mount("", "./syz-tmp", "tmpfs", 0, NULL))
    fail("mount(tmpfs) failed");
  if (mkdir("./syz-tmp/newroot", 0777))
    fail("mkdir failed");
  if (mkdir("./syz-tmp/newroot/dev", 0700))
    fail("mkdir failed");
  unsigned mount_flags = MS_BIND | MS_REC | MS_PRIVATE;
  if (mount("/dev", "./syz-tmp/newroot/dev", NULL, mount_flags, NULL))
    fail("mount(dev) failed");
  if (mkdir("./syz-tmp/newroot/proc", 0700))
    fail("mkdir failed");
  if (mount(NULL, "./syz-tmp/newroot/proc", "proc", 0, NULL))
    fail("mount(proc) failed");
  if (mkdir("./syz-tmp/newroot/selinux", 0700))
    fail("mkdir failed");
  const char* selinux_path = "./syz-tmp/newroot/selinux";
  if (mount("/selinux", selinux_path, NULL, mount_flags, NULL)) {
    if (errno != ENOENT)
      fail("mount(/selinux) failed");
    if (mount("/sys/fs/selinux", selinux_path, NULL, mount_flags, NULL) &&
        errno != ENOENT)
      fail("mount(/sys/fs/selinux) failed");
  }
  if (mkdir("./syz-tmp/newroot/sys", 0700))
    fail("mkdir failed");
  if (mount(NULL, "./syz-tmp/newroot/sys", "sysfs", 0, NULL))
    fail("mount(sysfs) failed");
  if (mkdir("./syz-tmp/pivot", 0777))
    fail("mkdir failed");
  if (syscall(SYS_pivot_root, "./syz-tmp", "./syz-tmp/pivot")) {
    debug("pivot_root failed\n");
    if (chdir("./syz-tmp"))
      fail("chdir failed");
  } else {
    debug("pivot_root OK\n");
    if (chdir("/"))
      fail("chdir failed");
    if (umount2("./pivot", MNT_DETACH))
      fail("umount failed");
  }
  if (chroot("./newroot"))
    fail("chroot failed");
  if (chdir("/"))
    fail("chdir failed");

  struct __user_cap_header_struct cap_hdr = {};
  struct __user_cap_data_struct cap_data[2] = {};
  cap_hdr.version = _LINUX_CAPABILITY_VERSION_3;
  cap_hdr.pid = getpid();
  if (syscall(SYS_capget, &cap_hdr, &cap_data))
    fail("capget failed");
  cap_data[0].effective &= ~(1 << CAP_SYS_PTRACE);
  cap_data[0].permitted &= ~(1 << CAP_SYS_PTRACE);
  cap_data[0].inheritable &= ~(1 << CAP_SYS_PTRACE);
  if (syscall(SYS_capset, &cap_hdr, &cap_data))
    fail("capset failed");

  loop();
  doexit(1);
}

static int do_sandbox_namespace(void)
{
  int pid;

  real_uid = getuid();
  real_gid = getgid();
  mprotect(sandbox_stack, 4096, PROT_NONE);
  pid =
      clone(namespace_sandbox_proc, &sandbox_stack[sizeof(sandbox_stack) - 64],
            CLONE_NEWUSER | CLONE_NEWPID, 0);
  if (pid < 0)
    fail("sandbox clone failed");
  return pid;
}

static void execute(int num_repeat)
{
  int kNumThreads = 2;
  long tid;
  pthread_t th[kNumThreads];
  pthread_attr_t attr;
  cpu_set_t cpus;

  pthread_attr_init(&attr);
  pthread_barrier_init(&race_barrier, NULL, kNumThreads);

  debug("execute pid (%d)  \n", getpid());
  srand(time(0));

  for (tid = 0; tid < kNumThreads; tid++) {
    CPU_ZERO(&cpus);
    CPU_SET(tid, &cpus);

    pthread_create(&th[tid], &attr, execute_thr, (void*)tid);
  }

  for (tid = 0; tid < kNumThreads; tid++) {
    pthread_create(&th[2 + tid], 0, execute_thr, (void*)tid);
    if (rand() % 2)
      usleep(rand() % 10000);
  }

  for (tid = 0; tid < kNumThreads; tid++) {
    pthread_join(th[tid], 0);
  }
}

uint64_t r[4] = {0xffffffffffffffff, 0xffffffffffffffff, 0x0, 0x0};

void race_hcall_prepare(long);
void race_hcall_setup(long, long);
void race_hcall_mark_and_check(long);

void* execute_thr(void* arg)
{
  static int count = 0;
  uint64_t tid = (uint64_t)arg;
  int res = 0;
  uint64_t turn = 0;

  while (1) {
    if (count++ % 100 == 0) {
      debug(".");
    }

    turn = (count + tid) % 2;
    race_hcall_prepare(tid);
    if (tid == 0) {
    }
    if (tid == 1) {
    }

    race_hcall_setup(tid, 0);
    pthread_barrier_wait(&race_barrier);

    switch (tid) {
    case 0:
    case 2:
      NONFAILING(memcpy((void*)0x20000040, "/dev/snd/midiC#D#", 18));
      res = syz_open_dev(0x20000040, 2, 2);
      if (res != -1)
        r[0] = res;
      break;
    case 1:
    case 3:
      NONFAILING(memcpy((void*)0x20000100, "#! ", 3));
      NONFAILING(memcpy((void*)0x20000103, "./file0", 7));
      NONFAILING(*(uint8_t*)0x2000010a = 0xa);
      syscall(__NR_write, r[0], 0x20000100, 0x2000010b);
      break;
    }

    race_hcall_mark_and_check(tid);
    if (turn == 1) {
      NONFAILING(*(uint32_t*)0x20000000 = r[0]);
      NONFAILING(*(uint16_t*)0x20000004 = 0x8020);
      NONFAILING(*(uint16_t*)0x20000006 = 0);
      NONFAILING(*(uint32_t*)0x20000008 = r[0]);
      NONFAILING(*(uint16_t*)0x2000000c = 0x41);
      NONFAILING(*(uint16_t*)0x2000000e = 0);
      NONFAILING(*(uint32_t*)0x20000010 = r[0]);
      NONFAILING(*(uint16_t*)0x20000014 = 0x80);
      NONFAILING(*(uint16_t*)0x20000016 = 0);
      NONFAILING(*(uint32_t*)0x20000018 = r[0]);
      NONFAILING(*(uint16_t*)0x2000001c = 0);
      NONFAILING(*(uint16_t*)0x2000001e = 0);
      NONFAILING(*(uint32_t*)0x20000020 = r[0]);
      NONFAILING(*(uint16_t*)0x20000024 = 0);
      NONFAILING(*(uint16_t*)0x20000026 = 0);
      NONFAILING(*(uint64_t*)0x20000080 = 0);
      NONFAILING(*(uint64_t*)0x20000088 = 0);
      NONFAILING(*(uint64_t*)0x200000c0 = 2);
      syscall(__NR_ppoll, 0x20000000, 5, 0x20000080, 0x200000c0, 8);
      NONFAILING(memcpy((void*)0x20000140, "/dev/sequencer", 15));
      res = syscall(__NR_openat, 0xffffffffffffff9c, 0x20000140, 0x101000, 0);
      if (res != -1)
        r[1] = res;
      res = syscall(__NR_clock_gettime, 0, 0x200001c0);
      if (res != -1) {
        NONFAILING(r[2] = *(uint64_t*)0x200001c0);
        NONFAILING(r[3] = *(uint64_t*)0x200001c8);
      }
      NONFAILING(*(uint64_t*)0x20000240 = 0x20ffd000);
      NONFAILING(*(uint64_t*)0x20000248 = 0x3000);
      NONFAILING(*(uint64_t*)0x20000250 = 1);
      NONFAILING(*(uint64_t*)0x20000258 = 0);
      syscall(__NR_ioctl, r[1], 0xc020aa04, 0x20000240);
      NONFAILING(*(uint64_t*)0x20000200 = r[2]);
      NONFAILING(*(uint64_t*)0x20000208 = r[3] + 10000000);
      syscall(__NR_mq_timedreceive, r[1], 0x20000180, 6, 0x20000000000,
              0x20000200);
    }

    pthread_barrier_wait(&race_barrier);

    if (rand() % 1000 <= 1) {
      debug("Exiting\n", count);
      exit(0);
    }
    if (should_hypercall)
      break;
  }
  return 0;
}

void loop()
{
  debug("loop\n");
  execute(100);
}

int main(int argc, char* argv[])
{
  syscall(__NR_mmap, 0x20000000, 0x1000000, 3, 0x32, -1, 0);
  install_segv_handler();
  bps[0] = 0x817e39e6;
  bps[1] = 0x8184a901;
  if (argc > 1)
    should_hypercall = true;
  if (should_hypercall) {
    debug("[*] hypercall enabled: pid (%d)\n", getpid());
  } else {
    debug("[*] hypercall disabled: pid (%d)\n", getpid());
  }
  use_temporary_dir();
  for (;;) {
    int pid = do_sandbox_namespace();
    int status = 0;
    while (waitpid(pid, &status, __WALL) != pid) {
    }
  }
  return 0;
}
