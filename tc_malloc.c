/*
 * =====================================================================================
 *
 *       Filename:  tc_malloc.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  11/17/2019 02:18:59
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Junha Hyung (), sharpeeee@kaist.ac.kr
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include "tc_malloc.h"
// Global varibles for central structures
// initialization needed
Pageheap pageheap_;
SpanAllocator spanallocator_;
Pagemap pagemap_;
CentralFreelist centralfreelist_[KCLASSSIZES];

// Per thread global variables
// initialization needed
__thread Threadcache threadcache_;
int glcnt = 0;
int tcinit = 0;
// Spinlock
pthread_spinlock_t central_free_lock;
pthread_spinlock_t pageheap_lock;

void init_SpanAllocator(){
    memset(&spanallocator_, 0, sizeof(SpanAllocator));
    spanallocator_.inuse_ = 0;
    spanallocator_.free_avail_ = 0;
    return;
}

void init_Pagemap(){
    memset(&pagemap_, 0, sizeof(pagemap_));

    return;
}

void init_Pageheap(){
    memset(&pageheap_, 0, sizeof(pageheap_));
    pageheap_.size = 0;
    return;
}

void init_SpinLock(){
    pthread_spin_init(&central_free_lock,1);
    pthread_spin_init(&pageheap_lock,1);
}

uint32_t IndexToClass(uint32_t idx){
    uint32_t cl;
    if(idx>=0 && idx<=7){
        cl = 8*(idx+1);
    }
    else if(idx>=8 && idx<=38){
        cl = 64*(idx-6);
    }
    else if(idx>=39 && idx<=157){
        cl = 256*(idx-30);
    }
    else{
        cl = -1;
    }
    return cl;
}

void init_CentralFreelist(){
    uint32_t cl=0;
    for(int i=0;i<KCLASSSIZES;i++){
        cl = IndexToClass(i);
        centralfreelist_[i].size_class_ = cl;
        centralfreelist_[i].empty_ = NULL;
        centralfreelist_[i].nonempty_ = NULL;
        centralfreelist_[i].num_spans_ = 0;
        centralfreelist_[i].counter_ = 0;
    }

    for(int i=0;i<KCLASSSIZES;i++){
        pthread_spin_lock(&central_free_lock);
        CentralFreelist_FetchSpan(i);
        pthread_spin_unlock(&central_free_lock);
    }
    // debugging
    /*
    CentralFreelist_FetchSpan(0);
    CentralFreelist_FetchSpan(1);
    CentralFreelist_FetchSpan(1);
    */
    return;
}

// Allocates new span using SpanAllocator
// Grab an empty space for span from free_list or free_area, if none, allocate using sbrk
void* AllocateNewSpan(){
    Span_t result;
    // there is free space in free list, i.e. span that has been deleted
    if (spanallocator_.free_list_ != NULL){
        result = spanallocator_.free_list_;
        spanallocator_.free_list_ = result->next; // pointer to next free space
    }
    else {
        // not enough space in spanallocator for allocating a span
        // need to request more memory from the system using sbrk 
        if(spanallocator_.free_avail_ < sizeof(Span)){
            spanallocator_.free_area_ = (Span_t)sbrk(METAALLOCBYTE);
            if(spanallocator_.free_area_ == (Span_t)-1){
                printf("sbrk in AllocateNewSpan() faild\n");
                return NULL;
            }
            else{
                printf("span allocator allocated %u bytes with sbrk, free area: %p\n", METAALLOCBYTE, spanallocator_.free_area_);
            }
            spanallocator_.free_avail_ += METAALLOCBYTE;
        }
        result = spanallocator_.free_area_;
        spanallocator_.free_area_ ++; // should check
        spanallocator_.free_avail_ --;
    }
    spanallocator_.inuse_ ++; 
    return result;
}

void PagemapSet(Span_t span){
    uint32_t pid = span->start;
    uint32_t n = span->length;
    for(int i=pid;i<pid+n;i++){
        pagemap_.central_array[i] = span;
    }
    return;
}

// This function should be used when you need exact span at pagemap
Span_t PagemapGetExact(uint32_t pid){
    return pagemap_.central_array[pid];
}

