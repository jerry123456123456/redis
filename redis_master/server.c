#include "reactor.h"

void read_cb(int fd,int events,void *privdata){
    event_t *e = (event_t *)privdata;
    //将网络中读缓冲区的数据拷贝到用户态缓冲区
    int n = event_buffer_read(e);

    if(n > 0){
        //buffer_search检测是否是一个完整的数据包
        int len = buffer_search(evbuf_in(e),"\n",1);
        if(len > 0 && len < 1024){
            char buf[1024] = {0};
            buffer_remove(evbuf_in(e),buf,len);
            //发回客户端
            event_buffer_write(e,buf,len);
        }
    }
}

void accept_cb(int fd,int events,void *privdata){
    event_t *e = (event_t*) privdata;

    // 定义客户端地址结构体（用于存储新连接的客户端信息）
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));  // 初始化内存为0
    socklen_t len = sizeof(addr);  // 地址结构体长度（需传递指针给accept函数）

    int clientfd = accept(fd,(struct sockaddr*)&addr,&len);

    // 错误处理：若clientfd<=0表示接受连接失败（如队列空或系统错误）
    if (clientfd <= 0) {
        printf("accept failed\n");  // 打印失败信息
        return;  // 直接返回，不处理该连接
    }

    // 打印客户端地址信息（调试用）
    // inet_ntop函数：将二进制IP地址转换为点分十进制字符串（如"192.168.1.1"）
    // AF_INET表示IPv4地址族，&addr.sin_addr是客户端IP地址的二进制形式
    // str是存储结果的缓冲区，sizeof(str)是缓冲区长度（INET_ADDRSTRLEN是IPv4的最大长度宏）
    char str[INET_ADDRSTRLEN] = {0};
    printf("recv from %s at port %d\n", 
        inet_ntop(AF_INET, &addr.sin_addr, str, sizeof(str)),  // 客户端IP地址
        ntohs(addr.sin_port)  // 客户端端口号（ntohs将网络字节序转换为主机字节序）
    );

    // 为新客户端创建事件对象（用于后续事件监听）
    event_t *ne = new_event(event_base(e),clientfd,read_cb,0,0);

    // 将新事件对象添加到事件循环中，监听读事件（EPOLLIN）
    add_event(event_base(e), EPOLLIN, ne);
    set_nonblock(clientfd);
}

int main(){
    reactor_t *R = create_reactor();

    if(create_server(R,8888,accept_cb) != 0){
        release_reactor(R);
        return 1;
    }

    eventloop(R);
    release_reactor(R);
    return 0;
}