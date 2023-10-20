#include "include/hammer-suite.h"

#include "include/memory.h"
#include "include/utils.h"
#include "include/allocator.h"
#include "include/dram-address.h"
#include "include/addr-mapper.h"
#include "include/params.h"

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <limits.h>
#include <math.h>
#include <emmintrin.h>

#include <time.h>
#include <setjmp.h>

#define OUT_HEAD "f_og, f_new, vict_addr, aggr_addr\n"

#define ROW_FIELD 1
#define COL_FIELD 1 << 1
#define BK_FIELD 1 << 2
#define P_FIELD 1 << 3
#define ALL_FIELDS (ROW_FIELD | COL_FIELD | BK_FIELD)
#define FLIPTABLE

#define MERGE_AGGS true

#define SHADOW_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE)

#define NOP asm volatile("NOP" :: \
							 :);
#define NOP10 NOP NOP NOP NOP NOP NOP NOP NOP NOP NOP
#define NOP100 NOP10 NOP10 NOP10 NOP10 NOP10 NOP10 NOP10 NOP10 NOP10 NOP10
#define NOP1000 NOP100 NOP100 NOP100 NOP100 NOP100 NOP100 NOP100 NOP100 NOP100 NOP100

extern ProfileParams *p;

int g_bk;
FILE *out_fd = NULL;
static uint64_t CL_SEED = 0x7bc661612e71168c;

static inline __attribute((always_inline)) char *cl_rand_gen(DRAMAddr *d_addr)
{
	static uint64_t cl_buff[8];
	for (int i = 0; i < 8; i++)
	{
		cl_buff[i] =
			__builtin_ia32_crc32di(CL_SEED,
								   (d_addr->row + d_addr->bank +
									(d_addr->col + i * 8)));
	}
	return (char *)cl_buff;
}

typedef struct
{
	DRAMAddr *d_lst;
	physaddr_t *p_baselst;
	char **v_baselst;
	size_t len;
	size_t rounds;
	int num_banks;
	int fence_iter;
} HammerPattern;

typedef struct
{
	DRAMAddr d_vict;
	uint8_t f_og;
	uint8_t f_new;
	HammerPattern *h_patt;
} FlipVal;

typedef struct
{
	MemoryBuffer *mem;
	SessionConfig *cfg;
	DRAMAddr d_base;	// base address for hammering
	ADDRMapper *mapper; // dram mapper

	int (*hammer_test)(void *self);
} HammerSuite;

void create_dir(const char *dir_name)
{
	struct stat st = {0};
	if (stat(dir_name, &st) == -1)
	{
		mkdir(dir_name, 0777);
	}
}

void *create_output_fd(char *config, DRAMAddr d_base, SessionConfig *cfg)
{
	create_dir(DATA_DIR);
	char *out_name = (char *)malloc(500);
	char rows_str[10];
	strcpy(out_name, DATA_DIR);

	fprintf(stderr, "p->g_out_prefix: %s", p->g_out_prefix);

	if (!(p->g_out_prefix[0] == '\0'))
	{

		struct tm *date;
		const time_t t = time(NULL);
		date = localtime(&t);

		char yr[2];
		sprintf(yr, "%02d", (date->tm_year + 1900) % 100);
		strcat(out_name, yr);
		char mn[2];
		sprintf(mn, "%02d", (date->tm_mon + 1));
		strcat(out_name, mn);
		char dy[2];
		sprintf(dy, "%02d", (date->tm_mday));
		strcat(out_name, dy);
		strcat(out_name, ".");

		strcat(out_name, p->g_out_prefix);
		strcat(out_name, ".");
		strcat(out_name, config);
		strcat(out_name, ".");

		sprintf(rows_str, "%ld", cfg->h_rounds);
		strcat(out_name, rows_str);
	}
	else
	{
		strcat(out_name, "test");
	}
	strcat(out_name, ".csv");

	if (p->g_flags & F_NO_OVERWRITE)
	{
		int cnt = 0;
		char *tmp_name = (char *)malloc(500);
		strncpy(tmp_name, out_name, strlen(out_name));
		while (access(tmp_name, F_OK) != -1)
		{
			cnt++;
			sprintf(tmp_name, "%s.%02d", out_name, cnt);
		}
		strncpy(out_name, tmp_name, strlen(tmp_name));
		free(tmp_name);
	}

	fprintf(stderr, "[LOG] - File: %s\n", out_name);

	out_fd = fopen(out_name, "w+");
	assert(out_fd != NULL);

	free(out_name);
}

