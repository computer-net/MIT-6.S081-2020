# 介绍
这次实验是最后一个实验，主要是写一个网卡驱动，在已有的框架下添加一些代码，实验地址在[这里](https://pdos.csail.mit.edu/6.828/2020/labs/net.html)。本次实验是写的最不认真的一个了，也是写的最快的了，感谢网上的资源。个人感觉，本次实验内容应该放在讲键盘驱动那部分，当时看完了课程但不是很懂，没想到是最后的实验内容。

由于本次实验与 xv6 系统关系不大，且代码量很少，做的时候就直接参考了网上其他人的，在这里做个记录，以后想看的时候，用来给这门课程的实验结个尾。

# 实验内容

本次实验就是写两个函数，一个用于发送，另一个用于接收。几乎所有的外部设备都是类似于这种机制，产生中断，发送/接收信息，磁盘是很复杂的一种外设。不同设备的发送和接收接口是不同的，因此需要写相应设备的驱动才可以正常使用。

本次实验参考的是 e1000 的硬件手册（我懒得看了，。），根据手册内容书写发送和接收函数，主要是利用一个循环列表不断读取和写入。

e1000_transmit 函数：
```c
int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  acquire(&e1000_lock);
  uint index = regs[E1000_TDT];
  if((tx_ring[index].status & E1000_TXD_STAT_DD) == 0){
    release(&e1000_lock);
    return -1;
  }
  if(tx_mbufs[index])
    mbuffree(tx_mbufs[index]);
  tx_mbufs[index] = m;
  tx_ring[index].length = m->len;
  tx_ring[index].addr = (uint64)m->head;
  tx_ring[index].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
  regs[E1000_TDT] = (index + 1) % TX_RING_SIZE;
  release(&e1000_lock);
  return 0;
}
```
e1000_recv 函数：
```c
static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  uint index = regs[E1000_RDT];
  index = (index + 1) % RX_RING_SIZE;
  while(rx_ring[index].status & E1000_RXD_STAT_DD) {
    rx_mbufs[index]->len = rx_ring[index].length;
    net_rx(rx_mbufs[index]);
    if((rx_mbufs[index] = mbufalloc(0)) == 0)
      panic("e1000");
    rx_ring[index].addr = (uint64)rx_mbufs[index]->head;
    rx_ring[index].status = 0;
    index = (index + 1) % RX_RING_SIZE;
  }
  if(index == 0)
    index = RX_RING_SIZE;
  regs[E1000_RDT] = (index - 1) % RX_RING_SIZE;
}
```

# 总结
做实验的时间拖得有点久，还有好多其他事情要做，刚好要放假了，在这里算是收个尾。整个课程的实验真的是学到了不少东西，大赞！

# 参考文章

 1. [MIT 6.S081 lab 11：Networking](https://blog.csdn.net/weixin_43174477/article/details/119676414)
 2. [MIT 6.S081 2020 LAB11记录](https://zhuanlan.zhihu.com/p/351563871)
