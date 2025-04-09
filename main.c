/*
Main program for the virtual memory project.
Make all of your modifications to this file.
You may add or rearrange any code or data as you need.
The header files page_table.h and disk.h explain
how to use the page table and disk interfaces.
*/

#include "page_table.h"
#include "disk.h"
#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Global pointer to the disk object */
struct disk *disk = 0;

/* Global pointer to the physical memory spacet */
unsigned char *physmem = 0;

/* Global pointer to the virtual memory spacet */
unsigned char *virtmem = 0;

int *frame_table = 0;
int *page_table_rev = 0; // reverse lookup: frame -> page
int nframes = 0;
int npages = 0;
const char *algorithm = NULL;
int page_faults = 0;

/* A dummy page fault handler to start.  This is where most of your work goes. */
void page_fault_handler( struct page_table *pt, int page )
{
	page_faults++;
	int frame;
	int bits;

	page_table_get_entry(pt, page, &frame, &bits);

	// If the page is already present, page fault
	if (bits & BIT_PRESENT) {
		fprintf(stderr, "Page is already present on page %d\n", page);
		exit(1);
	}

	// Try to find a free frame
	for (int i = 0; i < nframes; i++) {
		if (frame_table[i] == -1) {
			// Empty frame found
			disk_read(disk, page, &physmem[i * PAGE_SIZE]);
			page_table_set_entry(pt, page, i, BIT_PRESENT | BIT_WRITE);
			frame_table[i] = page;
			page_table_rev[page] = i;
			return;
		}
	}

	// No free frame: choose a random one to evict
	// printf("%d\n", nframes);
	int victim = rand() % nframes;
	int evicted_page = frame_table[victim];

	// Get current bits of evicted page
	int evicted_bits;
	page_table_get_entry(pt, evicted_page, &frame, &evicted_bits);

	// If dirty, write back to disk
	if (evicted_bits & BIT_DIRTY) {
		disk_write(disk, evicted_page, &physmem[victim * PAGE_SIZE]);
	}

	// Invalidate old page
	page_table_set_entry(pt, evicted_page, 0, 0);

	// Load new page into victim frame
	disk_read(disk, page, &physmem[victim * PAGE_SIZE]);
	page_table_set_entry(pt, page, victim, BIT_PRESENT | BIT_WRITE);
	frame_table[victim] = page;
	page_table_rev[page] = victim;
}

int main( int argc, char *argv[] )
{
	if(argc!=5) {
		printf("use: virtmem <npages> <nframes> <rand|clock|custom> <alpha|beta|gamma|delta>\n");
		return 1;
	}

	npages = atoi(argv[1]);
	nframes = atoi(argv[2]);
	const char *algoname = argv[3];
	const char *program = argv[4];

	disk = disk_open("myvirtualdisk",npages);
	if(!disk) {
		fprintf(stderr,"couldn't create virtual disk: %s\n",strerror(errno));
		return 1;
	}

	struct page_table *pt = page_table_create( npages, nframes, page_fault_handler );
	if(!pt) {
		fprintf(stderr,"couldn't create page table: %s\n",strerror(errno));
		return 1;
	}

	physmem = page_table_get_physmem(pt);
	virtmem = page_table_get_virtmem(pt);

	frame_table = malloc(sizeof(int) * nframes);
	page_table_rev = malloc(sizeof(int) * npages);
	for (int i = 0; i < nframes; i++) frame_table[i] = -1;
	for (int i = 0; i < npages; i++) page_table_rev[i] = -1;

	
	if(!strcmp(program,"alpha")) {
		alpha_program(pt,virtmem,npages*PAGE_SIZE);

	} else if(!strcmp(program,"beta")) {
		beta_program(pt,virtmem,npages*PAGE_SIZE);

	} else if(!strcmp(program,"gamma")) {
		gamma_program(pt,virtmem,npages*PAGE_SIZE);

	} else if(!strcmp(program,"delta")) {
		delta_program(pt,virtmem,npages*PAGE_SIZE);

	} else {
		fprintf(stderr,"unknown program: %s\n",argv[4]);
		return 1;
	}

	page_table_delete(pt);
	disk_close(disk);

	return 0;
}