char *dAddr_2_str(DRAMAddr d_addr, uint8_t fields)
{
	static char ret_str[64];
	char tmp_str[10];
	bool first = true;
	memset(ret_str, 0x00, 64);
	if (fields & ROW_FIELD)
	{
		first = false;
		sprintf(tmp_str, "r%05ld", d_addr.row);
		strcat(ret_str, tmp_str);
	}
	if (fields & BK_FIELD)
	{
		if (!first)
		{
			strcat(ret_str, ".");
		}
		sprintf(tmp_str, "bk%02ld", d_addr.bank);
		strcat(ret_str, tmp_str);
		first = false;
	}
	if (fields & COL_FIELD)
	{
		if (!first)
		{
			strcat(ret_str, ".");
		}
		sprintf(tmp_str, "col%04ld", d_addr.col);
		strcat(ret_str, tmp_str);
		first = false;
	}
	return ret_str;
}

char *hPatt_2_str_thp(HammerPattern *h_patt, int fields)
{
	static char patt_str[10024];
	char *dAddr_str;

	memset(patt_str, 0x00, 1024);

	for (int i = 0; i < h_patt->len; i++)
	{
		char *agg_v = thp_dram_2_virt(h_patt->d_lst[i], h_patt->v_baselst[i]);
		virt_2_phys(h_patt->v_baselst[i]);
		DRAMAddr agg_d = phys_2_dram(virt_2_phys(agg_v));
		dAddr_str = dAddr_2_str(agg_d, fields);
		strcat(patt_str, dAddr_str);
		if (i + 1 != h_patt->len)
		{
			strcat(patt_str, "/");
		}
	}
	return patt_str;
}

char *hPatt_2_str_gb1(HammerPattern *h_patt, int fields)
{
	static char patt_str[10024];
	char *dAddr_str;

	memset(patt_str, 0x00, 1024);

	for (int i = 0; i < h_patt->len; i++)
	{
		char *agg_v = gb1_dram_2_virt(h_patt->d_lst[i], h_patt->v_baselst[i]);
		virt_2_phys(h_patt->v_baselst[i]);
		DRAMAddr agg_d = phys_2_dram(virt_2_phys(agg_v));
		dAddr_str = dAddr_2_str(agg_d, fields);
		strcat(patt_str, dAddr_str);
		if (i + 1 != h_patt->len)
		{
			strcat(patt_str, "/");
		}
	}
	return patt_str;
}

void print_start_attack(HammerPattern *h_patt)
{
	fprintf(out_fd, "%s : ", hPatt_2_str_thp(h_patt, ROW_FIELD | BK_FIELD));
	fflush(out_fd);
}

void print_start_attack_gb1(HammerPattern *h_patt)
{
	fprintf(out_fd, "%s : ", hPatt_2_str_gb1(h_patt, ROW_FIELD | BK_FIELD));
	fflush(out_fd);
}

void print_end_attack()
{
	fprintf(out_fd, "\n");
	fflush(out_fd);
}

void export_flip(FlipVal *flip)
{
	if (p->g_flags & F_VERBOSE)
	{
		fprintf(stdout, "[FLIP] - (%02x => %02x)\t vict: %s \n",
				flip->f_og, flip->f_new, dAddr_2_str(flip->d_vict, ALL_FIELDS));
		fflush(stdout);
	}

	fprintf(out_fd, "%02x,", flip->f_og);
	fflush(out_fd);
	fprintf(out_fd, "%02x,", flip->f_new);
	fflush(out_fd);
	fprintf(out_fd, "%s ", dAddr_2_str(flip->d_vict, ALL_FIELDS));
	fflush(out_fd);
}

int random_int(int min, int max)
{
	int number = min + rand() % (max - min);
	return number;
}

