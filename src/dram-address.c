#include "dram-address.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "utils.h"

#define DEBUG_REVERSE_FN 1

extern DRAMLayout g_mem_layout;

uint64_t get_dram_row(physaddr_t p_addr)
{
	return (p_addr & g_mem_layout.
		row_mask) >> __builtin_ctzl(g_mem_layout.row_mask);
}

uint64_t get_dram_col(physaddr_t p_addr)
{
	return (p_addr & g_mem_layout.
		col_mask) >> __builtin_ctzl(g_mem_layout.col_mask);
}

DRAMAddr phys_2_dram(physaddr_t p_addr)
{

	DRAMAddr res = { 0, 0, 0 };
	for (int i = 0; i < g_mem_layout.h_fns.len; i++) {
		res.bank |=
		    (__builtin_parityl(p_addr & g_mem_layout.h_fns.lst[i]) <<
		     i);
	}

	res.row = get_dram_row(p_addr);
	res.col = get_dram_col(p_addr);

	return res;
}

////////////////////////////////////////////////////////////////////////
///thp code 
uint64_t get_dram_row_thp(uint64_t v_addr)
{
	uint64_t row_mask = (HUGE_SIZE -1) & g_mem_layout.row_mask;
	return (v_addr & row_mask) >> __builtin_ctzl(row_mask);
}

uint64_t get_dram_col_thp(uint64_t v_addr)
{
	assert(__builtin_clzl(g_mem_layout.col_mask) > __builtin_clzl(HUGE_SIZE));
	return (v_addr & g_mem_layout.
		col_mask) >> __builtin_ctzl(g_mem_layout.col_mask);
}

DRAMAddr thp_virt_2_dram(char* v_addr)
{
	uint64_t addr64_v = (uint64_t) v_addr;
	DRAMAddr ret_d = { 0, 0, 0};

	for (int i = 0; i < g_mem_layout.h_fns.len; i++) {
		ret_d.bank |=
		    (__builtin_parityl(addr64_v & g_mem_layout.h_fns.lst[i]) <<
		     i);
	}

	ret_d.row = get_dram_row_thp(addr64_v);
	ret_d.col = get_dram_col_thp(addr64_v);

	return ret_d;
}

uint64_t get_dram_row_gb1(uint64_t v_addr)
{
	uint64_t row_mask = (ALLOC_SIZE -1) & g_mem_layout.row_mask;
	return (v_addr & row_mask) >> __builtin_ctzl(row_mask);
}

uint64_t get_dram_col_gb1(uint64_t v_addr)
{
	assert(__builtin_clzl(g_mem_layout.col_mask) > __builtin_clzl(ALLOC_SIZE));
	return (v_addr & g_mem_layout.
		col_mask) >> __builtin_ctzl(g_mem_layout.col_mask);
}


DRAMAddr gb1_virt_2_dram(char* v_addr)
{
	uint64_t addr64_v = (uint64_t) v_addr;
	DRAMAddr ret_d = { 0, 0, 0};

	for (int i = 0; i < g_mem_layout.h_fns.len; i++) {
		ret_d.bank |=
		    (__builtin_parityl(addr64_v & g_mem_layout.h_fns.lst[i]) <<
		     i);
	}

	ret_d.row = get_dram_row_gb1(addr64_v);
	ret_d.col = get_dram_col_gb1(addr64_v);

	return ret_d;
}

