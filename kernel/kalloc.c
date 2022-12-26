// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define NUM_PAGE ((PHYSTOP - KERNBASE) / PGSIZE)
#define PA2IDX(pa) ((pa - KERNBASE) / PGSIZE)

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  uint32 refcount[NUM_PAGE];
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kmem.refcount[PA2IDX((uint64)p)] = 1;
    kfree(p);
  }
  
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  int pn = PA2IDX((uint64)pa);
  if (kmem.refcount[pn] < 1) {
    panic("kfree: refcount");
  }
  kmem.refcount[pn]--;
  int tmp = kmem.refcount[pn];
  release(&kmem.lock);
  if (tmp > 0) {
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    int pn = PA2IDX((uint64)r);
    if (kmem.refcount[pn] > 0) {
      panic("kalloc: free page has non-zero reference count");
    }
    kmem.freelist = r->next;
    kmem.refcount[pn] = 1;
  }
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}

void
increfcount(uint64 pa)
{ 
  int pn = PA2IDX((uint64)pa);
  acquire(&kmem.lock);
  if (kmem.refcount[pn] < 1 || pa >= PHYSTOP) {
    panic("increfcount");
  }
  kmem.refcount[pn]++;
  release(&kmem.lock);
}