uint64_t hammer_thp(HammerPattern *patt, MemoryBuffer *mem, int skip_iter)
{
	fprintf(stderr, "thp                 %d: ", skip_iter);
	fprintf(out_fd, " thp                 %d: ", skip_iter);
	fflush(out_fd);
	char **v_lst = (char **)malloc(sizeof(char *) * patt->len);
	for (size_t i = 0; i < patt->len; i++)
	{
		v_lst[i] = thp_dram_2_virt(patt->d_lst[i], patt->v_baselst[i]);
	}

	sched_yield();
	// tries to sync hammer w/ beginning of refresh
	if (p->threshold > 0)
	{
		uint64_t t0 = 0, t1 = 0;
		// Threshold value depends on your system
		while (abs((int64_t)t1 - (int64_t)t0) < p->threshold)
		{
			t0 = rdtscp();
			*(volatile char *)v_lst[0];
			clflushopt(v_lst[0]);
			t1 = rdtscp();
		}
	}

	uint64_t cl0, cl1; // ns
	cl0 = realtime_now();

	for (int i = 0; i < patt->rounds; i++)
	{
		mfence();
		for (size_t j = 0; j < patt->len; j += 2)
		{
			*(volatile char *)v_lst[j];
			*(volatile char *)v_lst[j + 1];
		}
		for (size_t j = 0; j < patt->len; j += 2)
		{
			clflushopt(v_lst[j]);
			clflushopt(v_lst[j + 1]);
		}
	}
	cl1 = realtime_now();

	free(v_lst);
	// return (cl1-cl0) / 1000000; //ms
	return (cl1 - cl0); // ns
}

uint64_t hammer_gb1(HammerPattern *patt, MemoryBuffer *mem, int skip_iter)
{
	fprintf(stderr, "gb1   %d: ", skip_iter);
	fprintf(out_fd, " gb1   %d: ", skip_iter);
	fflush(out_fd);
	char **v_lst = (char **)malloc(sizeof(char *) * patt->len);
	for (size_t i = 0; i < patt->len; i++)
	{
		v_lst[i] = gb1_dram_2_virt(patt->d_lst[i], patt->v_baselst[i]);
	}

	sched_yield();
	// tries to sync hammer w/ beginning of refresh
	if (p->threshold > 0)
	{
		uint64_t t0 = 0, t1 = 0;
		// Threshold value depends on your system
		while (abs((int64_t)t1 - (int64_t)t0) < p->threshold)
		{
			t0 = rdtscp();
			*(volatile char *)v_lst[0];
			clflushopt(v_lst[0]);
			t1 = rdtscp();
		}
	}

	uint64_t cl0, cl1; // ns
	cl0 = realtime_now();

	for (int i = 0; i < patt->rounds; i++)
	{
		mfence();
		for (size_t j = 0; j < patt->len; j += 2)
		{
			*(volatile char *)v_lst[j];
			*(volatile char *)v_lst[j + 1];
		}
		for (size_t j = 0; j < patt->len; j += 2)
		{
			clflushopt(v_lst[j]);
			clflushopt(v_lst[j + 1]);
		}
	}
	cl1 = realtime_now();

	free(v_lst);
	return (cl1 - cl0); // ns
}

// DRAMAddr needs to be a copy in order to leave intact the original address

void fill_stripe_thp(DRAMAddr d_addr, char *base_v, uint8_t val, MemoryBuffer *mem)
{
	for (size_t col = 0; col < ROW_SIZE; col += CL_SIZE)
	{
		d_addr.col = col;
		char *tar_v = thp_dram_2_virt(d_addr, base_v);

		memset(tar_v, val, CL_SIZE);
		clflush(tar_v);
	}
}

void fill_row_thp(HammerSuite *suite, DRAMAddr *d_addr, char *v_addr, HammerData data_patt, bool reverse)
{

	if ((p->vpat != (void *)NULL) && (p->tpat != (void *)NULL))
	{
		uint8_t pat = reverse ? *p->vpat : *p->tpat;
		fill_stripe_thp(*d_addr, v_addr, pat, suite->mem);
		return;
	}

	if (reverse)
	{
		data_patt = (HammerData)((int)data_patt ^ (int)REVERSE);
	}

	switch (data_patt)
	{
	case RANDOM:
		//  rows are already filled for random data patt
		break;
	case ONE_TO_ZERO:
		fill_stripe_thp(*d_addr, v_addr, 0x00, suite->mem);
		break;
	case ZERO_TO_ONE:
		fill_stripe_thp(*d_addr, v_addr, 0xff, suite->mem);
		break;
	default:
		break;
	}
}

void fill_stripe_gb1(DRAMAddr d_addr, char *base_v, uint8_t val, MemoryBuffer *mem)
{
	for (size_t col = 0; col < ROW_SIZE; col += CL_SIZE)
	{
		d_addr.col = col;
		char *tar_v = gb1_dram_2_virt(d_addr, base_v);

		memset(tar_v, val, CL_SIZE);
		clflush(tar_v);
	}
}

