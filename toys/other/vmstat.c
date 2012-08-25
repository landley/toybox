/* vi: set sw=4 ts=4:
 *
 * vmstat.c - Report virtual memory statistics.
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_VMSTAT(NEWTOY(vmstat, ">2n", TOYFLAG_BIN))

config VMSTAT
	bool "vmstat"
	default y
	help
	  usage: vmstat [-n] [delay [count]]
	  -n Display the header only once
	  delay The delay between updates in seconds, when not specified
	        the average since boot is displayed.
	  count Number of updates to display, the default is inifinite.
*/

#include "toys.h"

void read_proc_stat(unsigned int * proc_running, unsigned int * proc_blocked,
		uint64_t * sys_irq, uint64_t * sys_ctxt,
		uint64_t * cpu_user, uint64_t * cpu_sys, uint64_t * cpu_idle, uint64_t * cpu_wait)
{
	char * off;
	uint64_t c_user, c_nice, c_sys, c_irq, c_sirq;
	int fd = xopen("/proc/stat", O_RDONLY);
	size_t s = xread(fd, toybuf, sizeof(toybuf)-1);
	toybuf[s] = 0;
	if ( s == sizeof(toybuf)-1)
		error_exit("/proc/stat is too large");

	off = strstr(toybuf, "cpu ");
	// Ignoring steal and guest fields for now.
	if (off) sscanf(off, "cpu  %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64 \
		" %"PRIu64" %"PRIu64" %"PRIu64, &c_user, &c_nice, &c_sys, cpu_idle,
		cpu_wait, &c_irq, &c_sirq);
	*cpu_user = c_user + c_nice;
	*cpu_sys = c_sys + c_irq + c_sirq;
	off = strstr(toybuf, "intr");
	if (off) sscanf(off, "intr %"PRIu64, sys_irq);

	off = strstr(toybuf, "ctxt");
	if (off) sscanf(off, "ctxt %"PRIu64, sys_ctxt);

	off = strstr(toybuf, "procs_running");
	if (off) sscanf(off, "procs_running %u", proc_running);
	(*proc_running)--; // look, i'm invisible.

	off = strstr(toybuf, "procs_blocked");
	if (off) sscanf(off, "procs_blocked %u", proc_blocked);

	close(fd);
}

void read_proc_meminfo(unsigned long * mem_swapped, unsigned long * mem_free,
			unsigned long * mem_buff, unsigned long * mem_cache)
{
	char * off;
	unsigned long swap_total, swap_free;
	int fd = xopen("/proc/meminfo", O_RDONLY);
	size_t s = xread(fd, toybuf, sizeof(toybuf)-1);
	toybuf[s] = 0;
	if ( s == sizeof(toybuf)-1)
		error_exit("/proc/meminfo is too large");

	off = strstr(toybuf, "MemFree");
	if (off) sscanf(off, "MemFree: %lu kB", mem_free);

	off = strstr(toybuf, "Buffers");
	if (off) sscanf(off, "Buffers: %lu kB", mem_buff);

	off = strstr(toybuf, "Cached");
	if (off) sscanf(off, "Cached: %lu kB", mem_cache);

	off = strstr(toybuf, "SwapFree");
	if (off) sscanf(off, "SwapFree: %lu kB", &swap_free); 

	off = strstr(toybuf, "SwapTotal");
	if (off) sscanf(off, "SwapTotal: %lu kB", &swap_total);
	*mem_swapped = swap_total - swap_free;

	close(fd);
}

void read_proc_vmstat(unsigned long * io_pages_in, unsigned long * io_pages_out,
			unsigned long * swap_bytes_in, unsigned long * swap_bytes_out)
{
	char * off;
	unsigned long s_pages_in, s_pages_out;
	unsigned long pagesize_kb = sysconf(_SC_PAGESIZE) / 1024L;
	int fd = xopen("/proc/vmstat", O_RDONLY);
	size_t s = xread(fd, toybuf, sizeof(toybuf)-1);
	toybuf[s] = 0;
	if ( s == sizeof(toybuf)-1)
		error_exit("/proc/vmstat is too large");

	off = strstr(toybuf, "pgpgin");
	if (off) sscanf(off, "pgpgin %lu", io_pages_in);

	off = strstr(toybuf, "pgpgout");
	if (off) sscanf(off, "pgpgout %lu", io_pages_out);

	off = strstr(toybuf, "pswpin");
	if (off) sscanf(off, "pswpin %lu", &s_pages_in);
	*swap_bytes_in = s_pages_in * pagesize_kb;

	off = strstr(toybuf, "pswpout");
	if (off) sscanf(off, "pswpout %lu", &s_pages_out);
	*swap_bytes_out = s_pages_out * pagesize_kb;

	close(fd);
}

