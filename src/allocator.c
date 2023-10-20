#include "allocator.h"
#include "include/params.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include "utils.h"

int alloc_buffer(MemoryBuffer * mem)
{
	if (mem->buffer[0] != NULL) {
		fprintf(stderr, "[ERROR] - Memory already allocated\n");
	}

	if (mem->align < _SC_PAGE_SIZE) {
		mem->align = 0;
	}

#if USE_1GB
	uint64_t alloc_size = mem->align ? mem->size + mem->align : mem->size;
	uint64_t alloc_flags = MAP_PRIVATE | MAP_POPULATE;
#endif

#if USE_THP
	uint64_t alloc_size = mem->size;
	uint64_t alloc_flags = MAP_PRIVATE | MAP_POPULATE;
#endif

#if USE_1GB
	if (true) {
		if (mem->fd == 0) {
			fprintf(stderr,
				"[ERROR] - Missing file descriptor to allocate hugepage\n");
			exit(1);
		}
		alloc_flags |=
		    (true) ? MAP_ANONYMOUS | MAP_HUGETLB
		    | (30 << MAP_HUGE_SHIFT)
		    : (mem->flags & F_ALLOC_HUGE_2M) ? MAP_ANONYMOUS |
		    MAP_HUGETLB | (21 << MAP_HUGE_SHIFT)
		    : MAP_ANONYMOUS;
	} else {
		mem->fd = -1;
		alloc_flags |= MAP_ANONYMOUS;
	}
	mem->buffer[0] = (char *)mmap(NULL, mem->size, PROT_READ | PROT_WRITE,
				   alloc_flags, mem->fd, 0);
	if (mem->buffer[0] == MAP_FAILED) {
		perror("[ERROR] - mmap() failed");
		exit(1);
	}
	if (mem->align) {
		size_t error = (uint64_t) mem->buffer[0] % mem->align;
		size_t left = error ? mem->align - error : 0;
		munmap(mem->buffer[0], left);
		mem->buffer[0] += left;
		assert((uint64_t) mem->buffer[0] % mem->align == 0);
	}
#endif

#if USE_THP
	// madvise alloc
	for (int i = 0; i < NUM_PAGES; i++) {
		posix_memalign((void **)(&(mem->buffer[i])), POSIX_ALIGN, (1 << 21));
		if (madvise(mem->buffer[i], POSIX_ALIGN, MADV_HUGEPAGE) == -1)
		{
			fprintf(stderr, "MADV %d Failed\n", i);
			assert(false);
		}
		*(mem->buffer[i]) = 10;

		int data = FILL_FF? 0xff : 0x00;	
		memset((void*) mem->buffer[i], data, HUGE_SIZE);
	}
	if (mem->buffer[0] != NULL) {
		fprintf(stderr, "Buffer allocated\n");
	}
#endif

	if (mem->flags & F_VERBOSE) {
		fprintf(stderr, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
		fprintf(stderr, "[ MEM ] - Buffer:      %p\n", mem->buffer);
		fprintf(stderr, "[ MEM ] - Size:        %ld\n", alloc_size);
		fprintf(stderr, "[ MEM ] - Alignment:   %ld\n", mem->align);
		fprintf(stderr, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
	}
	return 0;
}

int free_buffer(MemoryBuffer * mem)
{
	free(mem->physmap);
	return munmap(mem->buffer, mem->size);
}