void fill_row_gb1(HammerSuite *suite, DRAMAddr *d_addr, char *v_addr, HammerData data_patt, bool reverse)
{

	if ((p->vpat != (void *)NULL) && (p->tpat != (void *)NULL))
	{
		uint8_t pat = reverse ? *p->vpat : *p->tpat;
		fill_stripe_gb1(*d_addr, v_addr, pat, suite->mem);
		return;
	}

	if (reverse)
	{
		data_patt = (HammerData)((int)data_patt ^ (int)REVERSE);
	}

	switch (data_patt)
	{
	case RANDOM:
		//  rows are already filled for random data patt
		break;
	case ONE_TO_ZERO:
		fill_stripe_gb1(*d_addr, v_addr, 0x00, suite->mem);
		break;
	case ZERO_TO_ONE:
		fill_stripe_gb1(*d_addr, v_addr, 0xff, suite->mem);
		break;
	default:
		break;
	}
}

void scan_chunk(HammerSuite *suite, HammerPattern *h_patt, MemoryChunk tar_chunk, HammerData data_patt)
{
	char *base_v = tar_chunk.from;
	char *end_v = tar_chunk.to;

	uint8_t t_val = data_patt == ONE_TO_ZERO? 0xff : 0x00;

	FlipVal flip;

	for (char *idx_v = base_v; idx_v < end_v; idx_v += (1 << 6))
	{
		clflush(idx_v);
		cpuid();
		uint8_t *idx8_v = (uint8_t *)idx_v;

		for (uint8_t *tmp_v = idx8_v; tmp_v < (idx8_v + CL_SIZE); tmp_v++)
		{

			if (*tmp_v != t_val)
			{

				DRAMAddr d_tmp = phys_2_dram(virt_2_phys((char *)tmp_v));
				flip.d_vict = d_tmp;
				flip.f_og = (uint8_t)t_val;
				flip.f_new = *(uint8_t *)(tmp_v);
				flip.h_patt = h_patt;
				export_flip(&flip);

				*tmp_v = t_val;
			}
		}

		memset((char *)(idx_v), t_val, CL_SIZE);
	}
}


