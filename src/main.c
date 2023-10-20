#include "stdio.h"
// #include <x86intrin.h> /* for rdtsc, rdtscp, clflush */
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <getopt.h>
#include <errno.h>
#include <stdint.h>

#include "include/utils.h"
#include "include/types.h"
#include "include/allocator.h"
#include "include/memory.h"
#include "include/dram-address.h"
#include "include/hammer-suite.h"
#include "include/params.h"


ProfileParams *p;

//DRAMLayout      g_mem_layout = {{{0x2040,0x24000,0x48000,0x90000}, 4}, 0x3fffe0000, ROW_SIZE-1}; //1R 8G x8 1CH
DRAMLayout      g_mem_layout = {{{0x4080,0x48000,0x90000,0x120000,0xc3300}, 5}, 0x1fffc0000, ROW_SIZE-1};  // 1R 8G x8 2CH

int main(int argc, char **argv)
{
	srand(time(NULL));
	p = (ProfileParams*)malloc(sizeof(ProfileParams));
	if (p == NULL) {
		fprintf(stderr, "[ERROR] Memory allocation\n");
		exit(1);
	}

	if(process_argv(argc, argv, p) == -1) {
		free(p);
		exit(1);
	}
	assert(USE_1GB ^ USE_THP);

	MemoryBuffer mem = {
		.buffer = (char **)malloc(sizeof(char *) * NUM_PAGES),
		.physmap = NULL,
		.fd = p->huge_fd,

#if USE_1GB
		.size = p->m_size,
#endif
#if USE_THP
		.size = HUGE_SIZE * NUM_PAGES,
#endif
		.align = p->m_align,
		.flags = p->g_flags & MEM_MASK
	};

	alloc_buffer(&mem);
	set_physmap(&mem);

	SessionConfig s_cfg;
	memset(&s_cfg, 0, sizeof(SessionConfig));

	s_cfg.h_rows = PATT_LEN;
	s_cfg.h_rounds = p->rounds;
	s_cfg.h_cfg = N_SIDED;
	s_cfg.d_cfg = FILL_FF ? ONE_TO_ZERO : ZERO_TO_ONE;
	s_cfg.base_off = p->base_off;
	s_cfg.aggr_n = p->aggr;
	s_cfg.dist = p->dist;
	s_cfg.vics = p->vics;

#if USE_THP
	mem_check(&s_cfg, &mem);
#endif
#if USE_1GB
	mem_check_1GB(&s_cfg, &mem);
#endif

	return 0;
}