void vmstat_main(void)
{
	const char fmt[] = "%2u %2u %6lu %6lu %6lu %6lu %4u %4u %5u %5u %4u %4u %2u %2u %2u %2u\n";
	unsigned int loop_num = 0, loop_max_num = 0, loop_delay = 0;
	unsigned int running = 0, blocked = 0;
	unsigned long mem_swap = 0, mem_free = 0, mem_buff = 0, mem_cache = 0;
	unsigned long io_pages_in[2], io_pages_out[2], swap_bytes_in[2], swap_bytes_out[2];
	uint64_t sys_irq[2], sys_ctxt[2], cpu_user[2], cpu_sys[2], cpu_idle[2], cpu_wait[2];
	int first_run = 1;
	int no_header = toys.optflags & 0x1;
	unsigned num_rows = 22;

	if (toys.optc >= 1)
		loop_delay = atoi(toys.optargs[0]);
	if (toys.optc >= 2)
		loop_max_num = atoi(toys.optargs[1]);

	if (loop_max_num < 0 || loop_delay < 0)
		error_exit("Invalid arguments");

	while(1) {
		uint64_t total_jif;
		int idx = loop_num%2;

		if(first_run || (!(loop_num % num_rows) && !no_header)) {
			unsigned rows = 0, cols = 0;
			terminal_size(&cols, &rows);
			num_rows = (rows > 3)? rows - 3 : 22;
			printf("procs -----------memory---------- ---swap-- -----io---- -system-- ----cpu----\n");
			printf(" r  b   swpd   free   buff  cache   si   so	bi	bo   in   cs us sy id wa\n");
		}

		read_proc_stat(&running, &blocked, &sys_irq[idx], &sys_ctxt[idx], &cpu_user[idx],
					   &cpu_sys[idx], &cpu_idle[idx], &cpu_wait[idx]);
		read_proc_meminfo(&mem_swap, &mem_free, &mem_buff, &mem_cache);
		read_proc_vmstat(&io_pages_in[idx], &io_pages_out[idx], &swap_bytes_in[idx], &swap_bytes_out[idx]);

		if (first_run) {
			struct sysinfo inf;
			sysinfo(&inf);
			first_run = 0;
			total_jif = cpu_user[idx] + cpu_idle[idx] + cpu_wait[idx];
			printf(fmt, running, blocked, mem_swap, mem_free, mem_buff, mem_cache,
				   (unsigned) (swap_bytes_in[idx]/inf.uptime),
				   (unsigned) (swap_bytes_out[idx]/inf.uptime),
				   (unsigned) (io_pages_in[idx]/inf.uptime),
				   (unsigned) (io_pages_out[idx]/inf.uptime),
				   (unsigned) (sys_irq[idx]/inf.uptime),
				   (unsigned) (sys_ctxt[idx]/inf.uptime),
				   (unsigned) (100*cpu_user[idx]/total_jif),
				   (unsigned) (100*cpu_sys[idx]/total_jif),
				   (unsigned) (100*cpu_idle[idx]/total_jif),
				   (unsigned) (100*cpu_wait[idx]/total_jif));
		}else{
			total_jif = cpu_user[idx] - cpu_user[!idx] + cpu_idle[idx] - cpu_idle[!idx] + cpu_wait[idx] - cpu_wait[!idx];
			printf(fmt, running, blocked, mem_swap, mem_free, mem_buff, mem_cache,
				   (unsigned) ((swap_bytes_in[idx] - swap_bytes_in[!idx])/loop_delay),
				   (unsigned) ((swap_bytes_out[idx] - swap_bytes_out[!idx])/loop_delay),
				   (unsigned) ((io_pages_in[idx] - io_pages_in[!idx])/loop_delay),
				   (unsigned) ((io_pages_out[idx] - io_pages_out[!idx])/loop_delay),
				   (unsigned) ((sys_irq[idx] - sys_irq[!idx])/loop_delay),
				   (unsigned) ((sys_ctxt[idx] - sys_ctxt[!idx])/loop_delay),
				   (unsigned) (100*(cpu_user[idx] - cpu_user[!idx])/total_jif),
				   (unsigned) (100*(cpu_sys[idx]  - cpu_sys[!idx]) /total_jif),
				   (unsigned) (100*(cpu_idle[idx] - cpu_idle[!idx])/total_jif),
				   (unsigned) (100*(cpu_wait[idx] - cpu_wait[!idx])/total_jif));
		}

		loop_num++;
		if (loop_delay == 0 || (loop_max_num != 0 && loop_num >= loop_max_num))
			break;
		sleep(loop_delay);
	}
}
