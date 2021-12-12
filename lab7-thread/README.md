# 写在前面
这个实验是多线程安全和同步的内容，实验地址在[这里](https://pdos.csail.mit.edu/6.828/2020/labs/thread.html)。说实话，有点简单，用了一天时间就写完了，感觉最近的实验，从实验三之后难度就降低了很多。但是实验的内容还是很有用的，主要学习了一遍线程的切换过程，感觉受益很多，建议看一下课程里对应的多线程部分。我的实验代码在 [github](https://github.com/computer-net/MIT-6.S081-2020/tree/master/lab7-thread) 上，感觉这个实验没太多代码和步骤需要分析，主要是基础知识，这里就随便写点我觉得重要的了。

## 线程调度
线程调度的过程大概是以下几个步骤：

- 首先是用户线程接收到了时钟中断，**强迫CPU从用户空间进程切换到内核**，同时在 trampoline 代码中，保存当前寄存器状态到 trapframe 中；
- **在 usertrap 处理中断时**，切换到了该进程对应的内核线程；
- 内核线程在内核中，先做一些操作，**然后调用 swtch 函数，保存用户进程对应的内核线程的寄存器至 context 对象**；
- **swtch 函数并不是直接从一个内核线程切换到另一个内核线程；而是先切换到当前 cpu 对应的调度器线程**，之后就在调度器线程的 context 下执行 schedulder 函数中；
- **schedulder 函数会再次调用 swtch 函数，切换到下一个内核线程中，由于该内核线程肯定也调用了 swtch 函数，所以之前的 swtch 函数会被恢复**，并返回到内核线程所对应进程的系统调用或者中断处理程序中。
- 当内核程序执行完成之后，**trapframe 中的用户寄存器会被恢复，完成线程调度**。

线程调度过程如下图所示：

![xv6线程调度过程](https://img-blog.csdnimg.cn/1c4e1047b3c64130b46e5632a3c7064f.png#pic_center)


**线程调度的过程主要是保存 contex 上下文状态，因为这里的切换全都是以函数调用的形式，因此这里只需要保存被调用者保存的寄存器（Callee-saved register）即可，调用者的寄存器会自动保存。**

## 进程、线程和协程

> **进程是操作系统资源分配的基本单位，而线程是处理器任务调度和执行的基本单位**

在 xv6 中，一个进程只有一个线程，因此本实验中区分不大。

第一个实验的内容很像协程的概念，即**用户线程切换时，不进入内核态，而是直接在用户线程上，让用户线程自己主动出让 cpu，而不是接受时钟中断**。

更具体的区别可以查一查其他人写的博客。

# 实验内容

感觉整个实验，最核心的还是任务一，后面两个实验与 xv6 没啥关系。

## 任务一（Uthread）

实现一个用户线程调度的方法。

> 这里的“线程”是完全用户态实现的，多个线程也只能运行在一个 CPU 上，并且没有时钟中断来强制执行调度，需要线程函数本身在合适的时候主动 yield 释放 CPU。

### 第一步，上下文切换

首先是 uthread_switch.S 中实现上下文切换，这里可以直接参考（复制） swtch.S：

```c
	.text

	/*
         * save the old thread's registers,
         * restore the new thread's registers.
         */

	.globl thread_switch
thread_switch:
	/* YOUR CODE HERE */sd ra, 0(a0)
    sd sp, 8(a0)
    sd s0, 16(a0)
    sd s1, 24(a0)
    sd s2, 32(a0)
    sd s3, 40(a0)
    sd s4, 48(a0)
    sd s5, 56(a0)
    sd s6, 64(a0)
    sd s7, 72(a0)
    sd s8, 80(a0)
    sd s9, 88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    ld ra, 0(a1)
    ld sp, 8(a1)
    ld s0, 16(a1)
    ld s1, 24(a1)
    ld s2, 32(a1)
    ld s3, 40(a1)
    ld s4, 48(a1)
    ld s5, 56(a1)
    ld s6, 64(a1)
    ld s7, 72(a1)
    ld s8, 80(a1)
    ld s9, 88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)

	ret    /* return to ra */
```

先将一些寄存器保存到第一个参数中，然后再将第二个参数中的寄存器加载进来。

### 第二步，定义上下文字段
从 proc.h 中复制一下 context 结构体内容，用于保存 ra、sp 以及 callee-saved registers：

```c
// Saved registers for user context switches.
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};
```

在线程的结构体中进行声明：

```c
struct thread {
  char       stack[STACK_SIZE]; /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
  struct context contex;           /* the context of thread */
};
```

### 第三步，调度 thread_schedule

在 thread_schedule 中调用 thread_switch：

```c
void 
thread_schedule(void)
{
  // ...................
  if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    /* YOUR CODE HERE
     * Invoke thread_switch to switch from t to next_thread:
     * thread_switch(??, ??);
     */
    thread_switch((uint64)&t->contex, (uint64)&current_thread->contex);
  } else
    next_thread = 0;
}
```

### 第四步，创建并初始化线程

```c
void 
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  // YOUR CODE HERE
  t->contex.ra = (uint64)func;
  t->contex.sp = (uint64)&t->stack + (STACK_SIZE - 1);
}
```

线程栈是从高位到低位，因此初始化时栈指针 sp 应该指向数组底部（好像直接加 `STACK_SIZE` 也行？？）。

返回地址 ra 直接指向该函数的地址就行，这样开始调度时，直接执行该函数（线程）。

## 任务二（Using threads）

利用加锁操作，解决哈希表 race-condition 导致的数据丢失问题。

主要是，在加大锁还是小锁的问题。如果只加一个锁，锁的粒度很大，会导致丢失性能，结果还不如不加锁的单线程。因此需要将锁的粒度减小，为每个槽位（bucket）加一个锁。

```c
pthread_mutex_t lock[NBUCKET];
static 
void put(int key, int value)
{
  int i = key % NBUCKET;

  pthread_mutex_lock(&lock[i]);
  // is the key already present?
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    insert(key, value, &table[i], table[i]);
  }
  pthread_mutex_unlock(&lock[i]);
}

static struct entry*
get(int key)
{
  int i = key % NBUCKET;

  pthread_mutex_lock(&lock[i]);
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key) break;
  }
  pthread_mutex_unlock(&lock[i]);
  return e;
}
```

在 main 函数中初始化锁：

```c
  for(int i = 0; i < NBUCKET; i++) {
    pthread_mutex_init(&lock[i], NULL);
  }
```

## 任务三（Barrier）

这个做着有点懵，莫名其妙的做完了！？

主要是条件变量：

> 条件变量是利用线程间共享的全局变量进行同步的一种机制，主要包括两个动作：一个线程等待"条件变量的条件成立"而挂起；另一个线程使"条件成立"（给出条件成立信号）。为了防止竞争，条件变量的使用总是和一个互斥锁结合在一起。


这里是生产者消费者模式，如果还有线程没到达，就加入到队列中，等待唤起；如果最后一个线程到达了，就将轮数加一，然后唤醒所有等待这个条件变量的线程。


```c
static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
  pthread_mutex_lock(&bstate.barrier_mutex);
  if(++bstate.nthread == nthread) {
    bstate.nthread = 0;
    bstate.round++;
    pthread_cond_broadcast(&bstate.barrier_cond);
  } else {
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);
}
```

# 总结
线程调度很牛，感觉刚好写了一下精髓部分。最后的条件变量现在还不是很懂，。。

# 参考文章
1. [MIT 6.S081 lab 7：Multithreading](https://blog.csdn.net/weixin_43174477/article/details/119431562)
2. [[mit6.s081] 笔记 Lab7: Multithreading | 多线程](https://juejin.cn/post/7016228101717753886)
