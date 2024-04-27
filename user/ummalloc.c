#include "kernel/types.h"

#include "user/user.h"

#include "ummalloc.h"

#define ALIGNMENT 8
#define INTSIZE 4
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + ALIGNMENT + (ALIGNMENT - 1)) & ~0x7)
// #define SIZE_T_SIZE (ALIGN(sizeof(uint)))

#define getprev(p) ((char*)(*(unsigned long*)(p)))
#define setprev(p, ptr) (*(unsigned long*)(p) = (unsigned long)(ptr))
#define getnext(p) ((char*)(*(unsigned long*)(p + ALIGNMENT)))
#define setnext(p, ptr) (*(unsigned long*)((char*)(p) + ALIGNMENT) = (unsigned long)(ptr))
#define hsp(p) ((char* )(p) - INTSIZE)
#define tsp(p) ((char* )(p) + getsize(hsp(p)) - ALIGNMENT)
#define getsize(p) ((*(unsigned int*)(p)) & ~0x7)
#define getalloc(p) ((*(unsigned int*)(p)) & 0x1)
#define wsize(p, val) (*(unsigned int*)(p) = val)

#define nextblockp(p) ((char*)(p) + getsize(((char*)(p) - INTSIZE)))
#define prevblockp(p) ((char*)(p) - getsize(((char*)(p) - ALIGNMENT)))
#define getasize(size) ((size) <= 2*ALIGNMENT ? 3*ALIGNMENT : ALIGN(size))
void setsize(void* p, uint size){
	wsize(hsp(p), size);
	wsize(tsp(p), size);
}
static char* headp;
static char* tailp;
static void alloc_free(void* p, uint asize){
	uint size = getsize(hsp(p));
	void* ptr;
	if (size - asize < 3*ALIGNMENT){//无需切分 直接分配
		setsize(p, size|1);
		setprev(getnext(p), getprev(p));
		setnext(getprev(p), getnext(p));
	}
	else {
		setsize(p, asize|1);
		ptr = nextblockp(p);
		setnext(ptr, getnext(p));
		setprev(ptr, getprev(p));
		setnext(getprev(ptr), ptr);
		setprev(getnext(ptr), ptr);
		setsize(ptr, size-asize);
	}
}
static void* find_free(uint asize){
	void* p;
	for (p = getnext(headp); p != tailp; p = getnext(p))
		if (asize <= getsize(hsp(p))) return p;
	return 0;
}

static void* merge(void* ptr){
	uint prev_used = getalloc(tsp(prevblockp(ptr)));
	uint next_used = getalloc(hsp(nextblockp(ptr)));
	uint size = getsize(hsp(ptr));
	
	void* prevptr = prevblockp(ptr);
	void* nextptr = nextblockp(ptr);
	if (prev_used && next_used) return ptr;
	else if (!prev_used && !next_used){
		size = size + getsize(hsp(prevptr)) + getsize(tsp(nextptr));
		setprev(getnext(nextptr), prevptr);
		setnext(prevptr, getnext(nextptr));
		wsize(hsp(prevptr), size);
		wsize(tsp(nextptr), size);
		ptr = prevptr;
	} else if (!prev_used && next_used){
		size += getsize(tsp(prevptr));
		setprev(getnext(ptr), prevptr);
		setnext(prevptr, getnext(ptr));
		wsize(hsp(prevptr), size);
		wsize(tsp(ptr), size);
		ptr = prevptr;
	} else if (prev_used && !next_used){
		size += getsize(hsp(nextptr));
		setprev(getnext(nextptr), ptr);
		setnext(ptr, getnext(nextptr));
		wsize(hsp(ptr), size);
		wsize(tsp(nextptr), size);
	}
	return ptr;
}
static void* extend(uint num){
	char* ptr;
	char* p;
	uint size;
	if(num % 2 == 1) size = num * INTSIZE + INTSIZE;
	else size = num * INTSIZE;
	if ((p = sbrk(size)) == (void*)-1) return 0;
	ptr = tailp;
	setsize(ptr, size);
	tailp = nextblockp(ptr);
	setnext(ptr, tailp);
	wsize(hsp(tailp), 0|1);
	setprev(tailp, ptr);
	return merge(ptr);
}

int mm_init(void){
	if ((headp = sbrk(4 * INTSIZE + 3 * ALIGNMENT)) == (void*)-1) return -1;
	wsize(headp, 0); //设置填充
	headp += ALIGNMENT;
	setsize(headp, 3*ALIGNMENT|1);
	tailp = nextblockp(headp);
	setprev(headp, 0);
	setnext(headp, tailp);
	wsize(hsp(tailp), 0|1);
	setprev(tailp, headp);
	if (extend((1<<12)/INTSIZE) == 0) return -1;//预分配1<<12
    return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void* mm_malloc(uint size){
	char* p;
  	uint asize;
	uint extendsize;
	asize = getasize(size);
	if ((p = find_free(asize)) != 0){
		alloc_free(p, asize);
		return p;
	}
	extendsize = asize;
	if ((p = extend(extendsize/INTSIZE)) == 0)return 0;
	alloc_free(p, asize);
	return p;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void* ptr){
  	char* p;
	uint size = getsize(hsp(ptr));
	for (p = getnext(headp); ; p = getnext(p)){
		if (ptr < (void*)p){
			setsize(ptr, size);
			setnext(ptr, p);
			setprev(ptr, getprev(p));
			setnext(getprev(p), ptr);
			setprev(p, ptr);
			break;
		}
	}
	merge(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void* mm_realloc(void* ptr, uint size){
	if (ptr == 0) return mm_malloc(size);
	else if (size == 0){
		mm_free(ptr);
		return 0;
	}
	uint nowsize;
	uint asize;
	asize = getasize(size);
	nowsize = getsize(hsp(ptr));
  	void* oldptr = ptr;
	void* prev;
	void* next;
    void* newptr;
	void* nextptr;
    uint extendsize;
	uint totalsize;
	char* p;
	if (asize == nowsize) return ptr;
	else if (asize < nowsize){
		if (nowsize-asize >= 3*ALIGNMENT){	
			setsize(ptr, asize|1);
			nextptr = nextblockp(ptr);
			setsize(nextptr, nowsize-asize);
			for (p = getnext(headp); ; p = getnext(p)){
				if (nextptr < (void*)p){
					prev = getprev(p);
					next = p;
					setprev(nextptr, prev);
					setnext(nextptr, next);
					setnext(prev, nextptr);
					setprev(p, nextptr);
					break;
				}
			}
		} 
		return ptr;
	}
	else{
		nextptr = nextblockp(ptr);
		totalsize = getsize(hsp(nextptr))+nowsize;
		if (getalloc(hsp(nextptr)) || totalsize < asize){
			newptr = find_free(asize);
			if (newptr == 0){
        		extendsize = asize;
				if ((newptr = extend(extendsize/INTSIZE)) == 0) return 0;
			}
			alloc_free(newptr, asize);
			memcpy(newptr, oldptr, nowsize-2*INTSIZE);
			mm_free(oldptr);
			return newptr;
		}
		else{
			prev = getprev(nextptr);
			next = getnext(nextptr);
			if (totalsize-asize >= 3*ALIGNMENT){
				setsize(ptr, asize|1);
				nextptr = nextblockp(ptr);
				setsize(nextptr, totalsize-asize);
				setprev(nextptr, prev);
				setnext(nextptr, next);
				setnext(prev, nextptr);
				setprev(next, nextptr);
			} else{
				setsize(ptr, totalsize|1);
				setnext(prev, next);
				setprev(next, prev);
			}
			return ptr;
		}
	}
}