Span_t newspan(uint32_t p, uint32_t len){
    Span_t result = (Span_t)AllocateNewSpan(); // allocates new span from SpanAllocator
    if (result == NULL){
        printf("failed allocating new span in newspan()");
        return NULL;
    }
    result->start = p;
    result->length = len;
    result->next = NULL;
    result->prev = NULL;
    result->objects = NULL;
    result->location = 0;
    result->refcount = 0;
    result->sizeclass = 0;
    return result;
}

// put span into free list of span allocator
// should also take care of pagemap
// set span location to 0
void DeleteSpan(Span_t span){
    uint32_t pid = span->start;
    uint32_t n = span->length;
    for(int i=pid;i<pid+n;i++){
        pagemap_.central_array[i] = NULL;
    }

    span->location = 0;
    span -> next = spanallocator_.free_list_;
    spanallocator_.free_list_ = span;
    spanallocator_.inuse_ --;
    return;
}

// remove span from pageheap freelist and change span's location to 3
void Pageheap_RemoveFromFreelist(Span_t span){
   span->location = 3;
   uint32_t idx = (span->length >= KMAXPAGES) ? KMAXPAGES - 1 : span->length-1; 
   
   if(span->prev == NULL){
       pageheap_.free_[idx] = span->next;
   }
   else{
       span->prev->next = span->next;
   }
   if(span->next != NULL){
       span->next->prev = span->prev;
   }
   span->prev = NULL;
   span->next = NULL;
   pageheap_.size -= span->length*4096;
   return;
}

// Check if two spans are mergeable, if so, remove other span from the free list 
Span_t PreMerge(Span_t span, Span_t other){
    if (other == NULL) {
        return other;
    }
    // if location of span is different, two spans cannot be coalesced
    if (span->location != other->location){
        return NULL;
    }
    Pageheap_RemoveFromFreelist(other);
    return other;
}

// initialize span structure, put it in the freelist of page heap (+change its location), and coalesce if possible
void Pageheap_InsertSpan(Span_t span){
    span->location = 1;
    span->sizeclass = 0;
    span->refcount = 0;
    uint32_t pid = span->start;
    uint32_t n = span->length;


    Span_t prev;
    Span_t next;

    // prev
    if(pid == 0) {
        //printf("pid is 0\n");
        prev = NULL;
    }
    else {
        // PreMerge: remove prev from freelist and change its location to 0
        prev = PreMerge(span, PagemapGetExact(pid-1));
        //printf("Premerge for prev done\n");
    }
    if(prev != NULL){
        uint32_t prevlen = prev->length;
        //printf("coalesce prev\n");
        // coalesce
        span->start -= prevlen;
        span->length += prevlen;
        // delete span and put it into freelist of span allocator
        DeleteSpan(prev);
        PagemapSet(span);
    }
    // next
    next = PreMerge(span, PagemapGetExact(pid+n));
    //printf("Premerge for next done\n");
    if(next != NULL){
        uint32_t nextlen = next->length;
        //printf("coalesce next\n");
        //coalesce
        span->length += nextlen;
        // delete span and put it into freelist of span allocator
        DeleteSpan(next);
        PagemapSet(span);
    }

    // insert span to free list
    Pageheap_FreelistAppend(span);
    return;
} 

void Pageheap_FreelistAppend(Span_t span){
    span->location = 1;
    uint32_t idx = span->length-1;
    idx = (idx > KMAXPAGES-1) ? KMAXPAGES -1 : idx; 
    span->next = pageheap_.free_[idx];
    if(pageheap_.free_[idx] != NULL){
        pageheap_.free_[idx]->prev = span;
    }
    pageheap_.free_[idx] = span;
    pageheap_.size += span->length*4096;
}

// n is number of bytes
// TODO: add spin lock?? not sure
bool Growheap(uint32_t n){
    uint32_t npages = BytesToPages(n);
    if (npages>KTOTALPAGES){
        return false; 
    }
    uint32_t req = (n>KMINSYSALLOC) ? n : KMINSYSALLOC;

    void* result = sbrk(req); 
    if (result == (void*)-1){
        return false;
    }
    else printf("sbrk(%u) success\n", req);

    //compute page id
    uint32_t pid = (uint32_t)((unsigned long)result >> KPAGESHIFT);

    npages = BytesToPages(req);
    Span_t newspan_t = newspan(pid, npages);
    PagemapSet(newspan_t);
    Pageheap_InsertSpan(newspan_t);

    return true;
}
uint32_t BytesToPages(uint32_t byte){
    uint32_t npages;
    if(byte % 4096==0){
        npages = (uint32_t)((unsigned long)byte >> KPAGESHIFT); 
    }
    else{
        npages = (uint32_t)((unsigned long)byte >> KPAGESHIFT)+1; 
    }
    return npages;
}

