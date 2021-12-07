# 写在前面
这次的实验与上一个实验（懒分配）很相似，实现写时复制，实验地址在[这里](https://pdos.csail.mit.edu/6.828/2020/labs/cow.html)。与以往不同的是，这次只有一个任务，虽然是 hard 难度，但写起来并没有很吃力，应该是我写的最快的一个实验了。思路很简单，主要是容易出现各种 bug，debug 的过程会比较煎熬，。我的代码在 [github](https://github.com/computer-net/MIT-6.S081-2020/tree/master/lab6-cow) 上。

在 debug 的时候，有一些我觉得没啥问题的代码出现了错误，有的改完后也不是很理解\~\~ debug 的时候，发现别人都在强调进程安全问题，我也觉得很有道理，写的时候也加上了，然后发现并没有什么用。在实验都写完后，我把进程锁又删了，发现没什么问题，也能通过所有的测试用例。（理论上，我觉得应该加上读写锁的，保证进程安全；实际上，加了好像没啥用？！）

# 实验内容

实验只有一个任务，就是实现一个内存的写时复制机制（copy-on-write fork），也称为 COW。为什么要实现这个功能呢，主要原因是：

> **在 shell 中执行指令时，首先会 fork 一个子进程，然后在子进程中使用 exec 执行 shell 中的指令。在这个过程中，fork 需要完整的拷贝所有父进程的地址空间，但在 exec 执行时，又会完全丢弃这个地址空间，创建一个新的，因此会造成很大的浪费。**

为了优化这个特定场景（fork 时）的内存利用率，我们可以在 fork 时，并不实际分配内存（与上一个实验的懒分配很像），而是让**子进程和父进程共享相同的内存区域（页表不同，但指向的物理地址相同）**。但为了保证进程之间的隔离性，我们不能同时对这块区域进行写操作，因此，**设置共享的内存地址只有读权限。当需要向内存页中写入数据时，会触发缺页中断，此时再拷贝一个内存页**，更新进程的页表，将内容写进去，然后重新执行刚才出错的指令。

 在这个过程中，**需要为每个物理内存页保存一个指向该内存页的页表数量**。当为 0 时，表示没有进程使用该内存页，可以释放了；大于 1 时，每当有进程释放该内存页时，将对应的数值减一。

需要注意的是，这里要标记写入内存是 COW 场景。否则，如果真的有一个页面只能读不能写的话，就会出现问题。这里我们使用的是 PTE 页表项保留的标记位 RSW。

另一个知识点：**在XV6中，除了 trampoline page 外，一个物理内存 page 只属于一个用户进程**。

## 任务说明

有两个场景需要处理 cow 的写入内存页场景：

- 一个是用户进程写入内存，此时会触发 page fault 中断（15号中断是写入中断，只有这个时候会触发 cow，而13号中断是读页面，不会触发 cow）；
- 另一个是直接在内核状态下写入对应的内存，此时不会触发 usertrap 函数，需要另做处理。

总结起来，实验总共有以下四个步骤。

### 第一步，创建 page 的计数数组

首先对每个物理页面创建一个计数变量，保存在一个数组中，页面的数目就是数组的长度。这里有一个知识点：不是所有的物理内存都可以被用户进程映射到的，这里有一个范围，即 KERNBASE 到 PHYSTOP。具体映射可以从 xv6 手册中看到：

![xv6 va和pa的映射关系](https://img-blog.csdnimg.cn/6176a3695be042dbb506bbe65dc745dc.png#pic_center)

由于一个页表的大小（PGSIZE）是 4096，因此数组的长度可以定义为：`(PHYSTOP - KERNBASE) / PGSIZE`

- 我们可以先在 kernel/kalloc.c 中定义一个用于计数的数组：

```c
uint page_ref[(PHYSTOP - KERNBASE) / PGSIZE];
```

由于是全局变量，C 语言会自动初始化为 0。

- 分配内存时增加数值：

在 `kernel/riscv.h` 中定义 COW 标记位和计算物理内存页下标的宏函数：

```c
#define PTE_COW (1L << 8)  // copy on write
#define COW_INDEX(pa) (((uint64)(pa) - KERNBASE) >> 12)
```

**带参数的宏，参数需要用括号括起来，避免参数是表达式时引起歧义**。

在 kalloc 时，设置值为 1：

```c
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    page_ref[COW_INDEX(r)] = 1;
  }
  return (void*)r;
}
```

- 在使用 kfree 释放内存页时，首先需要判断计数值是否大于 1：

```c
void
kfree(void *pa)
{
  struct run *r;  

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  //acquire(&ref_lock);
  if(page_ref[COW_INDEX(pa)] > 1) {
    page_ref[COW_INDEX(pa)]--;
    //release(&ref_lock);
    return;
  }
  page_ref[COW_INDEX(pa)] = 0;
  //release(&ref_lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}
```

### 第二步，uvmcopy

在创建好计数数组后，在 fork 时，直接使用原来的物理页进行映射：

在 kernel/vm.c 中修改 uvmcopy 函数：
```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  //char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    *pte = (*pte & ~PTE_W) | PTE_COW;
    flags = PTE_FLAGS(*pte);
    //if((mem = kalloc()) == 0)
    //  goto err;
    //memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, pa, flags) != 0){
      //kfree(mem);
      goto err;
    }
    //acquire(&ref_lock);
    page_ref[COW_INDEX(pa)]++;
    //release(&ref_lock);
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

需要在代码前面添加 extern 声明，用于引入外部的变量：

```c
extern uint page_ref[]; // kalloc.c
```

### 第三步，处理中断 usertrap

和上一个实验相同，在 usertrap 中添加中断处理逻辑：

```c
 else if(r_scause() == 15) {
    uint64 va = r_stval();
    if(va >= p->sz)
      p->killed = 1;
    else if(cow_alloc(p->pagetable, va) != 0)
      p->killed = 1;
  }
```

其中的 cow_alloc 函数，在 kalloc.c 中实现，并在 defs.h 中进行声明：

```c
int
cow_alloc(pagetable_t pagetable, uint64 va) {
  va = PGROUNDDOWN(va);
  if(va >= MAXVA) return -1;
  pte_t *pte = walk(pagetable, va, 0);
  if(pte == 0) return -1;
  uint64 pa = PTE2PA(*pte);
  if(pa == 0) return -1;
  uint64 flags = PTE_FLAGS(*pte);
  if(flags & PTE_COW) {
    uint64 mem = (uint64)kalloc();
    if (mem == 0) return -1;
    memmove((char*)mem, (char*)pa, PGSIZE);
    uvmunmap(pagetable, va, 1, 1);
    flags = (flags | PTE_W) & ~PTE_COW;
    //*pte = PA2PTE(mem) | flags;
	if (mappages(pagetable, va, PGSIZE, mem, flags) != 0) {
      kfree((void*)mem);
      return -1;
    }
  }
  return 0;
}
```

这里，需要判断很多异常情况，测试用例会测试到。对错误的异常处理需要异常谨慎，，我就是在这里 debug 了很久，。。

### 第四步，内核写内存 copyout

这里直接调用上面写的 cow_alloc 函数即可（本来自己写了一下，发现一直出现各种问题，索性直接调用上面的函数，发现完全没问题，。）。

```c
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if (cow_alloc(pagetable, va0) != 0)
      return -1;
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    pte = walk(pagetable, va0, 0);
    if(pte == 0)
      return -1;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```

最后注意在 defs.h 中添加 walk 声明。

# 总结
这个实验是我完成最快但 debug 最久的实验，。之前的实验很长时间都在考虑怎么实现，这个实验主要都是在 debug，。

文章同步在[知乎]()。
# 参考文献（博客）
1. [【MIT6.S081】 lab6 cow pages](https://zhuanlan.zhihu.com/p/406265385)
2. [MIT 6.S081 Lab6 CopyOnWrite](https://blog.csdn.net/newbaby2012/article/details/120048430?utm_medium=distribute.pc_relevant.none-task-blog-2~default~baidujs_title~default-0.no_search_link&spm=1001.2101.3001.4242.1)
