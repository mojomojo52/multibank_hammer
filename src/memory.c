#include "memory.h"
#include "utils.h"

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
#include <params.h>
//#include <dram-address.h>

#define DEF_RNG_LEN (8<<10)
#define DEBUG
#define DEBUG_LINE fprintf(stderr, "[DEBUG] - GOT HERE\n");


static int pmap_fd = NOT_OPENED;
static physaddr_t base_phys = 0L;

uint64_t get_pfn(uint64_t entry)
{
	return ((entry) & 0x7fffffffffffffff);
}

physaddr_t get_physaddr(uint64_t v_addr, int pmap_fd)
{
	uint64_t entry;
	uint64_t offset = (v_addr / 4096) * sizeof(entry);
	uint64_t pfn;
	bool to_open = false;
	if (pmap_fd == NOT_OPENED) {
		pmap_fd = open("/proc/self/pagemap", O_RDONLY);
		assert(pmap_fd >= 0);
		to_open = true;
	}
	int bytes_read = pread(pmap_fd, &entry, sizeof(entry), offset);

	if(bytes_read != 8) {
		fprintf(stderr, "FAILURE!!! Addr. not present in pagemap\n");
		assert(false);
	}

	assert(entry & (1ULL << 63));

	if (to_open) {
		close(pmap_fd);
		to_open = false;
	}

	pfn = get_pfn(entry);
	assert(pfn != 0);
	return (pfn << 12) | (v_addr & 4095);
}

char* get_virtaddr(physaddr_t p_addr, int pmap_fd, MemoryBuffer *mem)
{
	uint64_t entry;
	uint64_t tmp_vaddr;
	uint64_t mem_end;
	uint64_t pfn;

	bool to_open = false;

	if (pmap_fd == NOT_OPENED) {
		pmap_fd = open("/proc/self/pagemap", O_RDONLY);
		assert(pmap_fd >= 0);
		to_open = true;
	}
	bool found_addr = false;

	for (int i = 0; i < NUM_PAGES; i++)
	{
		tmp_vaddr = (uint64_t)mem->buffer[i];
		mem_end	  = (tmp_vaddr + (ALLOC_SIZE - sizeof(char)));
		while (tmp_vaddr < mem_end)
		{
			uint64_t offset = (tmp_vaddr / 4096) * sizeof(entry);
			tmp_vaddr += 4096;
			int bytes_read = pread(pmap_fd, &entry, sizeof(entry), offset);

			if (bytes_read != 8)
				continue;
			if (!(entry & (1ULL << 63)))
				continue;

			if (to_open)
			{
				close(pmap_fd);
			}

			pfn = get_pfn(entry);
			if (pfn == 0)
				continue;

			if (((pfn << 12) | (p_addr & 4095)) == p_addr) {
				found_addr = true;
				break;
			}
		}
	}
	assert(found_addr);

	return (char *)((tmp_vaddr - 4096) | ((uint64_t)p_addr & 4095));
}

int phys_cmp(const void *p1, const void *p2)
{
	return ((pte_t *) p1)->p_addr - ((pte_t *) p2)->p_addr;
}

void set_physmap(MemoryBuffer * mem)
{
	pmap_fd = open("/proc/self/pagemap", O_RDONLY);
	assert(pmap_fd >= 0);
}

void destruct_physmap(MemoryBuffer *mem) {
	close(pmap_fd);
}

physaddr_t virt_2_phys(char *v_addr, MemoryBuffer * mem)
{
	return get_physaddr((uint64_t)v_addr, pmap_fd);
}

physaddr_t virt_2_phys(char *v_addr)
{
	uint64_t v_addr_i = (uint64_t)v_addr;
	return get_physaddr(v_addr_i, pmap_fd);
}

char *phys_2_virt(physaddr_t p_addr, MemoryBuffer * mem)
{
	return get_virtaddr(p_addr, pmap_fd, mem);
}

char *phys_2_virt_delta(physaddr_t p_addr, physaddr_t p_base, char* v_base) {
	char* ret_p =  (char*) ((physaddr_t) v_base + (p_addr - p_base));
	return ret_p;
}