//Populate
//Fetch spans from page heap to central free list of index idx
//Requires central_free_lock to be locked
void CentralFreelist_FetchSpan(uint32_t idx){
    // unlock spinlock of central free list because we are going to pageheap
    uint32_t cl = centralfreelist_[idx].size_class_;
    //TODO retry unlock
    /*
    if(!pthread_spin_unlock(&central_free_lock)){
        //printf("unlocked central free list\n");
    }
    else{
        //printf("error on unlocking central free list\n");
    }
    */

    // fetch nbatch for every class of central free list. 
    // TODO: need optimization later on. Using constant for now
    uint32_t npages = NBATCH;
    //uint32_t reqpages = idx*NBATCH/2;
    pthread_spin_lock(&pageheap_lock);
    // Pageheap_New_Safe() fetches npages from pageheap, return NULL if fail(should never happen)
    // safe because it does growheap when there is no span in the pageheap
    Span_t span = Pageheap_New_Safe(npages);
    // location of span should be 3(allocated)
    // unlock pageheap
    pthread_spin_unlock(&pageheap_lock);

    if(span){
        // register size class
    }
    else{
        return;
    }

    // make objects for span
    span->sizeclass = cl;
    char* ptr = (char*)((unsigned long)span->start<<KPAGESHIFT);
    char* endp = (char*)((unsigned long)(span->start+span->length)<<KPAGESHIFT);
    void**iter = &span->objects;
    span->objects = ptr;
    int num=0;
    while((uintptr_t)ptr+cl<=(uintptr_t)endp){
        *iter = ptr;
        iter = (void**)ptr;
        ptr += cl;
        num++;
    }
    *iter = NULL;
    span->refcount = 0;

    // insert into central freelist
    span->location = 2;
    // lock necessary
    //pthread_spin_lock(&central_free_lock); 

    span->next = centralfreelist_[idx].nonempty_;
    if(centralfreelist_[idx].nonempty_ != NULL){
        centralfreelist_[idx].nonempty_ -> prev = span;
    }
    span->prev = NULL;
    centralfreelist_[idx].nonempty_ = span;
    centralfreelist_[idx].num_spans_ += 1;
    centralfreelist_[idx].counter_ += num;

    return;
}

// remove span of corresponding pages from pageheap freelist and return
// NULL if fail, but should not happen!
Span_t Pageheap_New_Safe(uint32_t npages){
    //npages = ()
    Span_t span = Pageheap_SearchAndRemove(npages);
    if(span){
        return span;
    }
    // Growheap of min(n, KMINSYSALLOC) where n=npages>>KPAGESHIFT
    if(!Growheap(npages<<KPAGESHIFT)){
        return NULL;
    }
    return Pageheap_SearchAndRemove(npages);
}

// Search span of npages, remove return. If non, return NULL
Span_t Pageheap_SearchAndRemove(uint32_t npages){
    if(npages<256){
        for(int i=npages;i<=KMAXPAGES;i++){
            if(pageheap_.free_[i-1]){
                // Carve can return NULL at idx 255 because there might not be corresponding size even though Span_t != NULL
                return Carve(pageheap_.free_[i-1], npages);
            }
        }
    }
    else{
        if(pageheap_.free_[255]){
            return Carve(pageheap_.free_[255], npages);
        }
    }
    return NULL;
}

Span_t Carve(Span_t span, uint32_t reqpages){
    Span_t iter = span;
    while((iter->length<reqpages) && (iter != NULL)){
        iter = iter->next;
    }
    if(iter == NULL){
        return iter;
    }
    Pageheap_RemoveFromFreelist(iter);
    if(iter->length == reqpages){
        return iter;
    }
    uint32_t leftpages = iter->length - reqpages;
    uint32_t leftpid = iter->start + reqpages;
    
    Span_t newspan_t = newspan(leftpid, leftpages);
    PagemapSet(newspan_t);
    iter->length = reqpages;
    PagemapSet(iter);
    Pageheap_InsertSpan(newspan_t);
    return iter;
}

bool init_Growheap(){
    return Growheap(KINITHEAPSIZE);
}

