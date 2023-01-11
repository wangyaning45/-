#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <fcntl.h>

#define BUFFER_SIZE 64

int main( int argc, char* argv[] )
{
    if( argc <= 2 )
    {
        //char *basename(char *path)返回文件路径最后的文件名
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    struct sockaddr_in server_address;
    bzero( &server_address, sizeof( server_address ) );
    server_address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &server_address.sin_addr );
    server_address.sin_port = htons( port );

    int sockfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( sockfd >= 0 );
    if ( connect( sockfd, ( struct sockaddr* )&server_address, sizeof( server_address ) ) < 0 )
    {
        printf( "connection failed\n" );
        close( sockfd );
        return 1;
    }
    // struct pollfd {
    // int fd;        /* 文件描述符 */
    // short events; /* 等待的事件,events成员告诉poll监听fd上的哪些事件，它是一系列事件的按位或 */
    // short revents; /* 实际发生了的事件 ,由内核修改，以通知应用程序fd上实际发生了哪些事件*/
    // };
    pollfd fds[2];
    fds[0].fd = 0;//标准输入
    fds[0].events = POLLIN;//数据可读
    fds[0].revents = 0;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN | POLLRDHUP;//数据可读与TCP连接被对方关闭，或者对方关闭了写操作事件相互或操作
    fds[1].revents = 0;
    char read_buf[BUFFER_SIZE];
    int pipefd[2];
    /*管道是由调用pipe函数来创建
     fd参数返回两个文件描述符,fd[0]指向管道的读端,
     fd[1]指向管道的写端。fd[1]的输出是fd[0]的输入。
    */
    int ret = pipe( pipefd );
    assert( ret != -1 );

    while( 1 )
    {
        //int poll(struct pollfd*fds,nfds_t nfds,int timeout);
        //nfds参数指定被监听事件集合fds的大小。
        //timeout参数指定poll的超时值，单位是毫秒。当timeout为-1
//时，poll调用将永远阻塞，直到某个事件发生；当timeout为0时，poll调
//用将立即返回。

        ret = poll( fds, 2, -1 );
        if( ret < 0 )
        {
            printf( "poll failure\n" );
            break;
        }
        //通过这种方式判断poll监听的fd实际发生的是哪个事件
        //该条件判断服务器是否关闭连接
        if( fds[1].revents & POLLRDHUP )
        {
            printf( "server close the connection\n" );
            break;
        }
        //该if为真，表示fd上有可读文件，即从服务器传来了数据
        //从socket中读取从服务器发来的数据
        else if( fds[1].revents & POLLIN )
        {
            memset( read_buf, '\0', BUFFER_SIZE );
            recv( fds[1].fd, read_buf, BUFFER_SIZE-1, 0 );
            printf( "%s\n", read_buf );
        }
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
    }
    
    close( sockfd );
    //管道也需要关闭吧？
    return 0;
}
