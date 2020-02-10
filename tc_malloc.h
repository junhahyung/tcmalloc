/*
 * =====================================================================================
 *
 *       Filename:  tc_malloc.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  11/17/2019 15:31:48
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Junha Hyung (), sharpeeee@kaist.ac.kr
 *   Organization:  
 *
 * =====================================================================================
 */

#ifndef __TCMALLOC_H__
#define __TCMALLOC_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>

// basic constant                        //
// --------------------------------------//
// --------------------------------------//
#define KPAGESHIFT 12 // 4KB pagesize
#define KMAXPAGES  256 // page heap storage size
#define KTOTALPAGES 1<<20 // page heap storage size = number of entries in pagemap
#define TESTPAGEMAP 1<<23 // for testing

#define KINITHEAPSIZE 2048<<20 // 2048MB
//#define KINITHEAPSIZE 128<<20 // 128MB
#define KMINSYSALLOC  256<<20 // 256MB 
//#define KMINSYSALLOC  128<<20 // 128MB 
#define METAALLOCBYTE  128<<20 // 128MB - how much to allocate from system each time for span structure
#define KCLASSSIZES 158
// #of pages to fetch from pageheap to central freelist
// Need optimization
#define NBATCH 256 
#define LARGEALLOC 32*1024


// span                                  //
//---------------------------------------//
//---------------------------------------//
typedef struct Span* Span_t;

typedef struct Span {
    uint32_t start;  //starting page number of span
    uint32_t length; //length
    Span_t next;
    Span_t prev;
    void* objects;
    uint32_t location;
    uint32_t refcount; // Number of non free objects (allocated objects)
    uint32_t sizeclass; // size class, only for small objects
} Span;

// Span allocator`                   //
//---------------------------------------//
//---------------------------------------//
typedef struct SpanAllocator{
    uint32_t inuse_;
    Span_t free_area_;
    uint32_t free_avail_; // available space in allocator
    Span_t free_list_; // contains spans with no real pages, only shell
} SpanAllocator;

// PageMap       `                   //
//---------------------------------------//
//---------------------------------------//
typedef struct PageMap {
    Span_t central_array[TESTPAGEMAP];    
} Pagemap;

// pageheap          `                   //
//---------------------------------------//
//---------------------------------------//
typedef struct PageHeap {
    Span_t free_[KMAXPAGES];
    Pagemap* pagemap_t;
    uint64_t size;
} Pageheap;

// CentralFreelist   `                   //
//---------------------------------------//
//---------------------------------------//
typedef struct CentralFreelist{
    uint32_t size_class_;
    Span_t empty_;
    Span_t nonempty_;
    uint32_t num_spans_; // total number of spans. i.e. empty + nonempty
    uint32_t counter_; // number of free objects
} CentralFreelist;

// ThreadCache       `                   //
//---------------------------------------//
//---------------------------------------//
typedef struct Freelist{
    void* list_;
    uint32_t length_; // length of free objects
    uint32_t max_length_; // maximum length recording for optimization?
} Freelist;

typedef struct ThreadCache {
    //ThreadCache* next_;
    //ThreadCache* prev_;
    uint32_t size_; // overall size
    Freelist list_[KCLASSSIZES];
} Threadcache;

// API functions                         //
//---------------------------------------//
//---------------------------------------//
void *tc_central_init();
void *tc_thread_init();
void *tc_malloc(uint32_t size);
void tc_free(void *ptr);

// Internal Functions                    //
//---------------------------------------//
//---------------------------------------//

void init_SpanAllocator();
void init_Pagemap();
void init_Pageheap();
bool init_Growheap();
void init_Freelist(Freelist* freelist);

uint32_t BytesToPages(uint32_t byte);
uint32_t IndexToClass(uint32_t idx);

void* AllocateNewSpan();
Span_t newspan(uint32_t p, uint32_t len);
void DeleteSpan(Span_t span);

void PagemapSet(Span_t span);
Span_t PagemapGetExact(uint32_t pid);
Span_t PagemapGet(uint32_t pid);

Span_t PreMerge(Span_t span, Span_t other);
void Pageheap_RemoveFromFreelist(Span_t span);
void Pageheap_InsertSpan(Span_t span);
void Pageheap_FreelistAppend(Span_t span);
Span_t Pageheap_New_Safe(uint32_t npages);
Span_t Pageheap_SearchAndRemove(uint32_t npages);
Span_t Carve(Span_t span, uint32_t reqpages);
bool Growheap(uint32_t n);

void CentralFreelist_FetchSpan(uint32_t idx);
int CentralFreelist_RemoveRange(uint32_t idx, void**start, void**end, uint32_t batchsize);

void* large_malloc(uint32_t size);
void* small_malloc(uint32_t size);
uint32_t SizeToPages(uint32_t size);
uint32_t ClassToIndex(uint32_t cl);
void* SpanToPointer(Span_t span);

void do_free_pages(Span_t span);
void free_small(void*ptr, uint32_t cl);

// number of objects to fetch from central freelist to ThreadCache
// TODO optimize!
uint32_t Thread_GetFetchSize(uint32_t idx);
void* FetchFromCentralFree(uint32_t idx);

// Debugging functions                   //
//---------------------------------------//
void check(Span_t span);
void Print_Pageheap();
void Check_Pagemap();
void Print_SpanAllocator();
void Print_CentralFreelist();
void Print_ThreadCache();

#endif