void *tc_central_init(){
    init_SpinLock();
    init_Pageheap();
    init_SpanAllocator(); // make pointers to NULL, integers to 0
    init_Pagemap(); // make entries in pagemap into NULL
    if(!init_Growheap()){
        return NULL;
    }
    init_CentralFreelist();
    printf("init_CentralFreelist()\n");

    // for debugging
    //Print_Pageheap();
    //Check_Pagemap();
    //Print_SpanAllocator();
    //Print_CentralFreelist();

    printf("tc_central_init() finished\n");
    return &pageheap_.free_;
}

void *tc_malloc(uint32_t size){
    if(size>=LARGEALLOC){
        void* result = large_malloc(size);
        return result;
    }
    else{
        return small_malloc(size);
    }
}

void* small_malloc(uint32_t size){
    uint32_t cl;
    if(size<=64){
        if(size%8==0){
            cl = size;
        }
        else{
            cl = ((size/8)+1)*8;
        }
    }
    else if(size<=2048){
        if(size%64==0){
            cl = size;
        }
        else{
            cl = ((size/64)+1)*64;
        }
    }
    else if(size<32*1024){
        if(size%256==0){
            cl = size;
        }
        else{
            cl = ((size/256)+1)*256;
        }
    }
    else{
        printf("small alloc size error\n");
        return NULL;
    }
    uint32_t idx = ClassToIndex(cl);
    void** freelist = &threadcache_.list_[idx].list_;
    if(!*(char**)freelist){
        FetchFromCentralFree(idx);
    }
    threadcache_.size_ -= size;

    void* result = threadcache_.list_[idx].list_;
    //Pop
    *(char**)freelist = (void*)(*(char**)(threadcache_.list_[idx].list_));
    threadcache_.list_[idx].length_ --;
    //printf("central free size: %u\n", centralfreelist_[idx].counter_);
    //printf("pageheap size: %lu\n", pageheap_.size);
    return result;
    
}

void tc_free(void*ptr){
    if(ptr==NULL){
        return;
    }
    uint32_t pid = (uint32_t)((uintptr_t)ptr>>KPAGESHIFT);
    Span_t span = pagemap_.central_array[pid];
    uint32_t cl = span->sizeclass;
    if(cl==0){
        do_free_pages(span);
        return;
    }
    free_small(ptr, cl);
    return;
}

void free_small(void* ptr, uint32_t cl){
    uint32_t idx  = ClassToIndex(cl);
    *(char**)ptr = threadcache_.list_[idx].list_;
    threadcache_.list_[idx].list_ = ptr; 
    threadcache_.size_ += cl;
    threadcache_.list_[idx].length_ += 1;
    return;
}

// large free
void do_free_pages(Span_t span){
    span->sizeclass=0;
    span->location=1;
    span->objects=NULL;
    span->refcount=0;
    pthread_spin_lock(&pageheap_lock);
    Pageheap_InsertSpan(span);
    pthread_spin_unlock(&pageheap_lock);
    return;
}

uint32_t ClassToIndex(uint32_t cl){
    uint32_t idx;
    if(cl<=64) {
        idx = (cl/8)-1;
    }
    else if(cl<=2048){
        idx = ((cl-128)/64)+8;
    }
    else if(cl<32*1024){
        idx = (cl/256)+30;
    }
    else{
        printf("error\n");
        return 10000;
    }
}

void* large_malloc(uint32_t size){
    uint32_t pages = BytesToPages(size);
    pthread_spin_lock(&pageheap_lock);
    Span_t span = Pageheap_New_Safe(pages);
    pthread_spin_unlock(&pageheap_lock);
    //printf("pageheap size %lu\n", pageheap_.size);
    //Print_Pageheap();
    //printf("central free counter %u\n",centralfreelist_[8].counter_);
    //printf("req pages: %u\n", pages);
    //Print_Pageheap(); 

    return SpanToPointer(span);
}


void* SpanToPointer(Span_t span){
    return (void*)((uintptr_t)(span->start)<<KPAGESHIFT);   
}
uint32_t Thread_GetFetchSize(uint32_t idx){
    // class size
    uint32_t size = centralfreelist_[idx].size_class_;
    // TODO optimize using maxlength in freelist
    uint32_t numfetch = (4*1024)/size;
    uint32_t MAXFETCHOBJECT = 16;
    if(numfetch<1) numfetch = 1;
    if(numfetch>MAXFETCHOBJECT){
        numfetch = MAXFETCHOBJECT;
    }
    return numfetch;
}