char* thp_dram_2_virt(DRAMAddr d_addr, char* base_v) {
	uint64_t ret_64 = 0;
	uint64_t col_val = 0;
	uint64_t base_64 = (uint64_t) base_v;
	assert(base_v != 0);
	uint64_t row_mask = (HUGE_SIZE -1) & g_mem_layout.row_mask;
	uint64_t uppermask = 0UL - (1UL <<(64 - __builtin_clzl(row_mask)));

	ret_64 = (d_addr.row << __builtin_ctzl(g_mem_layout.row_mask));	// set row bits
	if(__builtin_parity(ret_64 & uppermask) != 0){
		fprintf(stderr, "bk: %lu r: %lu par: %lx ret: %016lx uppermask: %016lx \n", d_addr.bank, d_addr.row, __builtin_parity(ret_64 & uppermask), ret_64, uppermask);
		assert(false);
	};
	ret_64 |= (d_addr.col << __builtin_ctzl(g_mem_layout.col_mask));	// set col bits

	for (int i = 0; i < g_mem_layout.h_fns.len; i++) {
		uint64_t masked_addr = ret_64 & g_mem_layout.h_fns.lst[i];
		// if the address already respects the h_fn then just move to the next func
		if (__builtin_parity(masked_addr) == ((d_addr.bank >> i) & 1L)) {
			continue;
		}
		// else flip a bit of the address so that the address respects the dram h_fn
		// that is get only bits not affecting the row.
		uint64_t h_lsb = __builtin_ctzl((g_mem_layout.h_fns.lst[i]) &
						~(g_mem_layout.col_mask) &
						~(g_mem_layout.row_mask));
		ret_64 ^= 1 << h_lsb;
	}

	ret_64 = ret_64 | (base_64 & uppermask);

	#if CHECK_DRAM_2_VIRT
	DRAMAddr tmp_ret_d = thp_virt_2_dram((char*) ret_64);
	assert(tmp_ret_d.row == d_addr.row && tmp_ret_d.bank == d_addr.bank && tmp_ret_d.col == d_addr.col);
	#endif


	return (char*) ret_64;
}

char *gb1_dram_2_virt(DRAMAddr d_addr, char *base_v)
{
	uint64_t ret_64 = 0;
	uint64_t col_val = 0;
	uint64_t base_64 = (uint64_t) base_v;
	assert(base_v != 0);
	uint64_t row_mask = (ALLOC_SIZE -1) & g_mem_layout.row_mask;
	uint64_t uppermask = 0UL - (1UL <<(64 - __builtin_clzl(row_mask)));

	ret_64 = (d_addr.row << __builtin_ctzl(g_mem_layout.row_mask));	// set row bits

	if(__builtin_parity(ret_64 & uppermask) != 0){
		fprintf(stderr, "\n\nbk: %lu r: %lu par: %lx ret: %016lx uppermask: %016lx \n", d_addr.bank, d_addr.row, __builtin_parity(ret_64 & uppermask), ret_64, uppermask);
		assert(false);
	};
	ret_64 |= (d_addr.col << __builtin_ctzl(g_mem_layout.col_mask));	// set col bits

	for (int i = 0; i < g_mem_layout.h_fns.len; i++) {
		uint64_t masked_addr = ret_64 & g_mem_layout.h_fns.lst[i];
		// if the address already respects the h_fn then just move to the next func
		if (__builtin_parity(masked_addr) == ((d_addr.bank >> i) & 1L)) {
			continue;
		}
		// else flip a bit of the address so that the address respects the dram h_fn
		// that is get only bits not affecting the row.
		uint64_t h_lsb = __builtin_ctzl((g_mem_layout.h_fns.lst[i]) &
						~(g_mem_layout.col_mask) &
						~(g_mem_layout.row_mask));
		ret_64 ^= 1 << h_lsb;
	}

	ret_64 = ret_64 | (base_64 & uppermask);

	#if CHECK_DRAM_2_VIRT
	DRAMAddr tmp_ret_d = gb1_virt_2_dram((char*) ret_64);
	if (!(tmp_ret_d.row == d_addr.row && tmp_ret_d.bank == d_addr.bank && tmp_ret_d.col == d_addr.col)) {
		fprintf(stderr, "\n\nWrong addr expct'd: %d/%d/%d got: %d/%d/%d \n", 
		d_addr.bank, d_addr.row, d_addr.col,
		tmp_ret_d.bank, tmp_ret_d.row, tmp_ret_d.col	
		);
		fprintf(stderr, "%lx row_mask: %lx ret: %lx xor'd: %lx uppermask: %016lx \n", ALLOC_SIZE -1, row_mask, ret_64, ret_64 ^ base_64, uppermask);
		assert(false);
	}

	#endif


	return (char*) ret_64;
}

