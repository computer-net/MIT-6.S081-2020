# 介绍
## 写在前面
这次实验主要是写与 traps（中断陷阱）有关的代码，实验指导书在[这里](https://pdos.csail.mit.edu/6.828/2020/labs/traps.html)。总体来讲，代码量不是很大，大部分时间都思考应该怎么写、为什么这么写可以、函数是怎么运行的等问题。终于在思考了几天后完成了这次实验，实验代码在我的 [github](https://github.com/computer-net/MIT-6.S081-2020/tree/master/lab4-traps) 上。建议在做实验之前看看课程或者讲义，感觉这次的课很精髓，对做实验很有帮助。

## 前置知识

主要是 backtrace 和 alarm 部分的知识：

### 函数栈帧
在本科时候学计算机系统（CSAPP）时，x86 使用的函数**参数压栈**的方式来保存函数参数，xv6 使用**寄存器**的方式保存参数。

无论是x86还是xv6，函数调用时，都需要将返回地址和调用函数（父函数）的栈帧起始地址压入栈中。即被调用函数的栈帧中保存着这两个值。在 xv6 中，fp为当前函数的栈顶指针，sp为栈指针。fp-8 存放返回地址，fp-16 存放原栈帧（调用函数的fp）。

在 backtrace 任务中需要用到这些知识。

### 系统调用
再次回忆 lab2 的系统调用过程，在这里根据课程内容做一下补充：

- 首先，当用户调用系统调用的函数时，在进入函数前，会执行 user/usys.S 中相应的汇编指令，指令首先**将系统调用的函数码放到a7寄存器内，然后执行 ecall 指令进入内核态**。
- ecall 指令是 cpu 指令，该指令只做三件事情。
	
	- 首先将cpu的状态**由用户态（user mode）切换为内核态（supervisor mode）**；
	- 然后将**程序计数器的值保存在了SEPC寄存器**；
	- 最后**跳转到STVEC寄存器指向的指令**。

	ecall 指令并没有将 page table 切换为内核页表，也没有切换栈指针，需要进一步执行一些指令才能成功转为内核态。

- 这里需要对 trampoline 进行一下说明，STVEC寄存器中存储的是 trampoline page 的起始位置。**进入内核前，首先需要在该位置处执行一些初始化的操作。例如，切换页表、切换栈指针等操作**。需要注意的是，由于用户页表和内核页表都有 trampoline 的索引，且索引的位置是一样的，因此，即使在此刻切换了页表，cpu 也可以正常地找到这个地址，继续在这个位置执行指令。
- 接下来，cpu 从 trampoline page 处开始进行取指执行。接下来需要**保存所有寄存器的值**，以便在系统调用后恢复调用前的状态。为此，**xv6将进程的所有寄存器的值放到了进程的 trapframe 结构中**。
- 在 kernel/trap.c 中，需要 **检查触发trap的原因，以确定相应的处理方式**。产生中断的原因有很多，比如**系统调用、运算时除以0、使用了一个未被映射的虚拟地址、或者是设备中断等等**。这里是因为系统调用，所以以系统调用的方式进行处理。
- 接下来开始在内核态执行系统调用函数，**在 kernel/syscall.c 中取出 a7 寄存器中的函数码，根据该函数码，调用 kernel/sysproc.c 中对应的系统调用函数**。
- 最后，在系统调用函数执行完成后，将保存在 trapframe 中的 SEPC 寄存器的值取出来，从该地址存储的指令处开始执行（保存的值为ecall指令处的PC值加上4，即为 ecall 指令的下一条指令）。随后**执行 ret 恢复进入内核态之前的状态**，转为用户态。

以 write() 函数为例，系统调用的过程如下图所示：

![write() 函数系统调用过程](https://img-blog.csdnimg.cn/fa8870f7027c4d9183ba0b64936cf710.png#pic_center)
首先是 ecall 指令进入内核态；然后在 trampoline 处执行 uservec，完成初始化操作；随后执行 usertrap，判断中断类型，这里是系统调用中断；转到 syscall 中，根据 a7 寄存器中的值，调用对应的系统调用函数，即 sys_write 函数；最后使用 ret 指令进行返回，同时恢复寄存器的值，恢复到用户进行系统调用前的状态。

# 实验内容

## 任务一（RISC-V assembly）

首先，执行 `make fs.img` 指令，进行编译。然后查看生成的 user/call.asm 文件，其中的 main 函数如下：

![lab4-callasm](https://img-blog.csdnimg.cn/186f66cd03304ee38d89bb204c220d69.png#pic_center)

这部分没有需要写的代码，主要根据这个编译生成的代码，回答几个问题。

这里直接按照中文翻译了。

### 问题一
Q: 哪些寄存器存储了函数调用的参数？举个例子，main 调用 printf 的时候，13 被存在了哪个寄存器中？
A: A1. a0-a7, a2.

根据第 45 行代码，可以看到 13 被放到了 a2 寄存器中。猜测是 a0-a7 寄存器保存参数。

### 问题二

Q: main 中调用函数 f 对应的汇编代码在哪？对 g 的调用呢？ (提示：编译器有可能会内联(inline)一些函数)
A: nowhere, compiler optimization by inline function.

其实是没有这样的代码。 g(x) 被内联到 f(x) 中，然后 f(x) 又被进一步内联到 main() 中。所以看到的不是函数跳转，而是优化后的内联函数。

### 问题三

Q: printf 函数所在的地址是？
A: 0x0000000000000630 (ra=pc=0x30, 1536(ra)=0x0000000000000630).

其实，直接在 user/call.asm 代码中一直找，就能找到 printf 函数的地址。

也可以通过计算得到，首先将当前程序计数器的值赋给 ra 寄存器。`auipc ra, 0x0`，是指将当前立即数向右移动12位，然后加上 pc 寄存器的值，赋给 ra 寄存器，由于立即数为 0，因此 ra 的值即为 pc 的值。当前指令在0x30处，因此 pc = 0x30。1536(ra) 是指 1536 加上 ra 寄存器的值，1536 转为16进制再加上0x30 即为 0x0000000000000630。刚好是 printf 的地址。

### 问题四

Q: 在 main 中 jalr 跳转到 printf 之后，ra 的值是什么？
A: 0x38(ra=pc+4).

jalr 指令会将 pc + 4 赋给当前寄存器，刚好是其下一条指令的地址。

### 问题五

Q: 运行下面的代码

	unsigned int i = 0x00646c72;
	printf("H%x Wo%s", 57616, &i);      

输出是什么？
如果 RISC-V 是大端序的，要实现同样的效果，需要将 i 设置为什么？需要将 57616 修改为别的值吗？

A: He110 World, 0x726c6400, no change for 57616.

％x 表示以十六进制数形式输出整数，57616 的16进制表示就是 e110，与大小端序无关。
%s 是输出字符串，以整数 i 所在的开始地址，按照字符的格式读取字符，直到读取到 '\0' 为止。当是小端序表示的时候，内存中存放的数是：72 6c 64 00，刚好对应rld。当是大端序的时候，则反过来了，因此需要将 i 以16进制数的方式逆转一下。

### 问题六

Q: 在下面的代码中，'y=' 之后会答应什么？ (note: 答案不是一个具体的值) 为什么?

	printf("x=%d y=%d", 3);

A: print the value of a2 register.

printf 接收到了两个参数，但实际需要三个参数，最后一个参数是放在 a2 寄存器中，由于没有输入第三个参数，因此 a2 寄存器中目前有啥就输出啥。

## 任务二（Backtrace）
内核为每个进程分配了一段栈帧内存页，用于存放栈。函数调用就是在该位置处进行的。需要记得的是：**fp为当前函数的栈顶指针，sp为栈指针。fp-8 存放返回地址，fp-16 存放原栈帧**（调用函数的fp）。因此，我们可以通过当前函数的栈顶指针 fp 来找到调用该函数的函数栈帧，然后递归地进行下去。直到到达当前页的开始地址。

### 第一步，添加声明

首先，在 kernel/defs.h 中添加 backtrace 的声明：`void            backtrace(void);`

### 第二步，读取 fp
由于我们需要先获取到当前函数的栈帧 fp 的值，该值存放在 s0 寄存器中，因此需要写一个能够读取 s0 寄存器值得函数。按照实验指导书给的方法，在 kernel/riscv.h 添加读取 s0 寄存器的函数：

```c
// read the current frame pointer
static inline uint64
r_fp()
{
  uint64 x;
  asm volatile("mv %0, s0" : "=r"(x));
  return x;
}
```

### 第三步，实现backtrace

在 kernel/printf.c 中实现一个 backtrace() 函数：

```c
void backtrace(void) {
  uint64 fp = r_fp(), top = PGROUNDUP(fp);
  printf("backtrace:\n");
  for(; fp < top; fp = *((uint64*)(fp-16))) {
    printf("%p\n", *((uint64*)(fp-8)));
  }
}
```

迭代方法，不断循环，输出当前函数的返回地址，直到到达该页表起始地址为止。

### 第四步，添加函数调用

在 kernel/printf.c 文件中的 panic 函数里添加 backtrace 的函数调用；在 sys_sleep 代码中也添加同样的函数调用。

panic

```c
  printf("\n");
  backtrace();
  panicked = 1; // freeze uart output from other CPUs
```

sys_sleep
```c
  release(&tickslock);
  backtrace();
  return 0;
```

最后执行测试命令：`bttest`。（不在 panic 中添加也能通过。）

## 任务三（Alarm）

任务三纠结了两天，感觉还好。首先，需要明确一下该任务的目的。主要是实现一个定时调用的函数，每过一定数目的 cpu 的切片时间，调用一个用户函数，同时，在调用完成后，需要恢复到之前没调用时的状态。

这里需要注意的是：

- 在当前进程，已经有一个要调用的函数正在运行时，不能再运行第二个；
- 注意寄存器值的保存方式，在返回时需要保存寄存器的值；
- 系统调用的声明和书写方式。

### 第一步，声明系统调用
- 在 user/user.h 中添加声明：

```c
int sigalarm(int ticks, void (*handler)());
int sigreturn(void);
```
- 在 user/usys.pl 添加 entry，用于生成汇编代码：

```c
entry("sigalarm");
entry("sigreturn");
```
- 在 kernel/syscall.h 中添加函数调用码：

```c
#define SYS_sigalarm  22
#define SYS_sigreturn 23
```
- 在 kernel/syscall.c 添加函数调用代码：

```c
extern uint64 sys_sigalarm(void);
extern uint64 sys_sigreturn(void);
```

```c
[SYS_sigalarm]  sys_sigalarm,
[SYS_sigreturn] sys_sigreturn,
```

### 第二步，实现 test0

可以查看 alarmtest.c 的代码，能够发现 test0 只需要进入内核，并执行至少一次即可。不需要正确返回也可以通过测试。

- 首先，写一个 sys_sigreturn 的代码，直接返回 0即可（后面再添加）：

```c
uint64
sys_sigreturn(void)
{
  return 0;
}
```
- 然后，在 kernel/proc.h 中的 proc 结构体添加字段，用于记录时间间隔，经过的时钟数和调用的函数信息：

```c
  int interval;
  uint64 handler;
  int ticks;
```

- 编写 `sys_sigalarm()` 函数，给 proc 结构体赋值：

```c
uint64
sys_sigalarm(void)
{
  int interval;
  uint64 handler;
  struct proc * p;
  if(argint(0, &interval) < 0 || argaddr(1, &handler) < 0 || interval < 0) {
    return -1;
  }
  p = myproc();
  p->interval = interval;
  p->handler = handler;
  p->ticks = 0;
  return 0;
}
```

- 在进程初始化时，给初始化这些新添加的字段（kernel/proc.c 的 allocproc 函数）：

```c
  p->interval = 0;
  p->handler = 0;
  p->ticks = 0;
```

- 在进程结束后释放内存（freeproc 函数）：

```c
  p->interval = 0;
  p->handler = 0;
  p->ticks = 0;
```

- 最后一步，在时钟中断时，添加相应的处理代码：

```c
  if(which_dev == 2) {
    if(p->interval) {
	  if(p->ticks == p->interval) {
	    p->ticks = 0;  // 待会儿需要删掉这一行
		p->trapframe->epc = p->handler;
	  }
	  p->ticks++;
	}
    yield();
  }
```

到这里，test0 就可以顺利通过了。值得注意的是，现在还不能正确返回到调用前的状态，因此test1 和 test2 还不能正常通过。

这里为啥把要调用的函数直接赋给 epc 呢，原因是函数在返回时，调用 ret 指令，使用 trapframe 内事先保存的寄存器的值进行恢复。这里我们更改 epc 寄存器的值，在返回后，就直接调用的是 handler 处的指令，即执行 handler 函数。

handler 函数是用户态的代码，使用的是用户页表的虚拟地址，因此只是在内核态进行赋值，在返回到用户态后才进行执行，并没有在内核态执行handler代码。

### 第三步，实现 test1/test2
在这里需要实现正确返回到调用前的状态。由于在 ecall 之后的 trampoline 处已经将所有寄存器保存在 trapframe 中，为此，需要添加一个字段，用于保存 trapframe 调用前的所有寄存器的值。

什么不直接将返回的 epc 进行保存，再赋值呢？或者说为什么需要保存 trapframe 的值呢？为什么需要两个系统调用函数才能实现一个功能呢？之前的系统调用函数都是一个就完成了。

这是我一直思考的一些问题。首先需要了解系统调用的过程：

- 在调用 sys_sigalarm 前，已经把调用前所有的寄存器信息保存在了 trapframe 中。然后进入内核中执行 sys_sigalarm 函数。执行的过程中，只需要做一件事：**为 ticks 等字段进行赋值**。赋值完成后，该系统调用函数就完成了，trapframe 中的寄存器的值恢复，返回到了用户态。此时的 trapframe 没有保存的必要。
- 在用户态中，**当执行了一定数量的 cpu 时间中断后，我们将返回地址更改为 handler 函数，这样，在 ret 之后便开始执行 handler 函数**。在 cpu 中断时，也是进入的 trap.c 调用了相应的中断处理函数。
- 在执行好 handler 后，我们希望的是**回到用户调用 handler 前的状态**。但那时的状态已经被用来调用 handler 函数了，现在的 trapframe 中存放的是执行 sys_sigreturn 前的 trapframe，如果直接返回到用户态，则找不到之前的状态，无法实现我们的预期。
- 在 alarmtest 代码中可以看到，**每个 handler 函数最后都会调用 sigreturn 函数，用于恢复之前的状态**。由于每次使用 ecall 进入中断处理前，都会使用 trapframe 存储当时的寄存器信息，包括时钟中断。因此 trapframe 在每次中断前后都会产生变换，**如果要恢复状态，需要额外存储 handler 执行前的 trapframe（即更改返回值为 handler 前的 trapframe）**，这样，无论中间发生多少次时钟中断或是其他中断，保存的值都不会变。
- 因此，在 sigreturn 只需要使用存储的状态覆盖调用 sigreturn 时的 trapframe，就可以在 sigreturn 系统调用后恢复到调用 handler 之前的状态。再使用 ret 返回时，就可以返回到执行 handler 之前的用户代码部分。

所以，其实只需要增加一个字段，用于保存调用 handler 之前的 trapframe 即可：

- 在 kernel/proc.h 中添加一个指向 trapframe 结构体的指针：

```c
struct trapframe *pretrapframe;
```
- 在进程初始化时，为该指针进行赋值：

```c
  p->interval = 0;
  p->handler = 0;
  p->ticks = 0;
  if((p->pretrapframe = (struct trapframe *)kalloc()) == 0){
    release(&p->lock);
	return 0;
  }
```
- 进程结束后，释放该指针：

```c
  p->interval = 0;
  p->handler = 0;
  p->ticks = 0;
  if(p->pretrapframe)
    kfree((void*)p->pretrapframe);
```

- 在每次时钟中断处理时，判断是否调用 handler，如果调用了，就存储当前的 trapframe，用于调用之后的恢复：

```c
  if(which_dev == 2) {
    if(p->interval) {
	  if(p->ticks == p->interval) {
	    //p->ticks = 0;
	    //memmove(p->pretrapframe, p->trapframe, sizeof(struct trapframe));
	    *p->pretrapframe = *p->trapframe;
		p->trapframe->epc = p->handler;
	  }// else {
	    p->ticks++;
	  //}
	}
    yield();
  }
```
- 最后，实现 sys_sigreturn 恢复执行 handler 之前的状态：

```c
uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  *p->trapframe = *p->pretrapframe;
  //memmove(p->trapframe, p->pretrapframe, sizeof(struct trapframe));
  p->ticks = 0;
  return 0;
}
```

到这里就都完成了。需要注意的是，如果有一个handler函数正在执行，就不能让第二个handler函数继续执行。为此，可以再次添加一个字段，用于标记是否有 handler 在执行。我第一次通过的时候就增加了一个字段，但想来想去，感觉有点多余。

其实可以直接在 sigreturn 中设置 ticks 为 0，而取消 trap.c 中的 ticks 置 0 操作。这样，即使第一个 handler 还没执行完，由于 ticks 一直是递增的，第二个 handler 始终无法执行。只有当 sigreturn 执行完成后，ticks 才置为 0，这样就可以等待下一个 handler 执行了。

# 总结

感觉写的代码量比较少，主要是理解 trap 中断陷阱这个概念和流程。我也是想了很久，本来应该早点写完的，但由于一些事情耽误了一下，最后总算还是搞定了。实验很酷！

文章同步在[知乎]()

# 参考文献

 1. [[MIT 6.S081] Lab 4: traps](https://blog.csdn.net/lostunravel/article/details/121341055)
 2. [[mit6.s081] 笔记 Lab4: Traps | 中断陷阱](https://juejin.cn/post/7010653349024366606)
 3. [MIT 6.S081 2020 LAB4记录](https://zhuanlan.zhihu.com/p/347945926)