void init_Freelist(Freelist* freelist){
    freelist->list_ = NULL;
    freelist->length_ = 0;
    freelist->max_length_ = 1; 
    return;
}

// fetch objects from central freelist
void* FetchFromCentralFree(uint32_t idx){
    uint32_t batchsize = Thread_GetFetchSize(idx);
    int objsize = centralfreelist_[idx].size_class_;
    void *start;
    void *end;
    Freelist* freelist = &threadcache_.list_[idx];
    int numfetch = CentralFreelist_RemoveRange(idx, &start, &end, batchsize);
    if(batchsize != numfetch){
        printf("error on fetching objects from central freelist\n");
        return NULL;
    }

    threadcache_.size_ += numfetch*objsize;
    //append to free list
    freelist->list_ = start;
    freelist->length_ += numfetch;
    // double max length every time fetchsize is larger than max length
    if (batchsize > freelist->max_length_){
        freelist->max_length_ = batchsize*2;
    }
    return start;
}

// Helper function for CentralFreelist_RemoveRange
// Remove object from span, return n fetched, set start point and end
// move span into empty if all objects are allocated
int RemoveObjectFromSpan(uint32_t idx, Span_t span, uint32_t batchsize, void** start, void** end){
    void* iter;
    int cnt = 0;

    *start = span->objects;
    if(span->objects) cnt++;
    iter = *start;
    void* prev = *start;
    while((char**)iter){
        iter = (void*)(long)(*(char**)iter);
        if(cnt>=batchsize){
            span->refcount += cnt;
            *end = prev;
            // Remove
            span->objects = (void*)(long)(*(char**)prev);
            *(char**)prev = NULL;
            centralfreelist_[idx].counter_ -= cnt;
            return cnt;
        }
        if(iter){
            cnt++;
            prev = iter;
        }
    }
    span->refcount += cnt;
    *end = prev;
    //Remove
    span->objects = (void*)(long)(*(char**)prev);
    *(char**)prev = NULL;
    centralfreelist_[idx].counter_ -= cnt;
    return cnt;
}

// Remove objects of corresponding index by batch size from central free list and assign to start and end. Return number of objects fetched (=batchsize) Return 0 on error
int CentralFreelist_RemoveRange(uint32_t idx, void** start, void** end, uint32_t batchsize){
    pthread_spin_lock(&central_free_lock);
    if(centralfreelist_[idx].counter_ < batchsize){
        // fetch from pageheap
        CentralFreelist_FetchSpan(idx);
    }
    Span_t iter = centralfreelist_[idx].nonempty_;
    int n = 0;
    int cnt;
    int left = batchsize;
    char* cur_start = NULL;
    char*prev_start = NULL;
    char* cur_end = NULL;
    char*prev_end = NULL;
    Span_t tmp_head = iter;
    while(iter){
        n++;
        cnt = RemoveObjectFromSpan(idx, iter, left, (void**)&cur_start, (void**)&cur_end); 

        // move span to empty slot if all objects are allocated
        if(iter->objects == NULL){
            tmp_head = iter->next;
            if(iter->prev == NULL){
                centralfreelist_[idx].nonempty_ = iter->next;
            }
            else{
                iter->prev->next = iter->next;
            }
            if(iter->next != NULL){
                iter->next->prev = iter->prev;
            }
            iter->prev = NULL;
            // prepend to empty_
            iter->next = centralfreelist_[idx].empty_;
            centralfreelist_[idx].empty_ = iter;
            if(iter->next){
                iter->next->prev = iter;
            }
            iter = tmp_head;
        }
        // first cycle
        if(n==1){
            *start = cur_start;
        }
        left -= cnt;
        if(prev_end){
            *(char**)prev_end = (cur_start);
        }
        prev_start = cur_start;
        prev_end = cur_end;
        if(left==0){
            *end = cur_end;
            (*(char**)(*(char**)end)) = NULL;
            pthread_spin_unlock(&central_free_lock);
            return batchsize-left;
        }
    }
    //shouldn't reach here
    assert(0);
    return 0;
}

