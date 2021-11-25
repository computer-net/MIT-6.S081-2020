# 介绍
## 写在前面

今天做完了 6.S081 [Lab3: page tables](https://pdos.csail.mit.edu/6.828/2020/labs/pgtbl.html)，实验三的主要任务是增加一个页表。本来计划上周就做完，但自己的基础知识实在匮乏，又先去补充了一下虚拟内存方面的知识，把李志军老师的操作系统慕课看了一些，周末才开始搞。中间遇到了一些bug，参考了一下大佬们的实现，磕磕绊绊算是做完了\~，代码在 [github](https://github.com/computer-net/MIT-6.S081-2020/tree/master/lab3-pgtbl) 上。

感觉这个实验跟前两个不是一个级别\~

## 前置知识

做实验之前首先需要知道一些关于虚拟内存的基本知识。

### 隔离性与地址空间

由于操作系统需要具有**隔离性**，即能够保证所有进程之间互不影响。为了实现隔离性，操作系统为每个进程都分配了一段内存，且进程只能访问属于自己的这段内存，这样就可以保证每个进程之间相互隔离。如果一个进程崩溃了，其他进程还可以正常运行。地址空间是一种实现内存隔离的方式。

### 地址重定向与虚拟地址

操作系统在读取可执行文件时，并不是直接将整个文件读到一块内存中，而是使用分段的方式，例如代码段、数据段等，将不同的段放到内存中不同的位置。在这个过程中，进程将每个段的段基地址进行存储。

取地址时，由于 cpu 是按照 “取出指令并进行执行” 的方式进行的，取出的地址是**逻辑地址**，即相对于段基地址的相对地址。段基地址（CS寄存器）加上偏移地址（IP寄存器）即为数据在内存中的地址。但这个地址也并不是真实的物理内存的地址，而是**虚拟地址**，需要通过进一步转换才能得到物理地址。

### 页与页表

但由于在进程中的内存是连续的，但对应到物理内存不一定是连续的，一般而言，哪里有空闲的地方就将到那个位置上。这样就引入了**内存页**的概念。内存的大小并不是需要多少就分配多大的，由于进程需要的内存大小没有范围，有的大有的小，因此，如果安装进程所需的内存大小直接分配物理内存很容易出现大量的内存碎片。为此，将物理内存的大小按照内存页进行划分，通常一个内存页为4096大小，12位比特。这样的话，操作系统就会按照进程所需的内存大小，分配给进程相应数量的内存页，可以有效的利用内存，减少内存的浪费。

上述通过计算得到虚拟地址还需要进一步映射到真实的物理地址上，这样的话就需要一个地址映射表，即**页表**。为了方便进行索引，页表的方式应该是线性的（线性页表的查找时间复杂度为O（1））。也就是说，即使没有对应的物理内存，页表项也应该存在。RISCV中的页表索引为 27 比特，也就是说，我们需要为每个进程维护一个 $2^{27}$ 大小的页表，这显然是不现实的。

为此，引入了**多级页表**，将27比特的索引划分为三级子页表，每个级别为 9 比特，代表页表的偏移量。这样的话，就有三级页表，每张表有 512 个表项。如果对应的子表不存在，则不需要创建这个子表，因此节省了大量的内存空间。多级页表的索引方式如下图所示：

![多级页表](https://img-blog.csdnimg.cn/b40f204069f54292862b9ef907ff28bb.png#pic_center)

由于每个页表有512个页表项，每个页表项有64位大小，其中前10位为保留位，中间44位为存储下一个页表的地址，后10位是页表的标记位。标记位的作用如下图所示：

![页表项标记位](https://img-blog.csdnimg.cn/01695ba69d0a45c893cfdc5cdaa0e542.png#pic_center)

虽然有10个标记位，但这里能够用到的标记位只有后四位\~

### 内核态页表与用户态页表

进程在进行系统调用后，状态转为内核态，相应的，页表也会切换为**内核态页表**。每个进程都有属于自己的用户态页表，但内核态页表是所有进程共用一个的。

操作系统的启动会从 0x80000000 这个地址开始。物理地址低于这个地址部分存放的是一些其他设备，大于这个地址会走向DRAM芯片。这些其他I/O设备的地址由内核态页表使用直接映射的方式进行存储，内核态页表的虚拟地址与物理地址相同。仅有两个映射例外，一个是内核栈，另一个是 `trampoline` 页，这两部分的映射并不是直接映射。

# 实验内容

实验任务主要有三个，第一个最简单，第二个最难，。

## 任务一（Print a page table）
任务一是打印页表。如果了解了虚拟内存和页表的知识后，就应该知道这里大概是怎么写了。

建议先看一下 `kernel/riscv.h` 文件里对页表的一些宏定义。

直接在 `kernel/vm.c` 中添加递归打印的代码：

```c
void _vmprint(pagetable_t pagetable, int level) {
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
	if (pte & PTE_V) {
      uint64 pa = PTE2PA(pte);
      for (int j = 0; j < level; j++) {
		if (j) printf(" ");
		printf("..");
	  }
	  printf("%d: pte %p pa %p\n", i, pte, pa);
	  if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
	    _vmprint((pagetable_t)pa, level+1);
	  }
	}
  }
}

void vmprint(pagetable_t pagetable) {
  printf("page table %p\n", pagetable);
  _vmprint(pagetable, 1);
}
```

还要在 `kernel/defs.h` 文件中添加函数定义：`void            vmprint(pagetable_t);` 并在 `exec.c` 中进行调用：`if (p->pid == 1) {
    vmprint(p->pagetable);
  }`

最后使用 `./grad-lab-pgtbl pte print` 命令进行测试。

这部分可以反复看几遍，主要是了解页表是如何进行映射的以及页表的标记位如何使用，虽然任务简单，但对理解虚拟内存和多级页表很有帮助。

## 任务二（A kernel page table per process）

任务二主要是理解内核态页表。由于进程在进入内核态后，页表会自动切换成内核态的页表，如果在内核态接收到进程的虚拟地址，还需要先在用户态将地址转为内核态能识别的物理地址。例如，调用 write() 函数写数据。这样操作很麻烦，可不可以在内核态直接翻译进程的虚拟地址呢。

为此，这个任务的目的是，为每个进程新增一个内核态的页表，然后在该进程进入到内核态时，不使用公用的内核态页表，而是使用进程的内核态页表，这样就可以实现在内核态直接使用虚拟地址的功能了。

### 第一步，添加 kernel pagetable
在 `kernel/proc.h` 中的 `proc` 结构体中添加一个字段 `pagetable_t kpagetable;`，表示内核态页表。

### 第二步，初始化内核态页表

这部分主要参考 kernel/vm.c 里 `kvminit` 函数。由于其中的 `kvmmap` 函数是为 `kernel_pagetable` 添加映射的，因此我们仿照它重新写一个映射函数：

```c
void ukvmmap(pagetable_t kpagetable, uint64 va, uint64 pa, uint64 sz, int perm) {
  if(mappages(kpagetable, va, sz, pa, perm) != 0)
    panic("uvmmap");
}
```

然后按照 `kvminit` 函数的方式进行初始化。

```c
pagetable_t ukvminit() {
  pagetable_t kpagetable = (pagetable_t) kalloc();
  memset(kpagetable, 0, PGSIZE);
  ukvmmap(kpagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  ukvmmap(kpagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  ukvmmap(kpagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
  ukvmmap(kpagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
  ukvmmap(kpagetable, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);
  ukvmmap(kpagetable, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);
  ukvmmap(kpagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  return kpagetable;
}
```

在 `kernel/proc.c` 中的 `allocproc` 函数里添加调用函数的代码：

```c
// An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
// 下面是新添加的
// An empty user kernel page table.
  p->kpagetable = ukvminit();
  if(p->kpagetable == 0) {
    freeproc(p);
	release(&p->lock);
	return 0;
  }
```

记得在 `kernel/defs.h` 添加函数声明：`pagetable_t     ukvminit(void);`

### 第三步，初始化内核栈

内核栈的初始化原来是在 `kernel/proc.c` 中的 `procinit` 函数内，这部分要求将函数内的代码转移到 `allocproc` 函数内，因此在上一步初始化内核态页表的代码下面接着添加初始化内核栈的代码：

```c
// An empty user kernel page table.
  p->kpagetable = ukvminit();
  if(p->kpagetable == 0) {
    freeproc(p);
	release(&p->lock);
	return 0;
  }
// 初始化内核栈
  char *pa = kalloc();
  if(pa == 0)
    panic("kalloc");
  uint64 va = KSTACK((int)(p - proc));
  ukvmmap(p->kpagetable, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  p->kstack = va;
```

### 第四步，进程调度时，切换内核页

内核页的管理使用的是 SATP 寄存器，在 `kernel/proc.c` 的调度函数 `scheduler` 中添加切换 SATP 寄存器的代码，并在调度后切换回来：

```c
// change satp
w_satp(MAKE_SATP(p->kpagetable));
sfence_vma();

// change process
swtch(&c->context, &p->context);

// change back
kvminithart();
```

注：貌似也可以在 `#if !defined (LAB_FS)` 内添加切换回的代码，感觉也很有道理。

### 第五步，释放内核栈内存

释放页表的第一步是先释放页表内的内核栈，因为页表内存储的内核栈地址本身就是一个虚拟地址，需要先将这个地址指向的物理地址进行释放：

```c
// delete kstack
  if(p->kstack) {
    pte_t* pte = walk(p->kpagetable, p->kstack, 0);
	if(pte == 0)
      panic("freeproc: walk");
	kfree((void*)PTE2PA(*pte));
  }
  p->kstack = 0;
```

这里的地址释放，需要手动释放内核栈的物理地址。因此需要单独对内核栈进行处理。

需要注意的是，这里需要将 `walk` 函数的定义添加到 `kernel/defs.h` 中，否则无法直接引用。

### 第六步，释放内核页表

然后是释放页表，直接遍历所有的页表，释放所有有效的页表项即可。仿照 `freewalk` 函数。由于 `freewalk` 函数将对应的物理地址也直接释放了，我们这里释放的内核页表仅仅只是用户进程的一个备份，释放时仅释放页表的映射关系即可，不能将真实的物理地址也释放了。因此不能直接调用`freewalk` 函数，而是需要进行更改：

```c
void proc_freewalk(pagetable_t pagetable) {
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
	if (pte & PTE_V) {
	  pagetable[i] = 0;
	  if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
	    uint64 child = PTE2PA(pte);
		proc_freewalk((pagetable_t)child);
	  }
	}
  }
  kfree((void*)pagetable);
}
```

再在 `freeproc` 中进行调用：

```c
  // delete kernel pagetable
  if(p->kpagetable) {
    proc_freewalk(p->kpagetable);
  }
  p->kpagetable = 0;
```

### 第七步，切换进程内核页表

最后，在 vm.c 中添加头文件：

```c
#include "spinlock.h"
#include "proc.h"
```

然后更改 `kvmpa` 函数：

```c
pte = walk(myproc()->kpagetable, va, 0);
```

## 任务三（Simplify copyin/copyinstr）

任务三是衔接任务二，主要目的是将用户进程页表的所有内容都复制到内核页表中，这样的话，就完成了内核态直接转换虚拟地址的方法。

### 第一步，复制页表内容

首先写一个复制页表项的方法（需要注意的是，这里的 FLAG 标记位，PTE_U需要设置为0，以便内核访问。）：

```c
void u2kvmcopy(pagetable_t upagetable, pagetable_t kpagetable, uint64 oldsz, uint64 newsz) {
  oldsz = PGROUNDUP(oldsz);
  for (uint64 i = oldsz; i < newsz; i += PGSIZE) {
    pte_t* pte_from = walk(upagetable, i, 0);
	pte_t* pte_to = walk(kpagetable, i, 1);
	if(pte_from == 0) panic("u2kvmcopy: src pte do not exist");
	if(pte_to == 0) panic("u2kvmcopy: dest pte walk fail");
	uint64 pa = PTE2PA(*pte_from);
	uint flag = (PTE_FLAGS(*pte_from)) & (~PTE_U);
	*pte_to = PA2PTE(pa) | flag;
  }
}
```

同时在 `kernel/defs.h` 中进行声明：`void            u2kvmcopy(pagetable_t, pagetable_t, uint64, uint64);`

### 第二步，fork()，sbrk()，exec()

分别找 fork()，sbrk()，exec() 函数，在其中添加代码。

fork()
```c
np->cwd = idup(p->cwd);
  // 添加代码
  u2kvmcopy(np->pagetable, np->kpagetable, 0, np->sz);

safestrcpy(np->name, p->name, sizeof(p->name));
```

sbrk() 需要到 `sysproc.c` 找，可以发现，调用的是 `growproc` 函数，在其中添加防止溢出的函数：

```c
if(PGROUNDUP(sz + n) >= PLIC) return -1;
```

exec() 在 `kernel/exec.c` 中，在执行新的程序前，初始化之后，进行页表拷贝：
```c
u2kvmcopy(pagetable, p->kpagetable, 0, sz);
// Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
  ......
```

userinit() 函数内也需要调用复制页表的函数。

### 第三步，调用 copyin_new 和 copyinstr_new

将copyin和copyinstr函数内部全部注释掉，改为调用copyin_new和copyinstr_new函数即可。

最后在系统内部运行 `usertests` 命令，如果全部通过，则完成。

使用 `make grade` 命令查看得分。

# 总结
做完实验感觉是真的很难。做完第一个任务感觉对页表理解又深刻了一些；做完第二个感觉就是很神奇，做的过程中不断地询问自己这个任务的目的是啥，总是觉得已经有一个内核页表为啥还要再搞一个；第三个任务稀里糊涂地就完事了，主要是在这几个函数内部调用复制页表的函数。可以发现，这几个函数都是进程刚初始化或是改变的时候。感觉还有一些东西不是很懂，大佬们的实现也是各有不同，做完就是感觉很神奇。

# 参考文献
1. [【MIT-6.S081-2020】Lab3 Pgbtl](https://zhuanlan.zhihu.com/p/280914560)
2. [MIT 6.S081 2020 LAB3记录](https://zhuanlan.zhihu.com/p/347172409)
3. [MIT 6.S081 Lecture Notes](https://fanxiao.tech/posts/MIT-6S081-notes/)

