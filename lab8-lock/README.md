# 介绍
## 写在前面
最近有点忙，。。拖了一周多的时间才开始写这个实验。。

这次实验的目的是设计锁，主要目的是降低多线程情况下对锁的竞争，实验地址在 [Lab: locks](https://pdos.csail.mit.edu/6.828/2020/labs/lock.html)。实验的整体思路还是比较简单的：**就是降低锁的粒度，将一个大锁更换为一些粒度小的锁，这样可以大幅度降低锁的竞争**。我的代码在 [github](https://github.com/computer-net/MIT-6.S081-2020/tree/master/lab8-lock)。

关于这次实验，对应的讲义没有太多有帮助的内容，可以直接上手做实验。我是先看了一下一遍对应的课程，虽然跟这次实验关系不是很大，但还是在下面记录一下。

## 课程内容
### 线程切换到调度器线程的执行过程

1. 先获取自己的锁；
2. 将自己的状态从 RUNNING 设置为 RUNNABLE；
3. 调用switch函数（其实是调用 sched 函数，在 sched 函数中再调用的 switch 函数）；
4. switch 函数将当前的线程切换到调度器线程；
5. 调度器线程之前也调用了switch函数，现在恢复执行会从自己的switch函数返回；
6. 返回之后，调度器线程会释放刚刚出让了CPU的进程的锁。

**最开始获取锁的时候，是因为防止其他 cpu 遍历进程表单，检测到该线程的状态为 RUNNABLE，进而执行该线程。在调用 switch 函数时也不释放锁。而是在调度器线程中，在进程的线程完全停止使用自己的栈之后，再释放进程的锁。**

注意：**在执行switch函数的过程中，不能持有任何其他的锁。只能持有一个该进程的锁。**（在XV6中，死锁是通过禁止在线程切换的时候加锁来避免的。）

### Sleep&Wakeup 接口

当线程利用 sleep 出让 cpu 进行等待时，线程调用sleep函数并等待一个特定的条件。当特定的条件满足时，代码会调用wakeup函数。**这里的sleep函数和wakeup函数是成对出现的， sleep函数和wakeup函数都带有一个叫做sleep channel的参数。**

### Lost wakeup

简单的说，就是只含有一个 sleep channel 参数的sleep是有缺陷的：
1. 在 sleep 的时候是否应该获取共享变量的锁，如果获取的话，其他线程不能获取锁来对其进行改变，永远停留在判断共享变量的循环上；
2. 另一种方案是在 sleep 之前先释放锁，在返回后，再获取锁，这样保护了共享变量；但一旦释放了锁，当前CPU的中断会被重新打开，会导致在该线程还没 sleep 的时候，对应的 wakeup 已经执行了，这时就会使得 sleep 不会被唤醒，导致 Lost wakeup 事件发生。

解决 Lost wakeup 的方法是：**让 sleep 函数带有锁作为参数**。这样的话，线程会先释放这个锁，同时为了避免在还没sleep的时候wakeup就执行了，先获取对应进程的锁，然后再释放共享锁；在设置好进程的状态后，释放进程的锁，同时再获取共享变量的锁。

### 关闭一个进程

1. **exit系统调用**

exit 会释放进程的内存和page table，关闭已经打开的文件，父进程也会从wait系统调用中唤醒，所以exit最终会导致父进程被唤醒。由父进程调用的freeproc函数，来完成释放进程资源的最后几个步骤。

如果一个父进程要退出，需要设置它的子进程的父进程为 init 进程。

2. **kill系统调用**

kill 本身并没有做什么事情，主要是扫描进程表单，找到目标进程。然后只是将进程的proc结构体中killed标志位设置为1。如果进程正在SLEEPING状态，将其设置为RUNNABLE。

### Sleep Lock

磁盘块缓存时，使用的锁是不一样的，而是 Sleep Lock 这个结构。sleep lock是基于spinlock实现的，由于 spinlock 加锁时中断必须要关闭。而且磁盘读取数据需要很长时间。sleep lock的优势就是，**可以在持有锁的时候不关闭中断。**（这块还是有点懵，等以后再瞅瞅，。）

# 实验内容

## 任务一（Memory allocator）

第一个任务是解决内存块的竞争问题。目前的内存块的结构是利用一个大锁锁住一个 freelist 的方式。按照实验指导书所说，我们需要将 freelist 进行拆分，给每个 cpu 分配一个freelist，来降低竞争。

### 第一步，更改内存块结构

NCPU 在 kernel/param.h 中进行了定义。

```c
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];
```

### 第二步，修改 kinit
将所有空闲内存全部分配给当前 cpu 的 freelist。

```c
void
kinit()
{
  for (int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}
```

### 第三步，修改 kfree

获取 cpuid 的时候需要关闭中断。

```c
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int id = cpuid();
  pop_off();

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}
```

### 第四步，修改 kalloc

如果当前 cpu 有空闲内存块，就直接返回；没有的话，从其他 cpu 对应的 freelist 中“偷”一块。

```c
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();
  pop_off();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  else {
    for (int i = 0; i < NCPU; i++) {
      if (i == id) continue;
      acquire(&kmem[i].lock);
      r = kmem[i].freelist;
      if(r)
        kmem[i].freelist = r->next;
      release(&kmem[i].lock);
      if(r) break;
    }
  }
  release(&kmem[id].lock);
  
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
```

## 任务二（Buffer cache）

任务二的目的是解决 cache 缓存竞争问题。可以参考任务一，设计小粒度的锁。这里缓存区大小是固定的，不好随意更改。而且，由于题目要求使用 ticks 的方式代替现有的 LRU 机制，因此设计时复杂一些。

### 第一步，修改结构

也是同样，先修改数据结构，将 buf 分成 13 份（实验指导书上建议使用的质数），同时获取 trap.c 中的 ticks 变量。

这里不仅需要所有 bucket 的小锁，还需要一个大的锁防止死锁。不能像任务一那样直接使用 bcache 数组，这会改变 buf 的大小。

```c
extern uint ticks;

struct {
  struct spinlock biglock;
  struct spinlock lock[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKET];
} bcache;
```

为 kernel/buf.h 中添加 lastuse 字段，便于使用 LRU 机制：

```c
struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
  uint lastuse;
};
```

### 第二步，初始化

原本的 LRU 机制是使用双向链表来实现的，在这里由于使用了 ticks，因此可以不用这种方式实现，直接单向链表即可。但我后面写的时候，发现单向链表不方便删除 block，因此这里依然使用了双向的结构。

```c
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.biglock, "bcache_biglock");
  for (int i = 0; i < NBUCKET; i++)
    initlock(&bcache.lock[i], "bcache");

  // Create linked list of buffers
  //bcache.head.prev = &bcache.head;
  //bcache.head.next = &bcache.head;
  for (int i = 0; i < NBUCKET; i++) {
    bcache.head[i].next = &bcache.head[i];
    bcache.head[i].prev = &bcache.head[i];
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head[0].next;
    b->prev = &bcache.head[0];
    initsleeplock(&b->lock, "buffer");
    bcache.head[0].next->prev = b;
    bcache.head[0].next = b;
  }
}
```

### 第三步，修改 brels，bpin和bunpin

这些函数修改起来比较简单，直接加锁即可。brelse 中，由于不使用之前的方式实现 LRU，因此当遇到空闲块的时候，直接设置它的使用时间即可。

```c
int
hash(int blockno)
{
  return blockno % NBUCKET;
}
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  
  int i = hash(b->blockno);

  acquire(&bcache.lock[i]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    //b->next->prev = b->prev;
    //b->prev->next = b->next;
    //b->next = bcache.head[i].next;
    //b->prev = &bcache.head[i];
    //bcache.head[i].next->prev = b;
    //bcache.head[i].next = b;
    b->lastuse = ticks;
  }
  
  release(&bcache.lock[i]);
}

void
bpin(struct buf *b) {
  int i = hash(b->blockno);
  acquire(&bcache.lock[i]);
  b->refcnt++;
  release(&bcache.lock[i]);
}

void
bunpin(struct buf *b) {
  int i = hash(b->blockno);
  acquire(&bcache.lock[i]);
  b->refcnt--;
  release(&bcache.lock[i]);
}

```

### 第四步，bget

这是最关键、最核心的一步。这里的步骤：

1. 首先还是判断是否命中，如果已经缓存好，皆大欢喜，直接返回；
2. 如果没找到，释放锁，按顺序先获取大锁，再获取小锁，避免死锁；这时由于可能释放锁后，又可能会有缓存，因此再遍历一遍；
3. 如果还没命中，就去寻找当前 bucket 对应的 LRU 的空闲块，使用 ticks 的方式寻找，如果找到了，就返回；
4. 如果还没找到，需要向其他 bucket 中拿内存块。

代码如下：

```c
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b, *b2 = 0;
  
  int i = hash(blockno), min_ticks = 0;
  acquire(&bcache.lock[i]);

  // 1. Is the block already cached?
  for(b = bcache.head[i].next; b != &bcache.head[i]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[i]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[i]);

  // 2. Not cached.
  acquire(&bcache.biglock);
  acquire(&bcache.lock[i]);
  // 2.1 find from current bucket.
  for (b = bcache.head[i].next; b != &bcache.head[i]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.lock[i]);
      release(&bcache.biglock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // 2.2 find a LRU block from current bucket.
  for (b = bcache.head[i].next; b != &bcache.head[i]; b = b->next) {
    if (b->refcnt == 0 && (b2 == 0 || b->lastuse < min_ticks)) {
      min_ticks = b->lastuse;
      b2 = b;
    }
  }
  if (b2) {
    b2->dev = dev;
    b2->blockno = blockno;
    b2->refcnt++;
    b2->valid = 0;
    //acquiresleep(&b2->lock);
    release(&bcache.lock[i]);
    release(&bcache.biglock);
    acquiresleep(&b2->lock);
    return b2;
  }
  // 2.3 find block from the other buckets.
  for (int j = hash(i + 1); j != i; j = hash(j + 1)) {
    acquire(&bcache.lock[j]);
    for (b = bcache.head[j].next; b != &bcache.head[j]; b = b->next) {
      if (b->refcnt == 0 && (b2 == 0 || b->lastuse < min_ticks)) {
        min_ticks = b->lastuse;
        b2 = b;
      }
    }
    if(b2) {
      b2->dev = dev;
      b2->refcnt++;
      b2->valid = 0;
      b2->blockno = blockno;
      // remove block from its original bucket.
      b2->next->prev = b2->prev;
      b2->prev->next = b2->next;
      release(&bcache.lock[j]);
      // add block
      b2->next = bcache.head[i].next;
      b2->prev = &bcache.head[i];
      bcache.head[i].next->prev = b2;
      bcache.head[i].next = b2;
      release(&bcache.lock[i]);
      release(&bcache.biglock);
      acquiresleep(&b2->lock);
      return b2;
    }
    release(&bcache.lock[j]);
  }
  release(&bcache.lock[i]);
  release(&bcache.biglock);
  panic("bget: no buffers");
}
```

过程有点复杂，但知道思路后，写起来还挺快的。

# 总结

任务一思路很简单，做起来很快。

任务二，其实也可以简单地做，直接像任务一一样，如果未缓存，就从其他 bucket 中直接拿一个未使用的。但题目要求我们需要利用 LRU 机制，而且不使用原来的方式，而是用 ticks 的方式来替换，因此设计起来复杂了一些。

最后想吐槽的是，有一部分直接使用 `./make-grade-lock` 命令是不行的，，这里并没有清楚上一次的错误，导致我改了一晚上。。。一直出现 `panic:freeing free block` 错误。。。需要先 `make clean` 才行，。

感觉可以优化一下查找速度，改 bug 改得我很烦躁，不想优化了。。

# 参考文章
1. [MIT 6.S081 2020 LAB8记录](https://zhuanlan.zhihu.com/p/350624682)
2. [MIT6.S081学习总结-lab8:Lock](https://blog.csdn.net/laplacebh/article/details/118497015)
