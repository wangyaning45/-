# 学习记录
## 聊天室程序
使用I/O复用技术------poll和epoll为例实现一个简单的聊天室程序，以阐述如何使用I/O复用技术来同时处理网络连接和用户输入。
该聊天室程序能让所有用户同时在线群聊，它分为客户端和服务器两个部分。
其中客户端程序有两个功能：

一是从标准输入终端读入用户数据，并将用户数据发送至服务器；

二是往标准输出终端打印服务器发送给它的数据。

服务器的功能是接收客户数据，并把客户数据发送给每一个登录到该服务器上的客户端（数据发送者除外）。

## 客户端
客户端程序使用poll同时监听用户输入和网络连接，并利用splice
函数将用户输入内容直接定向到网络连接上以发送之，从而实现数据
零拷贝，提高了程序执行效率。

1. 注册文件描述符，使poll同时监听用户输入和网络连接
```c++
// struct pollfd {
// int fd;        /* 文件描述符 */
// short events; /* 等待的事件,events成员告诉poll监听fd上的哪些事件，它是一系列事件的按位或 */
// short revents; /* 实际发生了的事件 ,由内核修改，以通知应用程序fd上实际发生了哪些事件*/
// };
// 事件 events
// POLLIN 有数据可读
// POLLRDNORM 有普通数据可读
// POLLRDBAND 有优先数据可读
// POLLPRI 有紧急数据可读
// POLLOUT 数据可写
// POLLWRNORM 普通数据可写
// POLLWRBAND 优先数据可写
// POLLMSGSIGPOLL 消息可用
// POLLRDHUP,通常用来判断对端是否关闭,当我们使用POLLRDHUP（EPOLLRDHUP）事件来判断对端是否关闭时，POLLRDHUP（EPOLLRDHUP）事件的处理应放在POLLIN（EPOLLIN）事件的前面，避免将对端关闭当做一次读事件，而无法处理POLLRDHUP（EPOLLRDHUP）事件。
// 返回事件 revent
// 除了 事件外；还有
// POLLERR 指定描述符发生错误
// POLLHUP 指定文件描述符挂起事件
// POLLNVAL 指定描述符非法
pollfd fds[2];
fds[0].fd = 0;//标准输入
fds[0].events = POLLIN;//数据可读
fds[0].revents = 0;
fds[1].fd = sockfd;
fds[1].events = POLLIN | POLLRDHUP;//数据可读与TCP连接被对方关闭，或者对方关闭了写操作事件相互或操作
fds[1].revents = 0;
```
2. 使用splice将用户输入的数据直接写到sockfd上（零拷贝）
```c
//该条件为真，表明用户在键盘上输入数据了
if( fds[0].revents & POLLIN )
{
    //ssize_t splice(int fd_in, loff_t *off_in, int fd_out,loff_t *off_out, size_t len, unsigned int flags);
    //splice（）在两个文件描述符之间移动数据，而无需在内核地址空间和用户地址空间之间进行复制。
    //它从文件描述中传输最多len字节的数据。将fd_in传递到文件描述符fd_out，其中文件描述符之一必须引用管道。
    //对于fd_in来说，若其是一个管道文件描述符，则off_in必须被设置为NULL，若它不是一个管道描述符，则off_in表示从输入数据流的何处开始读入数据，此时，其被设置为NULL，则说明从输入数据的当前偏移位置读入。否则off_in指出具体的偏移位置。
    //以上对于fd_out和off_out同样适用，只不过其用于输出数据流。
    //使用splice将用户输入的数据直接写到sockfd上（零拷贝）
    //首先将用户输入传进写管道端(pipefd[1]),然后socket从读管道端(pipefd[0])读出数据
    //从而实现将用户输入传入到socket中
    ret = splice( 0, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE );
    ret = splice( pipefd[0], NULL, sockfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE );
}
```

