/* vi: set sw=4 ts=4:
 *
 * taskset.c - Retrieve or set the CPU affinity of a process.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>
 *
 * No standard.

USE_TASKSET(NEWTOY(taskset, "<1pa", TOYFLAG_BIN|TOYFLAG_NEEDROOT))

config TASKSET
	bool "taskset"
	default y
	help
	  usage: taskset [-ap] [mask] [PID|cmd [args...]]

	  When mask is present the CPU affinity mask of a given PID will
	  be set to this mask. When a mask is not given, the mask will
	  be printed. A mask is a hexadecimal string where the bit position
	  matches the cpu number.
	  -a	Set/get the affinity of all tasks of a PID.
	  -p	Set/get the affinity of given PID instead of a new command.
*/

#define _GNU_SOURCE
#include "toys.h"

#define A_FLAG 0x1
#define P_FLAG 0x2

static int str_to_cpu_set(char * mask, cpu_set_t *set)
{
	int size = strlen(mask);
	char *ptr = mask + size - 1;
	int cpu = 0;

	CPU_ZERO(set);
	if (size > 1 && mask[0] == '0' && mask[1] == 'x') mask += 2;

	while(ptr >= mask) {
		char val = 0;

		if ( *ptr >= '0' && *ptr <= '9') val = *ptr - '0';
		else if (*ptr >= 'a' && *ptr <= 'f') val = 10 + (*ptr - 'a');
		else return -1;

		if (val & 1) CPU_SET(cpu,  set);
		if (val & 2) CPU_SET(cpu + 1, set);
		if (val & 4) CPU_SET(cpu + 2, set);
		if (val & 8) CPU_SET(cpu + 3, set);

		ptr--;
		cpu += 4;
	}
	return 0;
}

static char * cpu_set_to_str(cpu_set_t *set)
{
	int cpu;
	char *ptr = toybuf;

	for (cpu = (8*sizeof(cpu_set_t) - 4); cpu >= 0; cpu -= 4) {
		char val = 0;

		if (CPU_ISSET(cpu, set))	 val |= 1;
		if (CPU_ISSET(cpu + 1, set)) val |= 2;
		if (CPU_ISSET(cpu + 2, set)) val |= 4;
		if (CPU_ISSET(cpu + 3, set)) val |= 8;
		if (ptr != toybuf || val != 0) {
			if (val < 10) *ptr = '0' + val;
			else *ptr = 'a' + (val - 10);
			ptr++;
		}
	}
	*ptr = 0;
	return toybuf;
}

static void do_taskset(pid_t pid, int quiet)
{
	cpu_set_t mask;

	if (!pid) return;
	if (sched_getaffinity(pid, sizeof(mask), &mask))
		perror_exit("failed to get %d's affinity", pid);

	if (!quiet) printf("pid %d's current affinity mask: %s\n", pid, cpu_set_to_str(&mask));

	if (toys.optc >= 2)
	{
		if (str_to_cpu_set(toys.optargs[0], &mask))
			perror_exit("bad mask: %s", toys.optargs[0]);

		if (sched_setaffinity(pid, sizeof(mask), &mask))
			perror_exit("failed to set %d's affinity", pid);

		if (sched_getaffinity(pid, sizeof(mask), &mask))
			perror_exit("failed to get %d's affinity", pid);

		if (!quiet) printf("pid %d's new affinity mask: %s\n", pid, cpu_set_to_str(&mask));
	}
}

static int task_cb(struct dirtree *new)
{
	if (!new->parent) return DIRTREE_RECURSE;
	if (S_ISDIR(new->st.st_mode) && *new->name != '.')
			do_taskset(atoi(new->name), 0);

	return 0;
}

void taskset_main(void)
{
	char *pidstr = (toys.optc==1) ? toys.optargs[0] : toys.optargs[1];

	if (!(toys.optflags & P_FLAG)) {
		if (toys.optc >= 2) {
			do_taskset(getpid(),1);
			xexec(&toys.optargs[1]);
		} else error_exit("Needs at least a mask and a command");
	}

	if (toys.optflags & A_FLAG) {
		sprintf(toybuf, "/proc/%s/task/", pidstr);
		dirtree_read(toybuf, task_cb);
	} else do_taskset(atoi(pidstr), 0);
}
