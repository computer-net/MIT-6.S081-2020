# 介绍
## 写在前面

这个实验是文件系统，内容是**扩充 xv6 的文件系统大小和实现软链接**，实验地址在[这里](https://pdos.csail.mit.edu/6.828/2020/labs/fs.html)。感觉到这里是操作系统的另一个视图了，跟前面的虚拟内存和进程不太一样，相比来说，操作系统这部分的知识简单一些。为此我去补充了一下相关的知识，再次感觉这门课真好，。能力太差了，写代码花了好长时间，。

## 文件系统

xv6 的文件系统和许多其他系统的实现大体是相同的，只不过很多地方简化了很多。存储文件的方式都是以 block 的形式。物理磁盘在读写时，是以扇区为单位的，通常来讲，**每个扇区是 512 个字节**。但操作系统读取磁盘时，由于寻道的时间是很长的，读写数据的时间反而没那么久，因此会操作系统一般会读写连续的多个扇区，所使用的时间几乎一样。操作系统以多个扇区作为一个磁盘块，**xv6是两个扇区，即一个 block 为 1024 个字节。**

磁盘只是以扇区的形式存储数据，但如果没有一个读取的标准，该磁盘就是一个生磁盘。磁盘需要按照操作系统读写的标准来存储数据，格式如下：

![xv6磁盘组织方式](https://img-blog.csdnimg.cn/78b23cab71bc4b1b8cf70a3655f801cf.png#pic_center)
从中可以看到，磁盘中不同区域的数据块有不同的功能。**第 0 块数据块是启动区域，计算机启动就是从这里开始的；第 1 块数据是超级块，存储了整个磁盘的信息；然后是 log 区域，用于故障恢复；bit map 用于标记磁盘块是否使用；然后是 inode 区域 和 data 区域。**

磁盘中主要存储文件的 block 是 inode 和 data。操作系统中，文件的信息是存放在 inode 中的，每个文件对应了一个 inode，**inode 中含有存放文件内容的磁盘块的索引信息**，用户可以通过这些信息来查找到文件存放在磁盘的哪些块中。inodes 块中存储了很多文件的 inode。

# 实验内容

## 任务一（Large files）

xv6中的 inode 有 12个直接索引（直接对应了 data 区域的磁盘块），1个一级索引（存放另一个指向 data 区域的索引）。因此，**最多支持 12 + 256 = 268 个数据块**。如下图所示：

![xv6的inode](https://img-blog.csdnimg.cn/546a1319cd334e558f84456e92886a22.png#pic_center)


因为这个设计，xv6 中存储文件的大小受到了限制，因此本实验的第一个任务就是**通过实现二级索引扩大所支持的文件大小**。

### 第一步，修改 inode 数据结构
 kernel/fs.h 文件中减小 NDIRECT 的值，为二级索引留一个位置：

```c
#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDINDIRECT NINDIRECT * NINDIRECT
#define MAXFILE (NDIRECT + NINDIRECT + NDINDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+2];   // Data block addresses
};
```

上面的是磁盘中的 inode 结构，还需要在 kernel/file.h 中更改内存中的 inode 结构：

```c
// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+2];
};
```

### 第二步，实现 bmap 映射

仿照一级索引，写一下二级索引，在 kernel/fs.c 中添加代码：

```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  // 二级索引
  bn -= NINDIRECT;
  if(bn < NDINDIRECT) {
    if((addr = ip->addrs[NDIRECT+1]) == 0)
      ip->addrs[NDIRECT+1] = addr = balloc(ip->dev);
    // 通过一级索引，找到下一级索引
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn/NINDIRECT]) == 0) {
      a[bn/NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    // 重复上面的代码，实现二级索引
	bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if ((addr = a[bn%NINDIRECT]) == 0) {
      a[bn%NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}
```

### 第三步，itrunc 清理

在 kernel/fs.c 中，添加第二级索引的释放操作：

```c
void
itrunc(struct inode *ip)
{
  ...

  if(ip->addrs[NDIRECT+1]) {
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++) {
      if(a[j]) {
        bp2 = bread(ip->dev, a[j]);
        a2 = (uint*)bp2->data;
        for(i = 0; i < NINDIRECT; i++) {
          if(a2[i]) bfree(ip->dev, a2[i]);
        }
        brelse(bp2);
        bfree(ip->dev, a[j]);
        a[j] = 0;
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+1]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```

## 任务二（Symbolic links）

硬链接是指多个文件名指向同一个inode号码。有以下特点：

- **可以用不同的文件名访问同样的内容；**
- **对文件内容进行修改，会影响到所有文件名；**
- **删除一个文件名，不影响另一个文件名的访问。**

而软链接也是一个文件，但是**文件内容指向另一个文件的 inode**。打开这个文件时，会自动打开它指向的文件，类似于 windows 系统的快捷方式。

xv6 中没有符号链接（软链接），这个任务需要我们实现一个符号链接。

### 第一步，增加 symlink 系统调用

- user/usys.pl

```c
entry("symlink");
```

- user/user.h

```c
int symlink(const char*, const char*);
```

- kernel/syscall.h

```c
#define SYS_symlink 22
```

- kernel/syscall.c

```c
extern uint64 sys_symlink(void);

[SYS_symlink] sys_symlink,
```

- knerl/sysfile.c

最后是实现系统调用函数，先从寄存器中读取参数，然后开启事务，避免提交出错；为这个符号链接新建一个 inode；在符号链接的 data 中写入被链接的文件；最后，提交事务：

```c
uint64
sys_symlink(void)
{  
  char path[MAXPATH], target[MAXPATH];
  struct inode *ip;
  // 读取参数
  if(argstr(0, target, MAXPATH) < 0)
    return -1;
  if(argstr(1, path, MAXPATH) < 0)
    return -1;
  // 开启事务
  begin_op();
  // 为这个符号链接新建一个 inode
  if((ip = create(path, T_SYMLINK, 0, 0)) == 0) {
    end_op();
    return -1;
  }
  // 在符号链接的 data 中写入被链接的文件
  if(writei(ip, 0, (uint64)target, 0, MAXPATH) < MAXPATH) {
    iunlockput(ip);
    end_op();
    return -1;
  }
  // 提交事务
  iunlockput(ip);
  end_op();
  return 0;
}
```

### 第二步，增加标志位

按照实验手册，增加标志位：

- kernel/stat.h中添加`T_SIMLINK`
- kernel/fcntl.h中添加`O_NOFOLLOW`

### 第三步，修改 sys_open 函数

在打开文件时，如果遇到符号链接，直接打开对应的文件。这里为了避免符号链接彼此之间互相链接，导致死循环，设置了一个访问深度（我设成了 20），如果到达该访问次数，则说明打开文件失败。每次先读取对应的 inode，根据其中的文件名称找到对应的 inode，然后继续判断该 inode 是否为符号链接：

```c
int depth = 0;
...
 // 不断判断该 inode 是否为符号链接
 while(ip->type == T_SYMLINK && !(omode & O_NOFOLLOW)) {
    // 如果访问深度过大，则退出
    if (depth++ >= 20) {
      iunlockput(ip);
      end_op();
      return -1;
    }
    // 读取对应的 inode
    if(readi(ip, 0, (uint64)path, 0, MAXPATH) < MAXPATH) {
      iunlockput(ip);
      end_op();
      return -1;
    }
    iunlockput(ip);
    // 根据文件名称找到对应的 inode
    if((ip = namei(path)) == 0) {
      end_op();
      return -1;
    }
    ilock(ip);
  }
  ...
```

# 总结

做完感觉实验难度还行，，感觉比前几个难一些，主要是文件系统方面的知识储备不太够。这个实验对应的课程里没有讲太多有关实验内容的事，主要还是多看看其他文件系统函数是如何实现的（sys_open 函数帮助很大，还有 namei, readi, writei 等跟 inode 有关的函数）。

# 参考文章

 1. [MIT 6.s081 xv6-lab9-fs](https://zhuanlan.zhihu.com/p/430816131)
 2. [MIT 6.S081 2020 LAB9记录](https://zhuanlan.zhihu.com/p/351048431)
 3. [阮一峰 理解inode](https://www.ruanyifeng.com/blog/2011/12/inode.html)

