#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <sys/syscall.h>
#include <pthread.h>

#define gettid() syscall(__NR_gettid)

#define SCHED_DEADLINE 6

/* use the proper syscall numbers */
#ifdef __x86_64__
#define __NR_sched_setattr             314
#define __NR_sched_getattr             315
#endif

#ifdef __i386__
#define __NR_sched_setattr             351
#define __NR_sched_getattr             352
#endif

#ifdef __arm__
#define __NR_sched_setattr             380
#define __NR_sched_getattr             381
#endif

static volatile int done;

struct sched_attr {
       __u32 size;

       __u32 sched_policy;
       __u64 sched_flags;

       /* SCHED_NORMAL, SCHED_BATCH */
       __s32 sched_nice;

       /* SCHED_FIFO, SCHED_RR */
       __u32 sched_priority;

       /* SCHED_DEADLINE (nsec) */
       __u64 sched_runtime;
       __u64 sched_deadline;
       __u64 sched_period;
};

static int sched_setattr(pid_t pid,
                 const struct sched_attr *attr,
                 unsigned int flags)
{
       return syscall(__NR_sched_setattr, pid, attr, flags);
}

static int sched_getattr(pid_t pid,
                 struct sched_attr *attr,
                 unsigned int size,
                 unsigned int flags)
{
       return syscall(__NR_sched_getattr, pid, attr, size, flags);
}

static void set_affinity(int runtime, int period)
{
        struct sched_attr attr;
        int ret;
        unsigned flags = 0;

        uint64_t mask;

        mask = 0xFFFFFFFFFFFF;
        ret = sched_setaffinity(0, sizeof(mask), (cpu_set_t*)&mask);
        if (ret != 0) rte_exit(EXIT_FAILURE, "Error: cannot set affinity. Quitting...\n");

        printf("deadline thread started [%ld]\n", gettid());

        attr.size = sizeof(attr);

        attr.sched_flags = 0;
        attr.sched_nice = 0;
        attr.sched_priority = 0;

        attr.sched_policy = SCHED_DEADLINE;
        attr.sched_runtime = runtime * 1000;
        attr.sched_period = period * 1000;
	attr.sched_deadline = period * 1000;

        ret = sched_setattr(0, &attr, flags);
        if (ret < 0) {
                done = 0;
                perror("sched_setattr");
                exit(-1);
        }
}
