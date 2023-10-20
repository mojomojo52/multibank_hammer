/*
 * Copyright (c) 2018 Vrije Universiteit Amsterdam
 * Copyright (c) 2023 Ingab Kang
 *
 * This program is licensed under the GPL2+.
 */

#ifndef PARAMS_H
#define PARAMS_H 1

#include <stddef.h>
#include <stdint.h>

#define TRUE 1
#define FALSE 0
#define ROUNDS_std      540000 //x10
#define HUGETLB_std     "/mnt/huge/buff"
#define CONFIG_NAME_std "tmp/s_cfg.bin"
#define O_FILE_std      ""
#define ALLOC_SIZE     	(1<<30)
#define HUGE_SIZE		(1<<21)
#define ALIGN_std       1 << 21
#define PATT_LEN 		4096
#define AGGR_std		10
#define USE_1GB FALSE
#define USE_THP TRUE

#if USE_1GB
#define NUM_PAGES   1	
#endif

#if USE_THP
#define NUM_PAGES	512
#endif

#define POSIX_ALIGN (1<<23)

#define NUM_TARGETS 2000
#define CACHE_WAYS 16
#define NUM_EVICT_CYCLES 1
#define CACHE_COLORS 8
#define MAX_GROUP_SIZE ((NUM_PAGES / CACHE_COLORS) * 2)
#define STRIDE (1UL << 6)

#define MAX_HP_ROWS 8

#define ARRAY

// uncomment to do single-sided rowhammer
#define SINGLE_SIDED FALSE
// controls whether to use self-eviction or not. 
// changes whether the rows are fully random or if they are chosen to be in the same cache set
#define USE_EVSET FALSE  
#define CHECK_DRAM_2_VIRT TRUE
#define FILL_FF FALSE

typedef enum {
	FUZZ,
	MULT_BANK,
	GROUP_HAMMER,
	N_SIDED_HAMMER
} HammerSession;

typedef struct ProfileParams {
	uint64_t g_flags 		= 0;
	char 	*g_out_prefix;
	char	*tpat			= (char *)NULL;
	char 	*vpat			= (char *)NULL;
	int		 threshold		= 0;

	HammerSession hsess 	= N_SIDED_HAMMER;
	int		find_vic		= 0;
	int		dist			= -1;
	int		vics			= -1;
	size_t   m_size			= ALLOC_SIZE;
	size_t   m_align 		= ALIGN_std;
	size_t   rounds			= ROUNDS_std;
	size_t   base_off 		= 0;
	char     *huge_file		= (char *)HUGETLB_std;
	int		 huge_fd;
	char     *conf_file		= (char *)CONFIG_NAME_std;
	int 	 aggr			= AGGR_std;
	int      num_bks        = 16;
} ProfileParams;

int process_argv(int argc, char *argv[], ProfileParams *params);

#endif /* params.h */