int mem_check(SessionConfig *cfg, MemoryBuffer *memory)
{
	fprintf(stderr, "Running mem_check session...\n");
	MemoryBuffer mem = *memory;
	DRAMAddr d_base = phys_2_dram(virt_2_phys(mem.buffer[0], &mem));
	int acts;

	HammerSuite *suite = (HammerSuite *)malloc(sizeof(HammerSuite));
	suite->mem = &mem;
	suite->cfg = cfg;
	suite->d_base = d_base;
	suite->mapper = (ADDRMapper *)malloc(sizeof(ADDRMapper));
	init_addr_mapper(suite->mapper, &mem, &suite->d_base, cfg->h_rows);

	create_output_fd("memcheck", d_base, cfg);
	fprintf(out_fd, "!scan_check run\n");
	fflush(out_fd);

	int tot_banks = get_banks_cnt();
	MemoryChunk mem_chunk[NUM_PAGES];

	// Init data
	int num_chunks = 0;
	int iterations = 0;
	for (int buffer_n = 0; buffer_n < NUM_PAGES; buffer_n++)
	{
		char *base_v = mem.buffer[buffer_n];

		int init_data = FILL_FF ? 0xff : 0x00;
		memset(base_v, init_data, HUGE_SIZE);
	}

	// CLFLUSh hammer code - more randomize due to relaxed aggressor restrictions

	for (int iter = 0; iter < 1000; iter++)
	{
		for (int num_aggs = 10; num_aggs < 11; num_aggs++)
		{
			for (int sh_num_banks = 1; sh_num_banks < 7; sh_num_banks++)
			{
				{

					int sh_len = num_aggs * sh_num_banks;
					DRAMAddr *sh_agg_d = (DRAMAddr *)malloc(sizeof(DRAMAddr) * sh_len);
					char **sh_base_v = (char **)malloc(sizeof(char *) * sh_len);

					int sh_num_tar_chunks = (sh_len / sh_num_banks) / 2 + (sh_len / sh_num_banks) % 2; // double-sided

					int idx_tar[sh_num_tar_chunks] = {-1};
					int bank_tar[sh_num_banks] = {-1};

					for (int i = 0; i < (sh_num_banks);)
					{
						int new_bank;
						new_bank = random_int(0, 32);

						bool is_new = true;
						for (int j = 0; j < i; j++)
						{
							if (bank_tar[j] == new_bank)
							{
								is_new = false;
								break;
							}
						}

						if (is_new)
						{
							bank_tar[i] = new_bank;
							i++;
						}
					}
					// select thp idx & bank
					for (int i = 0; i < (sh_num_tar_chunks);)
					{
						int new_idx = random_int(0, sh_num_tar_chunks);

						bool is_new = true;
						for (int j = 0; j < i; j++)
						{
							if (idx_tar[j] == new_idx)
							{
								is_new = false;
								break;
							}
						}

						if (is_new)
						{
							idx_tar[i] = new_idx;
							i++;
						}
					}

					//////////////////////////

					for (int i = 0; i < sh_len; i++)
					{
						int tar_chunk = (i / sh_num_banks) / 2;
						int tar_bank = i % sh_num_banks;

						char *tar_base_v = mem.buffer[idx_tar[tar_chunk]];

						DRAMAddr tar_base_d = thp_virt_2_dram(tar_base_v);
						DRAMAddr tar_d;

						if ((i / sh_num_banks) % 2 == 0)
						{
							tar_d = tar_base_d;
							tar_d.row = tar_base_d.row + 1 + random_int(0, 5);
							tar_d.bank = bank_tar[tar_bank];
						}
						else
						{
							DRAMAddr tar_d = sh_agg_d[i - sh_num_banks];
							tar_d.row = tar_d.row + 2;
							tar_d.bank = bank_tar[tar_bank];
						}

						// sanity check
						char *tar_v = thp_dram_2_virt(tar_d, tar_base_v);

						sh_agg_d[i] = tar_d;
						sh_base_v[i] = tar_base_v;
					}

					for (int num_banks = sh_num_banks; num_banks <= sh_num_banks; num_banks++)
					{
						for (int offset = 0; offset < 1; offset++)
						{
							int offset_bk = offset * num_banks;
							if (offset_bk + num_banks > sh_num_banks)
							{
								offset_bk = sh_num_banks - num_banks;
							}

							HammerPattern h_patt;
							h_patt.rounds = cfg->h_rounds;
							h_patt.len = num_aggs * num_banks;
							h_patt.d_lst = (DRAMAddr *)malloc(sizeof(DRAMAddr) * h_patt.len);
							h_patt.v_baselst = (char **)malloc(sizeof(char *) * h_patt.len);

							fprintf(stderr, "num_banks: %d sh_num_banks: %d offset: %d offset_bk: %d sh_len: %d\n", num_banks, sh_num_banks, offset, offset_bk, sh_len);

							fprintf(stderr, "idx: ");
							for (int i = 0; i < h_patt.len; i++)
							{
								int idx = (i / num_banks) * sh_num_banks + i % num_banks + offset_bk;
								fprintf(stderr, "%d ", idx);
								assert(idx < sh_len);
								// add to h_patt
								h_patt.d_lst[i] = sh_agg_d[idx];
								h_patt.v_baselst[i] = sh_base_v[idx];
							}
							fprintf(stderr, "\n");
							fprintf(stderr, "[HAMMER] - %s: ", hPatt_2_str_thp(&h_patt, ALL_FIELDS));

							// SCAN FLUSHLESS
							int unroll_list[8] = {1, 2, 4, 5, 8, 10, 20, 40};

							for (int hammer_sel = 0; hammer_sel < 2; hammer_sel++)
							{
								{
									HammerData data = hammer_sel == 1 ? ONE_TO_ZERO : ZERO_TO_ONE;
									for (int buffer_n = 0; buffer_n < NUM_PAGES; buffer_n++)
									{
										char *base_v = mem.buffer[buffer_n];
										int init_data = data == ONE_TO_ZERO ? 0xff : 0x00;
										memset(base_v, init_data, HUGE_SIZE);
									}

#ifdef FLIPTABLE
									print_start_attack(&h_patt);
#endif
									for (int idx = 0; idx < h_patt.len; idx++)
										fill_row_thp(suite, &h_patt.d_lst[idx], h_patt.v_baselst[idx], data, false);

									uint64_t time;
									int test_num = hammer_sel;

									time = hammer_thp(&h_patt, &mem, hammer_sel);

										acts = h_patt.len * h_patt.rounds;
									fprintf(stderr, "%lu:%lu ", time / 1000000, time / acts);
									fprintf(stderr, "\n");

									for (int idx = 0; idx < h_patt.len; idx++)
										fill_row_thp(suite, &h_patt.d_lst[idx], h_patt.v_baselst[idx], data, true);

									for (int i = 0; i < h_patt.len; i++)
									{
										int tar_chunk = (i / num_banks) / 2;
										if (i % (num_banks * 2) == 0)
										{
											MemoryChunk tmp_chunk;

											tmp_chunk.from = mem.buffer[idx_tar[tar_chunk]];
											tmp_chunk.to = tmp_chunk.from + HUGE_SIZE;
											tmp_chunk.size = HUGE_SIZE;

											scan_chunk(suite, &h_patt, tmp_chunk, data);
										}
									}

									fprintf(stderr, ": %lu/%lu \n", time / 1000000, time / acts);
									fprintf(out_fd, ": %lu/%lu \n", time / 1000000, time / acts);
									fflush(out_fd);
								}
							}
						}
					}
				}
			}
		}
	}
}

