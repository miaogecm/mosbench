/*
 * Operate (XOP) on per-process directories.
 */

#define TESTNAME "creato"

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <pthread.h>

#include "bench.h"
#include "support/mtrace.h"
#include "forp.h"
#include "gemaphore.h"

#define NPMC 3

#define MAX_PROC 256

#define XOP xcreat

static uint64_t start;

static unsigned int ncores;
static unsigned int nprocs;
static unsigned int the_time;
static unsigned int close_fd;
static char *prefix;

static uint64_t pmc_start[NPMC];
static uint64_t pmc_stop[NPMC];

static struct {
	union __attribute__((__aligned__(64))){
		volatile uint64_t v;
		char pad[64];
	} count[MAX_PROC];
	volatile int run;
	struct gemaphore gema;
} *shared;

static inline void xcreat(const char *fn)
{
	int fd;

	fd = creat(fn, S_IRUSR|S_IWUSR);
	if (fd < 0)
		edie("creat");
	close(fd);
	if (unlink(fn))
		edie("unlink");
}

static void sighandler(int x)
{
	struct mtrace_appdata_entry entry;
	float sec, rate, one;
	uint64_t stop, tot;
	unsigned int i;

	tot = 0;
	for (i = 0; i < nprocs; i++)
		tot += shared->count[i].v;

	entry.u64 = tot;
	mtrace_appdata_register(&entry);

	forp_stop();
	mtrace_enable_set(0, TESTNAME);

	for (i = 0; i < NPMC; i++)
		pmc_stop[i] = read_pmc(i);

	stop = usec();
	shared->run = 0;

	tot = 0;
	for (i = 0; i < nprocs; i++)
		tot += shared->count[i].v;

	sec = (float)(stop - start) / 1000000;
	rate = (float)tot / sec;
	one = (float)(stop - start) / (float)tot;

	printf("rate: %f per sec\n", rate);
	printf("lat: %f usec\n", one);

	for (i = 0; i < NPMC; i++) {
		rate = (float)(pmc_stop[i] - pmc_start[i]) / 
			(float) shared->count[0].v;
		printf("pmc(%u): %f per op\n", i, rate);
	}
}

static void test(unsigned int proc)
{
	char fn[128];
	
	setaffinity(proc % ncores);

	snprintf(fn, sizeof(fn), "%s.%d/x", prefix, proc);

	if (proc == 0) {
		unsigned int i;

		gemaphore_p(&shared->gema);

		if (signal(SIGALRM, sighandler) == SIG_ERR)
			die("signal failed\n");
		alarm(the_time);
		start = usec();

		for (i = 0; i < NPMC; i++)
			pmc_start[i] = read_pmc(i);

		forp_reset();
		mtrace_enable_set(1, TESTNAME);
		shared->run = 1;
	} else {
		gemaphore_v(&shared->gema);
		while (shared->run == 0)
			nop_pause();
	}

	while (shared->run) {
		XOP(fn);
		shared->count[proc].v++;
	}
}

static void initshared(void)
{
	shared = mmap(0, sizeof(*shared), PROT_READ|PROT_WRITE, 
		      MAP_SHARED|MAP_ANONYMOUS, 0, 0);
	if (shared == MAP_FAILED)
		die("mmap failed");
	gemaphore_init(&shared->gema, nprocs - 1);
}

static void initfile(void)
{
	unsigned int i;
	char sys[128];
	char dn[128];

	for (i = 0; i < nprocs; i++) {
		setaffinity(i % ncores);

		snprintf(dn, sizeof(dn), "%s.%d", prefix, i);
		snprintf(sys, sizeof(sys), "rm -rf %s", dn);
		system(sys);

		if (mkdir(dn, S_IRWXU) < 0)
			edie("mkdir");
	}
}

int main(int ac, char **av)
{
	unsigned int i;

	if (ac < 4)
		die("usage: %s time nprocs base-filename [close]", av[0]);

	the_time = atoi(av[1]);
	nprocs = atoi(av[2]);
	prefix = av[3];
	ncores = sysconf(_SC_NPROCESSORS_CONF);

	if (ac > 4)
		close_fd = atoi(av[4]);

	initshared();
	initfile();

	for (i = 1; i < nprocs; i++) {
		pid_t p;

		p = fork();
		if (p < 0)
			edie("fork");
		else if (p == 0) {
			test(i);
			return 0;
		}
	}

	test(0);
	return 0;
}