////////////////////////////////////////////////////////////////////////

physaddr_t dram_2_phys(DRAMAddr d_addr)
{
	physaddr_t p_addr = 0;
	uint64_t col_val = 0;

	p_addr = (d_addr.row << __builtin_ctzl(g_mem_layout.row_mask));	// set row bits
	p_addr |= (d_addr.col << __builtin_ctzl(g_mem_layout.col_mask));	// set col bits

	for (int i = 0; i < g_mem_layout.h_fns.len; i++) {
		uint64_t masked_addr = p_addr & g_mem_layout.h_fns.lst[i];
		// if the address already respects the h_fn then just move to the next func
		if (__builtin_parity(masked_addr) == ((d_addr.bank >> i) & 1L)) {
			continue;
		}
		// else flip a bit of the address so that the address respects the dram h_fn
		// that is get only bits not affecting the row.
		uint64_t h_lsb = __builtin_ctzl((g_mem_layout.h_fns.lst[i]) &
						~(g_mem_layout.col_mask) &
						~(g_mem_layout.row_mask));
		p_addr ^= 1 << h_lsb;
	}

#if DEBUG_REVERSE_FN
	int correct = 1;
	for (int i = 0; i < g_mem_layout.h_fns.len; i++) {

		if (__builtin_parity(p_addr & g_mem_layout.h_fns.lst[i]) !=
		    ((d_addr.bank >> i) & 1L)) {
			correct = 0;
			break;
		}
	}
	if (d_addr.row != ((p_addr &
			    g_mem_layout.row_mask) >>
			   __builtin_ctzl(g_mem_layout.row_mask)))
		correct = 0;
	if (!correct)
		fprintf(stderr,
			"[DEBUG] - Mapping function for 0x%lx not respected\n",
			p_addr);

#endif

	return p_addr;
}

physaddr_t merge_high_bits(physaddr_t high, physaddr_t low) {
	assert(high != NULL);
	int num_high = 64 - __builtin_clzl(g_mem_layout.row_mask);
	assert(num_high != 0);
	physaddr_t ret_addr = low;
	ret_addr = ret_addr ^ (high >> num_high << num_high);

	return ret_addr;
}

void set_global_dram_layout(DRAMLayout & mem_layout)
{
	g_mem_layout = mem_layout;
}

DRAMLayout *get_dram_layout()
{
	return &g_mem_layout;
}

bool d_addr_eq(DRAMAddr * d1, DRAMAddr * d2)
{
	return (d1->bank == d2->bank) && (d1->row == d2->row)
	    && (d1->col == d2->col);
}

bool d_addr_eq_row(DRAMAddr * d1, DRAMAddr * d2)
{
	return (d1->bank == d2->bank) && (d1->row == d2->row);
}

uint64_t get_banks_cnt()
{
	return 1 << g_mem_layout.h_fns.len;

}

char *dram_2_str(DRAMAddr * d_addr)
{
	static char ret_str[1024];
	sprintf(ret_str, "DRAM(bk: %ld (%s), row: %08ld, col: %08ld)",
		d_addr->bank, int_2_bin(d_addr->bank), d_addr->row,
		d_addr->col);
	return ret_str;
}

char *dramLayout_2_str(DRAMLayout * mem_layout)
{
	static char ret_str[1024];
	sprintf(ret_str, "{0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx} - 0x%lx\n",
		mem_layout->h_fns.lst[0], mem_layout->h_fns.lst[1],
		mem_layout->h_fns.lst[2], mem_layout->h_fns.lst[3],
		mem_layout->h_fns.lst[4], mem_layout->h_fns.lst[5],
		mem_layout->row_mask);
	return ret_str;
}
