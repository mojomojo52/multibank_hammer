#pragma once

#include "types.h"

#define HASH_FN_CNT 6

typedef struct {
	uint64_t lst[HASH_FN_CNT];
	uint64_t len;
} AddrFns;

typedef struct {
	AddrFns h_fns;
	uint64_t row_mask;
	uint64_t col_mask;
} DRAMLayout;

typedef struct {
	/* bank is a simplified addressing of <ch,dimm,rk,bg,bk>
	   where all this will eventually map to a specific bank */
	uint64_t bank;
	uint64_t row;
	uint64_t col;
} DRAMAddr;


//thp
DRAMAddr thp_virt_2_dram(char* v_addr);
DRAMAddr gb1_virt_2_dram(char* v_addr);
char* thp_dram_2_virt(DRAMAddr d_addr, char* base_v);
char *gb1_dram_2_virt(DRAMAddr d_addr, char *base_v);


physaddr_t dram_2_phys(DRAMAddr d_addr);
DRAMAddr phys_2_dram(physaddr_t p_addr);
physaddr_t merge_high_bits(physaddr_t high, physaddr_t low);
char *dram_2_str(DRAMAddr * d_addr);
char *dramLayout_2_str(DRAMLayout * mem_layout);
DRAMLayout *get_dram_layout();
uint64_t get_banks_cnt();
bool d_addr_eq(DRAMAddr * d1, DRAMAddr * d2);
bool d_addr_eq_row(DRAMAddr * d1, DRAMAddr * d2);
