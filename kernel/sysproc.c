#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_mmap(void)
{ 
  // addr (first argument) will be always 0
  int length, prot, flags, fd;
  argint(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);

  struct proc *p = myproc();
  // File is not writable but mapped with PROT_WRITE, but mapped
  // with MAP_PRIVATE is still writable since writes are not flushed
  // to disk
  if (!p->ofile[fd]->writable && (prot & PROT_WRITE) && !(flags & MAP_PRIVATE)) {
    return -1;
  }

  struct vm_area_struct *vma = 0;
  // Allocate a new VMA struct
  for (int i = 0; i < NVMA; ++i) {
    if (p->vmas[i].valid == 0) {
      vma = &p->vmas[i];
      break;
    }
  }
  if (vma == 0) {
    return -1;
  }

  vma->vm_start = p->sz;
  vma->vm_length = length;
  vma->vm_prot = prot;
  vma->vm_flags = flags;
  vma->vm_file = p->ofile[fd];
  vma->valid = 1;
  filedup(vma->vm_file);

  p->sz += length;  // Lazy allocation

  return vma->vm_start;
}

uint64
sys_munmap(void)
{ 
  // Only unmap at the start, or at the end, or the whole region
  // (but not punch a hole in the middle of a region)
  uint64 addr;
  int length;
  argaddr(0, &addr);
  argint(1, &length);

  struct proc *p = myproc();
  struct vm_area_struct *vma = 0;
  for (int i = 0; i < NVMA; ++i) {
    vma = &p->vmas[i];
    if (vma->valid && vma->vm_start <= addr && addr < vma->vm_start + vma->vm_length) {
      break;
    }
  }
  if (vma == 0) {
    return -1;
  }
  // Write back the data to disk if the file is mapped with MAP_SHARED
  if (vma->vm_flags & MAP_SHARED) {
    pte_t *pte = walk(p->pagetable, addr, 0);
    // Only write back dirty pages
    if (*pte & PTE_D) {
      if (filewrite(vma->vm_file, addr, length) != length) {
        printf("munmap: filewrite failed\n");
        return -1;
      }
    }
  }
  // Unmap the file (some pages may not be mapped with actual physical address
  // due to lazy allocation)
  uvmunmap(p->pagetable, addr, length / PGSIZE, 1);
  // Unmap at the start of the region
  if (addr == vma->vm_start) {
    vma->vm_start += length;
  }
  vma->vm_length -= length;
  if (vma->vm_length == 0) {
    fileclose(vma->vm_file);
    vma->valid = 0;
  }
  return 0;
}