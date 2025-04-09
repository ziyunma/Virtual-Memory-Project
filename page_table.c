/*
This is the implementation of the page table module.
Do not modify this file.
Make all of your changes to main.c instead.
*/

#define _GNU_SOURCE

#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <limits.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>

#include "page_table.h"

#define PAGE_BITS 12

struct page_table {
	int fd;
	char *virtmem;
	int npages;
	char *physmem;
	int nframes;
	int *page_mapping;
	int *page_bits;
	page_fault_handler_t handler;
};

struct page_table *the_page_table = 0;

static int bits_to_prots( int bits )
{
	int prots = 0;
	if(bits&BIT_PRESENT) prots |= PROT_READ|PROT_EXEC;
	if(bits&BIT_WRITE) prots |= PROT_WRITE;
	return prots;
}

static void internal_fault_handler( int signum, siginfo_t *info, void *context )
{

#ifdef i386
	char *addr = (char*)(((struct ucontext *)context)->uc_mcontext.cr2);
#else
	char *addr = info->si_addr;
#endif

	struct page_table *pt = the_page_table;

	if(pt) {
		int page = (addr-pt->virtmem) / PAGE_SIZE;

		if(page>=0 && page<pt->npages) {
			pt->handler(pt,page);
			return;
		}
	}

	fprintf(stderr,"segmentation fault at address %p\n",addr);
	abort();
}

struct page_table * page_table_create( int npages, int nframes, page_fault_handler_t handler )
{
	int i;
	struct sigaction sa;
	struct page_table *pt;
	char filename[256];

	pt = malloc(sizeof(struct page_table));
	if(!pt) return 0;

	the_page_table = pt;

	sprintf(filename,"/tmp/pmem.%d.%d",getpid(),getuid());

	pt->fd = open(filename,O_CREAT|O_TRUNC|O_RDWR,0777);
	if(!pt->fd) return 0;

	ftruncate(pt->fd,PAGE_SIZE*npages);

	unlink(filename);

	pt->physmem = mmap(0,nframes*PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,pt->fd,0);
	pt->nframes = nframes;

	pt->virtmem = mmap(0,npages*PAGE_SIZE,PROT_NONE,MAP_SHARED|MAP_NORESERVE,pt->fd,0);
	pt->npages = npages;

	pt->page_bits = malloc(sizeof(int)*npages);
	pt->page_mapping = malloc(sizeof(int)*npages);

	pt->handler = handler;

	for(i=0;i<pt->npages;i++) pt->page_bits[i] = 0;

	sa.sa_sigaction = internal_fault_handler;
	sa.sa_flags = SA_SIGINFO;

	sigfillset( &sa.sa_mask );
	sigaction( SIGSEGV, &sa, 0 );

	return pt;
}

void page_table_delete( struct page_table *pt )
{
	munmap(pt->virtmem,pt->npages*PAGE_SIZE);
	munmap(pt->physmem,pt->nframes*PAGE_SIZE);
	free(pt->page_bits);
	free(pt->page_mapping);
	close(pt->fd);
	free(pt);
}

void page_table_set_entry( struct page_table *pt, int page, int frame, int bits )
{
	if( page<0 || page>=pt->npages ) {
		fprintf(stderr,"page_table_set_entry: illegal page #%d\n",page);
		abort();
	}

	if( frame<0 || frame>=pt->nframes ) {
		fprintf(stderr,"page_table_set_entry: illegal frame #%d\n",frame);
		abort();
	}

	pt->page_mapping[page] = frame;
	pt->page_bits[page] = bits;

	remap_file_pages(pt->virtmem+page*PAGE_SIZE,PAGE_SIZE,0,frame,0);
	mprotect(pt->virtmem+page*PAGE_SIZE,PAGE_SIZE,bits_to_prots(bits));
}

void __internal_set_bits( struct page_table *pt, void *vaddr, int bits )
{
	int page = ((char*)vaddr-pt->virtmem) >> PAGE_BITS;
	
	if( page<0 || page>=pt->npages ) {
		fprintf(stderr,"page_table_set_bits: illegal address 0x%p\n",vaddr);
		abort();
	}

	pt->page_bits[page] |= bits;
}

void page_table_get_entry( struct page_table *pt, int page, int *frame, int *bits )
{
	if( page<0 || page>=pt->npages ) {
		fprintf(stderr,"page_table_get_entry: illegal page #%d\n",page);
		abort();
	}

	*frame = pt->page_mapping[page];
	*bits = pt->page_bits[page];
}

void page_table_print_entry( struct page_table *pt, int page )
{
	if( page<0 || page>=pt->npages ) {
		fprintf(stderr,"page_table_print_entry: illegal page #%d\n",page);
		abort();
	}

	int b = pt->page_bits[page];

	printf("page %06d: frame %06d bits %c%c%c%c\n",
		page,
		pt->page_mapping[page],
		b&BIT_PRESENT ? 'P' : '-',
		b&BIT_WRITE   ? 'W' : '-',
	        b&BIT_DIRTY   ? 'D' : '-',
	        b&BIT_REF     ? 'R' : '-'
	);

}

void page_table_print( struct page_table *pt )
{
	int i;
	for(i=0;i<pt->npages;i++) {
		page_table_print_entry(pt,i);
	}
}

int page_table_get_nframes( struct page_table *pt )
{
	return pt->nframes;
}

int page_table_get_npages( struct page_table *pt )
{
	return pt->npages;
}

void * page_table_get_virtmem( struct page_table *pt )
{
	return pt->virtmem;
}

void * page_table_get_physmem( struct page_table *pt )
{
	return pt->physmem;
}
