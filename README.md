# tcmalloc
c implementation of tcmalloc by Junha Hyung (original implementation from Google)

## How to compile
for testing : gcc -pthread tc_malloc.c test_main.c
for using in other code : include tcmalloc_api.h

## code explanation
### Structure
span allocator – 스팬 스트럭쳐를 할당해주는 구조체이다. Sbrk 를 자주 하면 속도가 저하되기 때문에 미리 sbrk 로 스팬 구조체를 위한 공간을 만들어놓고, 새로운 스팬이 필요할때마다 sizeof(struct span)만큼 공간을 내어준다.

AllocateNewSpan() – newspan()함수를 도와주는 함수로서 span allocator 에서 스팬 구조체를 위한 공간을 준다

Newspan(p, len) – allocateNewspan()으로부터 스팬구조체를 위한 공간을 받아온 후, 스팬의 시작을 p, 길이를 len 으로 설정해준다. 그외 initialization 을 해준다.
Deletespan(span) – spanallocator 의 freelist 에 스팬 구조체를 넣어준다. 스팬 삭제할때 사용한다.

Pagemapget/pagemapset() – pagemap 의 엔트리를 가져오고/설정할때 사용한다. 

### Pageheap
Premerge(span, other) – 두개의 span 이 coalesce 될 수 있는지 확인한다. 스팬이 인접해있는지, 스팬이 현재 위치한 곳은 어디인지 등을 확인한다. 만약 가능하다면, other 을 freelist 에서 뺀다.

Pageheap_RemoveFromFreelist(span) – span 을 페이지힙의 프리리스트에서 뺀다. Pageheap_InsertSpan(span) – span 을 premerge 를 통해 합칠수 있는지 확인한후 합치고,아래 함수를 이용해 페이지힙의 freelist 에 넣는다. Pageheap_FreelistAppend(span) – 위 함수의 helper function 이다.

Pageheap_New_Safe(npages) – n 개의 페이지를 페이지힙 프리리스트에서 빼온다. 만약 없다면, growheap 을 통해 heap 공간을 늘려준후 다시 시도한다.

Pageheap_SearchAndRemove(npages) – 위 함수의 helper function 으로, 페이지힙을 index npages-1 부터 돌며 알맞은 페이지를 프리리스트에서 꺼내온다

Carve(span, npages) – 스팬에서 npages 만큼을 취하고, 남는 부분은 스팬을 새로 만들어 남은 부분을 지정해주고 알맞은 페이지힙 위치에 삽입한다. 위 함수에서 사용된다. Growheap(n) – n bytes 만큼 sbrk 를 통해 힙 공간을 늘려주고, 그 공간을 하나의 스팬으로 만든 후 initialize 해주고 프리리스트에 집어넣고 pagemap 에 알맞은 인덱스가 이 새로운 스팬을가리키도록 한다.

CentralFreelist_FetchSpan(idx) – 센트럴 프리리스트의 인덱스 idx 에 해당하는 곳에 스팬을 가져온다(pageheap 에서). 페이지힙으로 갈때 페이지힙에 스핀락을 걸어준다. 가져올때 Pageheap_new_safe() 함수를 사용한다.

CentralFreelist_RemoveRange(uint32_t idx, void**start, void**end, uint32_t batchsize); - central freelist 에서 object 들을 뺄때 사용한다.인덱스 Idx 에 해당하는 곳에서 빼오고, batchsize 만큼 가져온다. 센트럴 프리리스트에 락을 걸어주어야 한다. Start 와 end 에 각각 가져온 첫 오브젝트와 마지막 오브젝트 포인터값을 넣어준다.

### Malloc Free
(api 함수에서 직접적으로 사용되는 함수로, 오브젝트나 페이지의 크기에 따라서 어떤 함수를 쓸지 api 함수가 결정한다.)

Large_malloc(size) – 대용량 맬록.(32k>=) pageheap 에 락을 걸어준 후 pageheap 에서 직접 스팬을 가져온다. 스팬에서 페이지의 첫 포인터를 리턴해준다

Small_malloc(size) – 소규모 맬록. Thread cache 에서 바로 오브젝트를 빼서 주고, 만약 없다면 FetchFromCentralFree() 함수를 통해 central free list 에서 오브젝트를 가져온다.

Do_free_pages(span) – 대용량 프리. Pageheap 에 Lock 을 걸어주고 Pageheap_InsertSpan 을 통해 pageheap 프리리스트에 직접 스팬을 넣어준다.

Free_small(ptr, cl) – 소규모 프리. Threadcache 에 오브젝트를 넣어준다.

Threadcache_ : 쓰레드 캐시 스트럭처이다. 각 쓰레드 마다 하나씩 존재해야하기 때문에 Thread_local_storage (__thread) 커맨드를 이용해 만들었다.