int mem_check_1GB(SessionConfig *cfg, MemoryBuffer *memory)
{
	fprintf(stderr, "Running mem_check 1GB session...\n");
	MemoryBuffer mem = *memory;
	DRAMAddr d_base = phys_2_dram(virt_2_phys(mem.buffer[0], &mem));
	int acts;

	fprintf(stderr, "Huge base - virt: 0x%lx Phys: 0x%lx Dram: %d/%d/%d\n", mem.buffer[0], virt_2_phys(mem.buffer[0], &mem), d_base.bank,
	d_base.row, d_base.col);

	HammerSuite *suite = (HammerSuite *)malloc(sizeof(HammerSuite));
	suite->mem = &mem;
	suite->cfg = cfg;
	suite->d_base = d_base;
	suite->mapper = (ADDRMapper *)malloc(sizeof(ADDRMapper));
	init_addr_mapper(suite->mapper, &mem, &suite->d_base, cfg->h_rows);

	create_output_fd("memcheck_1gb", d_base, cfg);
	fprintf(out_fd, "!scan_check run\n");
	fflush(out_fd);

	int tot_banks = get_banks_cnt();
	int tot_rows = 1000;

	// addr translation test
	char *ttmp_v = mem.buffer[0];
	DRAMAddr ttmp_d = gb1_virt_2_dram(ttmp_v);

	for (int i = 0; i < 100; i++) {
		char* tmp_v = mem.buffer[0];
		DRAMAddr tmp_d = gb1_virt_2_dram(tmp_v);
		tmp_d.row = tmp_d.row + 1 + random_int(0, tot_rows - 100);
		tmp_d.bank = random_int(0, tot_banks);
	}

	/////////////////////////////
	// CLFLUSh hammer code - more randomize due to relaxed aggressor restrictions

	for (int iter = 0; iter < 5000; iter++)
	{
		for (int num_aggs = 10; num_aggs < 11; num_aggs++)
		{
			for (int sh_num_banks = 1; sh_num_banks < 7; sh_num_banks++)
			{

				int sh_len = num_aggs * sh_num_banks;
				DRAMAddr *sh_agg_d = (DRAMAddr *)malloc(sizeof(DRAMAddr) * sh_len);
				char **sh_base_v = (char **)malloc(sizeof(char *) * sh_len);

				int sh_num_tar_chunks = (sh_len / sh_num_banks) / 2 + (sh_len / sh_num_banks) % 2; // double-sided

				int bank_tar[sh_num_banks] = {-1};

				// select bank
				for (int i = 0; i < (sh_num_banks);)
				{
					int new_bank = random_int(0, tot_banks);

					bool is_new = true;
					for (int j = 0; j < i; j++)
					{
						if (bank_tar[j] == new_bank)
						{
							is_new = false;
							break;
						}
					}

					if (is_new)
					{
						bank_tar[i] = new_bank;
						i++;
					}
				}
				//////////////////////////
				for (int i = 0; i < sh_len; i++)
				{
					int tar_bank = i % sh_num_banks;

					char *tar_base_v = mem.buffer[0];

					DRAMAddr tar_base_d = gb1_virt_2_dram(tar_base_v);
					DRAMAddr tar_d;

					if ((i / sh_num_banks) % 2 == 0)
					{
						tar_d = tar_base_d;
						tar_d.row = tar_base_d.row + 1 + random_int(0, tot_rows - 1);
						tar_d.bank = bank_tar[tar_bank];
					}
					else
					{
						DRAMAddr tar_d = sh_agg_d[i - sh_num_banks];
						tar_d.row = tar_d.row + 2;
						tar_d.bank = bank_tar[tar_bank];
					}

					sh_agg_d[i] = tar_d;
					sh_base_v[i] = tar_base_v;
				}

				for (int num_banks = 1; num_banks <= sh_num_banks; num_banks = num_banks * 2)
				{
					for (int offset = 0; offset < (sh_num_banks + num_banks - 1) / num_banks; offset++) //Iterates through pre-selected banks
					//for (int offset = 0; offset < 1; offset++) // New bank added with increase in banks
					{
						int offset_bk = offset * num_banks;
						if (offset_bk + num_banks > sh_num_banks)
						{
							offset_bk = sh_num_banks - num_banks;
						}

						HammerPattern h_patt;
						h_patt.rounds = cfg->h_rounds;
						h_patt.len = num_aggs * num_banks;
						h_patt.d_lst = (DRAMAddr *)malloc(sizeof(DRAMAddr) * h_patt.len);
						h_patt.v_baselst = (char **)malloc(sizeof(char *) * h_patt.len);

						fprintf(stderr, "num_banks: %d sh_num_banks: %d offset: %d offset_bk: %d sh_len: %d\n", num_banks, sh_num_banks, offset, offset_bk, sh_len);

						fprintf(stderr, "idx: ");
						for (int i = 0; i < h_patt.len; i++)
						{
							int idx = (i / num_banks) * sh_num_banks + i % num_banks + offset_bk;
							fprintf(stderr, "%d ", idx);
							assert(idx < sh_len);
							// add to h_patt
							h_patt.d_lst[i] = sh_agg_d[idx];
							h_patt.v_baselst[i] = sh_base_v[idx];
						}
						fprintf(stderr, "\n");
						fprintf(stderr, "[HAMMER] - %s: ", hPatt_2_str_gb1(&h_patt, ALL_FIELDS));

						// SCAN FLUSHLESS
						for (int hammer_sel = 0; hammer_sel < 2; hammer_sel++)
						{
							HammerData data = hammer_sel == 1 ? ONE_TO_ZERO : ZERO_TO_ONE;
							char *base_v = mem.buffer[0];
							int init_data = data == ONE_TO_ZERO ? 0xff : 0x00;
							memset(base_v, init_data, ALLOC_SIZE);

#ifdef FLIPTABLE
							print_start_attack_gb1(&h_patt);
#endif
							for (int idx = 0; idx < h_patt.len; idx++)
								fill_row_gb1(suite, &h_patt.d_lst[idx], h_patt.v_baselst[idx], data, false);

							uint64_t time;
							int test_num = hammer_sel;

							time = hammer_gb1(&h_patt, &mem, test_num);

							acts = h_patt.len * h_patt.rounds;
							fprintf(stderr, "%lu:%lu ", time / 1000000, time / acts);
							fprintf(stderr, "\n");

							for (int idx = 0; idx < h_patt.len; idx++)
								fill_row_gb1(suite, &h_patt.d_lst[idx], h_patt.v_baselst[idx], data, true);

							for (int i = 0; i < h_patt.len; i++)
							{
								if (i % (num_banks * 2) == 0)
								{
									char* agg_v = gb1_dram_2_virt(h_patt.d_lst[i], h_patt.v_baselst[i]);

									MemoryChunk tmp_chunk;

									tmp_chunk.from = agg_v - (HUGE_SIZE / 2);
									if (tmp_chunk.from < base_v) tmp_chunk.from = base_v;
									tmp_chunk.to = tmp_chunk.from + (HUGE_SIZE / 2);
									tmp_chunk.size = HUGE_SIZE;


									scan_chunk(suite, &h_patt, tmp_chunk, data);
								}
							}

							fprintf(stderr, ": %lu/%lu \n", time / 1000000, time / acts);
							fprintf(out_fd, ": %lu/%lu \n", time / 1000000, time / acts);
							fflush(out_fd);
						}
						free(h_patt.d_lst);
						free(h_patt.v_baselst);
					}
				}
			}
		}
	}
}