// run per thread
void *tc_thread_init(){
    //tcinit ++;
    threadcache_.size_ = 0;  
    for(int idx=0;idx<KCLASSSIZES;idx++){
        init_Freelist(&threadcache_.list_[idx]);
        if(!FetchFromCentralFree(idx)){
            printf("fetchfromcentralfreelist failed\n");
            return NULL;
        }
    }
    return &threadcache_.list_;
    
}

void Print_Pageheap(){
    uint32_t cl=0;
    for(cl=0; cl<KMAXPAGES;cl++){
        Span_t start = pageheap_.free_[cl];
        Span_t iter = start;
        while(iter != NULL){
            printf("-----------------\n");
            printf("<print_pageheap>\n");
            printf("\n");
            printf("overall size of pageheap: %lu\n", pageheap_.size);
            printf("span at class %u\n", cl);
            printf("starting page of span: %u\n", iter->start);
            printf("length of span: %u\n", iter->length);
            printf("next addr of span: %p\n", iter->next);
            printf("prev addr of span: %p\n", iter->prev);
            printf("pointer to objects: %p\n", iter->objects);
            printf("location of span: %u\n", iter->location);
            printf("refcount of span: %u\n", iter->refcount);
            printf("sizeclass of span: %u\n", iter->sizeclass);
            printf("-----------------\n");

            iter = iter->next;
        }
    }
    return;
}

// run per thread
void Print_ThreadCache(){
    //printf("length of free list of byte 8: %d\n",threadcache_.list_[0].length_);
    void*iter;
    int n;
    int occupiedsize=0;
    for (int idx=0;idx<KCLASSSIZES;idx++){
        n=0;

        iter = threadcache_.list_[idx].list_;
        if(iter){ 
            n++;
            while(*(char**)iter){
                iter = (void*)(*(char**)iter);
                n++;
            }
        }
        uint32_t cl = IndexToClass(idx);
        occupiedsize += n*cl;
        if(threadcache_.list_[idx].length_ != n){
            printf("n: %d recorded length: %d\n", n, threadcache_.list_[idx].length_);
        }
        //printf("freelist length for index %d = %d\n", idx, n);
    }
    printf("length of thread cache index 3: %d\n",threadcache_.list_[3].length_);
    //printf("threadcache size: %d\n", threadcache_.size_);
    printf("thread cache has right values\n");
    return;
}

void check(Span_t span){
    if(pagemap_.central_array[span->start] != pagemap_.central_array[span->start+span->length-1]){
        printf("1error!!\n");
    }
    for(int i=span->start+1;i<span->start+span->length-1;i++){
        if(pagemap_.central_array[i] && (pagemap_.central_array[i] != span)){
            printf("2error!!\n");
            printf("pointer: %p\n", pagemap_.central_array[i]);
            printf("-----------\n");
        }
    }
    return;
}

void Check_Pagemap(){
    uint32_t idx = 0;
    Span_t span;
    for(idx=0;idx<(1<<20);idx++){
        span = pagemap_.central_array[idx];
        if (span != NULL){
            check(span); 
        }
    }
    return;
}

void Print_SpanAllocator(){
    printf("<print spanallocator>\n");
    printf("\n");
    printf("inuse : %u\n", spanallocator_.inuse_);
    printf("free_area_ : %p\n", spanallocator_.free_area_);
    printf("free_avail_: %u\n", spanallocator_.free_avail_);
    printf("free_list_: %p\n", spanallocator_.free_list_);
    printf("-----------------\n");
    return;
}

void Print_CentralFreelist(){
    for(int idx=0;idx<KCLASSSIZES;idx++){
        if(centralfreelist_[idx].nonempty_){
            printf("<central freelist index : %d>\n", idx);
            printf("\n");
            printf("sizeclass : %u\n", centralfreelist_[idx].size_class_);
            printf("num_spans : %u\n", centralfreelist_[idx].num_spans_);
            printf("number of free objects : %u\n", centralfreelist_[idx].counter_);
            Span_t iter = centralfreelist_[idx].nonempty_;
            int cnt = 1;
            while(iter){
                printf("-----------------\n");
                printf("span %d in nonempty-> startpoint : %u/ length: %u/ location: %u/ sizeclass: %u refcount: %u\n", cnt, iter->start, iter->length,iter->location, iter->sizeclass, iter->refcount);
                printf("-----------------\n");
                cnt ++;
                iter = iter->next;
            }
        }
    }
    return;
}