## 单进程服务器
服务器程序使用poll同时管理监听socket和连接socket
1. 将socket文件描述符设为非阻塞
```c++
int setnonblocking( int fd )
{
    //fcntl函数，正如其名字（file control）描述的那样，提供了对文件描述符的各种控制操作
    int old_option = fcntl( fd, F_GETFL );///*获取文件描述符旧的状态标志*/
    int new_option = old_option | O_NONBLOCK;///*设置非阻塞标志*/
    fcntl( fd, F_SETFL, new_option );//返回文件描述符旧的状态标志，以便日后恢复该状态标志
    return old_option;
}
```
解释下为什么将socket文件描述符设为非阻塞。

一个socket文件描述符是否设为为阻塞模式，只会影响到connect/accept/send/recv等socket API函数，不会影响到select/poll/epoll_wait函数，后三个函数的超时或者阻塞时间由函数自身控制。socket默认是阻塞模式，例如对于connetc而言，connect函数会一直阻塞到连接成功或者超时出错，如果阻塞时间较长，自然会影响程序的性能。如果将connect设置为非阻塞模式，无论连接是否成功，都会立即返回，程序会继续进行，例如主程序继续产生新的连接而不是一直阻塞，那么这种方式会出现一个新问题，那我们如何知道连接是否成功呢？这就用到了I/O复用技术，由select/poll/epoll来判断哪些连接是成功的，此外，Linux系统还需要额外增加一步：使用getsockopt函数判断此时socket是否有误。

该服务器程序将accept返回后的connfd设为非阻塞的，会影响send和recv函数，这两个函数都会立即返回，send函数即使因为对端TCP窗口太小发不出去也会立即返回，recv函数如果无数据可收也会立即返回，次时两个函数返回值都是-1，错误码为EAGIN。事实上，这种情况函数返回值有三种情形：

大于0：成功发送或者收取n个字节

等于0：对端关闭连接

小于0（-1）：出错或者信号中断或者对端TCP窗口太小数据发不出去或者当前网卡缓冲区已经没有数据可收
2. 核心逻辑过程
```c++
while(1)
{
    //poll监听注册的文件描述符（套接字）
    //遍历所有的文件描述符
    for(...)
    {
        if(当前文件描述符为监听套接字并且有连接事件发生)
        {
            //调用accept接受连接
            //并将新的connfd描述符注册到poll中
        }
        else if(当前文件描述符发生错误事件)
        {
            //处理错误事件
        }
        else if(客户端关闭连接事件发生)
        {
            //从poll监听队列中删除改连接对应的文件描述符
        }
        else if(客户端发来数据)
        {
            //调用recv接收数据，然后将数据写入给其他客户端缓存区中

        }
        else if(服务器要发送数据)
        {
            //调用send发送给其他客户端
        }

    }
    //关闭监听文件描述符
}
```
## 多进程服务器
一个子进程处理一个客户连接。同时，将所有客户socket连接的读缓冲设计为一块共享内存,每个客户使用其中的一段空间往里面写数据。每个客户端都可以读这块共享内存，这样当一个客户端发来数据时不需要调用send发送给其他客户端。
### 核心逻辑
```c++
while(!stop_server)
{
    //epoll返回就绪的文件描述符
    //遍历就绪的文件描述符
    for(...)
    {
        if(当前文件描述符为监听描述符)
        {
            //调用accept完成连接
            //创建双向管道以便后续父子进程通信
            //调用fork创建子进程
            if(当前进程为子进程)
            {
                //关闭一些文件描述符
                /*子进程使用I/O复用技术来同时监听两个文件描述符：客户连接socket、与父进程通
                信的管道文件描述符*/
                while(!stop_child)
                {
                    //epoll监听文件描述符
                    //遍历就绪的文件描述符
                    for(...)
                    {
                        if(当前文件描述符为connfd)
                        {
                            //调用recv接收数据，这里使用了共享内存，将接收的数据写到共享内存中
                            if(接收数据成功)
                            {
                                //向管道中写入当前连接的编号来通知父进程接收数据完成，让父进程通知其他客户，从共享内存中读取数据。

                                //如果没有共享内存，这里应该需要调用send将接收的数据发送给其他客户
                            }
                        }
                        else if(当前文件描述符为管道)
                        {
                            //调用recv接收从父进程传送过来的客户编号
                            //然后读取那个客户在共享内存中写入的数据

                            //如果没有共享内存，这里应该调用recv接收那个客户的数据

                            //调用send将数据发送给客户端
                        }
                    }
                }
                //处理完毕后退出，结束子进程
            }
            else if(父进程)
            {
                //关闭相关文件描述符
                //将与子进程通信的管道文件描述符添加到epoll注册表中
                //父进程没有把connfd添加到epoll中，是因为监听connfd的任务交给了子进程，在子进程那里监听connfd
            }
        }
        else if(当前描述符为信号管道)
        {
            //处理相应的信号
        }
        else if(当前描述符为某一个与子进程通信的管道)
        {
            //调用recv接收判断是哪个子进程
            //遍历当前所有客户，将上面那个子进程编号数据传给其他客户
            for(...)
            {
                //调用send通过管道将编号发送给子进程，至于发送客户输入的数据由子进程完成
            }
        }
    }
}
```

