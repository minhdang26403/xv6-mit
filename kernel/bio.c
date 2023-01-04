// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

// a bucket of the hash table
struct bbucket{
  struct spinlock lock; // lock per bucket
  struct buf head;  // LRU circular doubly-linked list of buffers
};

struct {
  struct spinlock lock; // global lock, used for finding unused buffers from other buckets
  struct buf buf[NBUF]; // list of buffers
  struct bbucket buckets[NBUCKET]; // table of buckets
} bcache;

void
insert_buf(struct bbucket *bucket, struct buf *buf)
{ 
  // insert a buffer into the beginning of the LRU linked list
  buf->next = bucket->head.next;
  buf->prev = &bucket->head;
  bucket->head.next->prev = buf;
  bucket->head.next = buf;
}

void
binit(void)
{

  initlock(&bcache.lock, "bcache");

  struct bbucket *bucket;
  // init lock for each bucket
  for (bucket = bcache.buckets; bucket < bcache.buckets + NBUCKET; ++bucket) {
    bucket->head.prev = &bucket->head;
    bucket->head.next = &bucket->head;
    initlock(&bucket->lock, "bcache.bucket");
  }

  // init lock for each buffer
  for (int i = 0; i < NBUF; ++i) {
    initsleeplock(&bcache.buf[i].lock, "buffer");
    insert_buf(&bcache.buckets[i % NBUCKET], &bcache.buf[i]);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct bbucket *bucket;

  int idx = blockno % NBUCKET;
  bucket = &bcache.buckets[idx];
  
  acquire(&bucket->lock);
  // Is the block already cached?
  for (b = bucket->head.next; b != &bucket->head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bucket->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer in the same bucket
  for(b = bucket->head.prev; b != &bucket->head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bucket->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bucket->lock);

  // Steal buffer from other buckets
  // Must acquire the global lock since we scan the whole hash table
  acquire(&bcache.lock);

  // scan the buffer pool once again since another thread may cache this buffer
  for (b = bcache.buf; b < bcache.buf + NBUF; ++b) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  int found;
  for (int i = 0; i < NBUCKET; ++i) {
    bucket = &bcache.buckets[i];
    acquire(&bucket->lock);
    found = 0;
    for (b = bucket->head.prev; b != &bucket->head; b = b->prev) {
      if (b->refcnt == 0) {
        found = 1;
        break;
      }
    }
    if (found) {
      release(&bcache.lock);
      break;
    } else {
      release(&bucket->lock);
    }
  }

  if (found == 0) {
    panic("bget: no buffers");
  }

  // remove buf from the current bucket
  b->next->prev = b->prev;
  b->prev->next = b->next;
  // reset the attributes of this buffer
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  release(&bucket->lock);

  acquire(&bcache.buckets[idx].lock);
  insert_buf(&bcache.buckets[idx], b);
  release(&bcache.buckets[idx].lock);
  
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int idx = b->blockno % NBUCKET;
  struct bbucket *bucket = &bcache.buckets[idx];
  acquire(&bucket->lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // remove the buffer from this bucket
    b->next->prev = b->prev;
    b->prev->next = b->next;
    // insert it back to the head of the linked list
    insert_buf(bucket, b);
  }
  
  release(&bucket->lock);
}

void
bpin(struct buf *b) {
  int idx = b->blockno % NBUCKET;
  struct bbucket *bucket = &bcache.buckets[idx];
  acquire(&bucket->lock);
  b->refcnt++;
  release(&bucket->lock);
}

void
bunpin(struct buf *b) {
  int idx = b->blockno % NBUCKET;
  struct bbucket *bucket = &bcache.buckets[idx];
  acquire(&bucket->lock);
  b->refcnt--;
  release(&bucket->lock);
}


