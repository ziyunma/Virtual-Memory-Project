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
int disk_reads = 0;
int disk_writes = 0;

//for clock algo:
int *ref_bits = 0;
int clock_hand = 0;

unsigned int *ages = 0; // Array of ages, one per frame
unsigned int time_counter = 0;

/* A dummy page fault handler to start.  This is where most of your work goes. */
void page_fault_handler( struct page_table *pt, int page )
{
	page_faults++; // incr counter
	int frame;
	int bits;

	page_table_get_entry(pt, page, &frame, &bits); // get curr mapping and flags for faulting page

	// If the page is already present, page fault, something went wrong
	if (bits & BIT_PRESENT) {
		fprintf(stderr, "Page is already present on page %d\n", page);
		exit(1);
	}

	// Try to find a free frame
	for (int i = 0; i < nframes; i++) {
		if (frame_table[i] == -1) {
			// Empty frame found
			disk_read(disk, page, &physmem[i * PAGE_SIZE]); //load page from disk
			disk_reads++;
			page_table_set_entry(pt, page, i, BIT_PRESENT | BIT_WRITE); // update page table and frame
			frame_table[i] = page;
			page_table_rev[page] = i;

			if (!strcmp(algorithm, "clock")) { // if clock algo
				if (!ref_bits) { // make sure ref_bits only allocated once
					ref_bits = malloc(sizeof(int) * nframes); 
					for (int j = 0; j < nframes; j++) ref_bits[j] = 0; // init all entries at beginning to 0
				}
				ref_bits[i] = 1; // set to 1 (recently used)
			}
			if (!strcmp(algorithm, "custom")) {
				time_counter++;
				ages[i] = time_counter;
			}
			return; // exit bc we found a free frame
		}
	}

	int victim; // didnt find free frame, evict someone

	if (!strcmp(algorithm, "rand")) { 
		victim = rand() % nframes; // using rand evict random frame
	} 
	else if (!strcmp(algorithm, "clock")) {
		if (!ref_bits) { // using clock, make sure ref_bits is init, if not do it
			ref_bits = malloc(sizeof(int) * nframes);
			for (int j = 0; j < nframes; j++) ref_bits[j] = 0;
		}
		while (1) {
			int candidate_page = frame_table[clock_hand]; // look at page curr at clock hanf pos and get PT info
			int candidate_bits;
			page_table_get_entry(pt, candidate_page, &frame, &candidate_bits);

			if (ref_bits[clock_hand] == 0) { // PT hasnt referenced recently so choose as victim
				victim = clock_hand;
				clock_hand = (clock_hand + 1) % nframes; // move clock hand to next spot (circular)
				break;
			} else { // ref bit is set to 1 so move clock fwd
				ref_bits[clock_hand] = 0;
				clock_hand = (clock_hand + 1) % nframes;
			}
		}
	}
	else if (!strcmp(algorithm, "custom")) {
		unsigned int oldest = ~0;
		victim = -1;
		for (int i = 0; i < nframes; i++) {
			if (ages[i] < oldest) {
				oldest = ages[i];
				victim = i;
			}
		}
	}
	
	
	int evicted_page = frame_table[victim]; // get virtual page stored in victim frame

	// Get current bits of evicted page
	int evicted_bits;
	page_table_get_entry(pt, evicted_page, &frame, &evicted_bits);

	// If dirty, write back to disk
	if (evicted_bits & BIT_DIRTY) {
		disk_write(disk, evicted_page, &physmem[victim * PAGE_SIZE]);
		disk_writes++;
	}

	// Invalidate old page
	page_table_set_entry(pt, evicted_page, 0, 0);

	// Load new page into victim frame
	disk_read(disk, page, &physmem[victim * PAGE_SIZE]);
	disk_reads++;
	page_table_set_entry(pt, page, victim, BIT_PRESENT | BIT_WRITE);
	frame_table[victim] = page;
	page_table_rev[page] = victim;

	if (!strcmp(algorithm, "clock")) { // if clock set ref bits to newly loaded page
		ref_bits[victim] = 1;
	}
	if (!strcmp(algorithm, "custom")) {
		time_counter++;
		ages[victim] = time_counter;
	}

}

int main( int argc, char *argv[] )
{
	if(argc!=5) {
		printf("use: virtmem <npages> <nframes> <rand|clock|custom> <alpha|beta|gamma|delta>\n");
		return 1;
	}

	npages = atoi(argv[1]);
	nframes = atoi(argv[2]);
	algorithm = argv[3];
	const char *program = argv[4];

	if (strcmp(algorithm, "rand") != 0 && strcmp(algorithm, "clock") != 0 && strcmp(algorithm, "custom") != 0) { 
		fprintf(stderr, "unknown algorithm: %s\n", algorithm);
		return 1;
	}

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
	ages = malloc(sizeof(unsigned int) * nframes);
	for (int i = 0; i < nframes; i++) frame_table[i] = -1;
	for (int i = 0; i < npages; i++) page_table_rev[i] = -1;
	for (int i = 0; i < nframes; i++) ages[i] = 0;
	
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

	printf("\nsummary:\n\tpage faults: %d\n", page_faults);
	printf("\tdisk reads: %d\n", disk_reads);
	printf("\tdisk writes: %d\n", disk_writes);

	page_table_delete(pt);
	disk_close(disk);

	return 0;
}