### 1、fork()系统调用

[参考链接](https://blog.csdn.net/bandaoyu/article/details/109541053#:~:text=fork%EF%BC%88%EF%BC%89%E5%87%BD%E6%95%B0,%E5%8F%AF%E4%BB%A5%E5%81%9A%E4%B8%8D%E5%90%8C%E7%9A%84%E4%BA%8B%E3%80%82)
1. 基础知识

一个进程，包括代码、数据和分配给进程的资源。fork（）函数通过系统调用创建一个与原来进程几乎完全相同的进程，也就是两个进程可以做完全相同的事，但如果初始参数或者传入的变量不同，两个进程也可以做不同的事。

一个进程调用fork（）函数后，系统先给新的进程分配资源，例如存储数据和代码的空间。然后把原来的进程的所有值都复制到新的新进程中，只有少数值与原来的进程的值不同。相当于克隆了一个自己。
```c++
/*
*  fork_test.c
*  version 1
*  Created on: 2010-5-29
*      Author: wangth
*/
#include <unistd.h>
#include <stdio.h>
 
int main ()
{
    pid_t fpid; //fpid表示fork函数返回的值
    int count = 0;
    fpid = fork();
    if (fpid < 0)
        printf("error in fork!");
    else if (fpid == 0)
    {
        printf("i am the child process, my process id is %d/n", getpid());
        printf("我是爹的儿子/n");
        count++;
    }
    else
    {
        printf("i am the parent process, my process id is %d/n", getpid());
        printf("我是孩子他爹/n");
        count++;
    }
    printf("统计结果是: %d/n", count);
    return 0;
}
```
运行结果是
```c
i am the child process, my process id is 5574

我是爹的儿子

统计结果是: 1

i am the parent process, my process id is 5573

我是孩子他爹

统计结果是: 1
```
在语句fpid=fork()之前，只有一个进程在执行这段代码，但在这条语句之后，就变成两个进程在执行了，这两个进程的几乎完全相同，将要执行的下一条语句都是if(fpid<0)……

在fork函数执行完毕后，如果创建新进程成功，则出现两个进程，一个是子进程，一个是父进程。在子进程中，fork函数返回0，在父进程中，fork返回新创建子进程的进程ID。我们可以通过fork返回的值来判断当前进程是子进程还是父进程。

fork出错可能有两种原因：

1）当前的进程数已经达到了系统规定的上限，这时errno的值被设置为EAGAIN。

2）系统内存不足，这时errno的值被设置为ENOMEM。

创建新进程成功后，系统中出现两个基本完全相同的进程，这两个进程执行没有固定的先后顺序，哪个进程先执行要看系统的进程调度策略。

还有人可能疑惑为什么不是从#include处开始复制代码的，这是因为fork是把进程当前的情况拷贝一份，执行fork时，进程已经执行完了int count=0;
fork只拷贝下一个要执行的代码到新的进程。

2. fork()在网络编程中的应用
父进程调用fork之前打开的所有文件描述符在fork返回后由子进程共享。父进程调用accept之后调用fork，所接受的已连接套接字随后在父进程和子进程之间共享，通常情况下，子进程接着读写这个已连接套接字，父进程则关闭这个已链接套接字。

在子进程处理完毕后，不需要显示地关闭已链接套接字，因为通常子进程处理完毕后会调用exit，它会关闭所有由内核打开地描述符。

我们知道，对于TCP套接字调用close会导致发送一个FIN，随后是正常地TCP连接终止序列，为什么父进程对connfd调用close没有终止它与客户的连接呢？其实每个文件或者套接字都有一个引用计数，引用计数在文件表项中维护，它是当前打开着的引用该文件或者套接字的描述符的个数，accept返回后与connfd关联的文件表项中引用计数为1，然而，fork之后，这个文件描述符被父子进程共享，因此引用计数值变为2，这样党父进程关闭connfd时，它只是把相应的引用计数从2变为1，该套接字的清理与资源释放要等到其引用计数变为0时才能到达。

### socketpair()函数
[参考链接](https://wuyaogexing.com/70/219250.html#:~:text=socketpair%20%28%29%E5%87%BD%E6%95%B0%E7%94%A8%E4%BA%8E%E5%88%9B%E5%BB%BA%E4%B8%80%E5%AF%B9%E6%97%A0%E5%90%8D%E7%9A%84%E3%80%81%E7%9B%B8%E4%BA%92%E8%BF%9E%E6%8E%A5%E7%9A%84%E5%A5%97%E6%8E%A5%E5%AD%90%E3%80%82,%E5%A6%82%E6%9E%9C%E5%87%BD%E6%95%B0%E6%88%90%E5%8A%9F%EF%BC%8C%E5%88%99%E8%BF%94%E5%9B%9E0%EF%BC%8C%E5%88%9B%E5%BB%BA%E5%A5%BD%E7%9A%84%E5%A5%97%E6%8E%A5%E5%AD%97%E5%88%86%E5%88%AB%E6%98%AFsv%20%E5%92%8Csv%20%EF%BC%9B%E5%90%A6%E5%88%99%E8%BF%94%E5%9B%9E-1%EF%BC%8C%E9%94%99%E8%AF%AF%E7%A0%81%E4%BF%9D%E5%AD%98%E4%BA%8Eerrno%E4%B8%AD%E3%80%82)

socketpair()函数用于创建一对无名的、相互连接的套接字。用于进程间或者进程内部的通信 ，在该程序中用于父子进程间的通信。不过，管道只能用于有关联的两个进程（比如父、子进程）间的通信。还有进程内部信号的通信也使用管道
如果函数成功，则返回0，创建好的套接字分别是sv[0]和sv[1]；否则返回-1，错误码保存于errno中。

基本用法： 

1. 这对套接字可以用于全双工通信，每一个套接字既可以读也可以写。例如，可以往sv[0]中写，从sv[1]中读；或者从sv[1]中写，从sv[0]中读； 
2. 如果往一个套接字(如sv[0])中写入后，再从该套接字读时会阻塞，只能在另一个套接字中(sv[1])上读成功； 
3. 读、写操作可以位于同一个进程，也可以分别位于不同的进程，如父子进程。如果是父子进程时，一般会功能分离，一个进程用来读，一个用来写。因为文件描述副sv[0]和sv[1]是进程共享的，所以读的进程要关闭写描述符, 反之，写的进程关闭读描述符。 

### 信号处理
[参考链接](https://blog.csdn.net/orangeboyye/article/details/125596135)
