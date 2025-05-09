#include "reactor.h"
#include <sys/epoll.h>

// 参数：无
// 返回值：reactor_t*（Reactor 实例指针）
// 功能：初始化 Reactor 核心资源（epoll 实例、事件数组等）
reactor_t *create_reactor(){
    //分配reactor结构体的内存
    reactor_t *r = (reactor_t *)malloc(sizeof(*r));

    //创建epoll实例
    r->epfd = epoll_create(1);

    //初始化成员变量
    r->listenfd = 0;
    r->stop = 0;
    r->iter = 0;  // 事件迭代器（用于查找空闲事件槽位）

    //分配事件数组，最大65535
    r->events = (event_t *)malloc(sizeof(event_t) * MAX_CONN);
    memset(r->events,0,sizeof(event_t) * MAX_CONN);
    // 清零 epoll 触发事件列表（避免脏数据）
    memset(r->fire, 0, sizeof(struct epoll_event) * MAX_EVENT_NUM);

    // 初始化定时器（示例中注释，实际可能用于超时管理）
    // init_timer();
    
    return r;
}

// 参数：reactor_t* r（待释放的 Reactor 实例）
// 功能：释放 Reactor 占用的所有资源
void release_reactor(reactor_t * r) {
    // 释放事件数组内存（存储所有注册的 event_t）
    free(r->events);
    // 关闭 epoll 实例文件描述符（释放内核资源）
    close(r->epfd);
    // 释放 Reactor 结构体本身
    free(r);
}

// 参数：reactor_t* r（当前 Reactor 实例）
// 返回值：event_t*（空闲的事件对象指针）
// 功能：从事件数组中查找一个未使用的 event_t（通过 r->iter 迭代）
static event_t *_get_event_t(reactor_t *r){
    //迭代器自增（用于循环查找空闲槽位）
    r->iter++;
    //循环检查时间数组，直到找到fd==0的空闲槽位（fd > 0表示已经被占用）
    while(r->events[r->iter & MAX_CONN].fd > 0){
        r->iter++;
    }
    // 返回空闲事件对象的指针（通过位运算限制索引范围在 MAX_CONN 内）
    //c语言返回结构体会返回副本，所以必须返回地址
    return &r->events[r->iter];
}

// 参数：
//   reactor_t* R：事件所属的 Reactor 实例
//   int fd：事件关联的文件描述符（如套接字）
//   event_callback_fn rd：读事件回调函数（fd 可读时触发）
//   event_callback_fn wt：写事件回调函数（fd 可写时触发）
//   error_callback_fn err：错误回调函数（fd 异常时触发）
// 返回值：event_t*（新创建的事件对象指针）
// 功能：初始化事件对象，分配缓冲区，并绑定回调函数
event_t *new_event(reactor_t *R, int fd,
    event_callback_fn rd,
    event_callback_fn wt,
    error_callback_fn err){
    // 断言：至少有一个回调函数被注册（避免无效事件）
    assert(rd != 0 || wt != 0 || err != 0);

    //从reactor事件数组中获取空闲事件槽位
    event_t *e = _get_event_t(R);

    //初始化事件对象成员
    e->r = R;
    e->fd = fd;
    e->in = buffer_new(16 * 1024);
    e->out = buffer_new(16 * 1024);
    e->read_fn = rd;
    e->write_fn = wt;
    e->error_fn = err;

    return e;
} 

// 参数：event_t* e（事件对象）
// 返回值：buffer_t*（输入缓冲区指针）
// 功能：获取事件的输入缓冲区（封装成员访问，提高安全性）
buffer_t* evbuf_in(event_t *e) {
    return e->in;
}

// 类似 evbuf_in，获取输出缓冲区
buffer_t* evbuf_out(event_t *e) {
    return e->out;
}

// 参数：event_t* e（事件对象）
// 返回值：reactor_t*（事件所属的 Reactor 实例）
// 功能：获取事件关联的 Reactor 实例
reactor_t* event_base(event_t *e) {
    return e->r;
}

// 参数：event_t* e（待释放的事件对象）
// 功能：释放事件的输入/输出缓冲区
void free_event(event_t *e) {
    buffer_free(e->in);   // 释放输入缓冲区
    buffer_free(e->out);  // 释放输出缓冲区
}

// 参数：int fd（待设置的文件描述符）
// 返回值：int（0=成功，-1=失败）
// 功能：将文件描述符设置为非阻塞模式（Reactor 核心要求）
int set_nonblock(int fd){
    //获取当前文件状态标志
    int flag = fcntl(fd,F_GETFD,0);
    //添加 O_NONBLOCK 标志（非阻塞模式）
    return fcntl(fd,F_SETFD,flag | O_NONBLOCK);
}

// 参数：
//   reactor_t* R：事件所属的 Reactor 实例
//   int events：关注的事件类型（如 EPOLLIN|EPOLLOUT）
//   event_t* e：待注册的事件对象
// 返回值：int（0=成功，1=失败）
// 功能：将事件对象关联的 fd 注册到 epoll 实例中
int add_event(reactor_t *R, int events, event_t *e){
    struct epoll_event ev; //epoll时事件结构体
    ev.events = events;  //关注的事件类型
    ev.data.ptr = e;  // 将事件对象指针绑定到 epoll 事件（后续触发时通过此指针获取 event_t）

    // 调用 epoll_ctl 注册事件（EPOLL_CTL_ADD：添加事件）
    if (epoll_ctl(R->epfd, EPOLL_CTL_ADD, e->fd, &ev) == -1) {
        printf("add event err fd = %d\n", e->fd);
        return 1;  // 注册失败
    }
    return 0;  // 注册成功
}

int del_event(reactor_t *R, event_t *e) {
    epoll_ctl(R->epfd, EPOLL_CTL_DEL, e->fd, NULL);
    free_event(e);
    return 0;
}

// 函数：启用/禁用文件描述符的读/写事件监听（基于 epoll 模型）
// 返回值：0=成功，1=失败（epoll_ctl 调用出错）
int enable_event(reactor_t *R, event_t *e, int readable, int writeable) {
    // 定义 epoll 事件结构体（用于传递给 epoll_ctl）
    struct epoll_event ev;

    // 初始化事件掩码：根据 readable/writeable 标志组合 EPOLLIN/EPOLLOUT
    // 若 readable 为非零，设置读事件标志 EPOLLIN；否则不设置
    // 若 writeable 为非零，设置写事件标志 EPOLLOUT；否则不设置
    ev.events = (readable ? EPOLLIN : 0) | (writeable ? EPOLLOUT : 0);

    // 将事件对象指针 e 关联到 epoll 事件的 data.ptr（便于事件触发时获取上下文）
    // epoll 推荐使用 data.ptr 存储自定义数据（比存 fd 更灵活，尤其多事件场景）
    ev.data.ptr = e;

    // 调用 epoll_ctl 对目标文件描述符 e->fd 执行修改操作（EPOLL_CTL_MOD）
    // 作用：更新该 fd 在 epoll 内核事件表中的监听事件（仅修改已存在的 fd，不新增）
    // 参数说明：
    // - R->epfd：epoll 内核事件表的文件描述符（来自 Reactor 实例）
    // - EPOLL_CTL_MOD：操作类型（修改已有事件）
    // - e->fd：需要修改事件监听的文件描述符
    // - &ev：新的事件配置（事件掩码 + 关联数据）
    if (epoll_ctl(R->epfd, EPOLL_CTL_MOD, e->fd, &ev) == -1) {
        // epoll_ctl 调用失败（如 fd 未注册到 epoll 中，或参数错误）
        // 常见错误码：EBADF（无效 fd）、EINVAL（无效操作类型）
        return 1;  // 返回 1 表示操作失败
    }

    // epoll_ctl 调用成功，返回 0 表示操作成功
    return 0;
}

// 参数：
//   reactor_t* r：当前 Reactor 实例
//   int timeout：epoll_wait 超时时间（ms，-1=永久阻塞）
// 功能：执行一次事件循环（处理 epoll_wait 返回的触发事件）
void eventloop_once(reactor_t *r, int timeout){
    int n = epoll_wait(r->epfd,r->fire,MAX_EVENT_NUM,timeout);

    //遍历所有触发的事件
    for(int i = 0;i < n;i++){
        struct epoll_event *e = &r->fire[i]; //当前触发的事件
        int mask = e->events;   //事件类型

        // 处理异常事件：
        // EPOLLERR：fd 发生错误（如对端关闭）
        // EPOLLHUP：fd 被挂起（如连接断开）
        if (e->events & EPOLLERR) mask |= EPOLLIN | EPOLLOUT;  // 强制标记为可读/可写（触发回调处理错误）
        if (e->events & EPOLLHUP) mask |= EPOLLIN | EPOLLOUT;

        // 从 epoll 事件中获取绑定的 event_t 对象
        event_t *et = (event_t *)e->data.ptr;

        //处理读事件
        if(mask & EPOLLIN){
            if(et->read_fn){
                et->read_fn(et->fd,EPOLLIN,et);
            }
        }

        //处理写事件
        if(mask & EPOLLOUT){
            if (et->write_fn) {  // 调用用户注册的写回调
                et->write_fn(et->fd, EPOLLOUT, et);
            }else{
                // 如果用户未注册写回调（如服务器监听套接字），自动发送输出缓冲区数据
                uint8_t *buf = buffer_write_atmost(evbuf_out(et));  //获取输出缓冲区的数据
                event_buffer_write(et,buf,buffer_len(evbuf_out(et))); //写入套接字
            } 
        }
    }
}

// 参数：reactor_t* r（当前 Reactor 实例）
// 功能：设置停止标志，使事件循环退出
void stop_eventloop(reactor_t * r) {
    r->stop = 1;  // 将停止标志置为 1（eventloop 循环会检测此标志）
}

// 参数：reactor_t* r（当前 Reactor 实例）
// 功能：持续运行事件循环，直到 stop 标志被设置
void eventloop(reactor_t * r) {
    // 循环运行，直到 r->stop 为 1
    while (!r->stop) {
        // 计算定时器超时时间（示例中注释，实际用于处理定时任务）
        // int timeout = find_nearest_expire_timer();
        
        // 执行单次事件循环（超时时间示例中为 -1，永久阻塞直到事件触发）
        eventloop_once(r, /*timeout*/ -1);
        
        // 处理超时的定时器任务（示例中注释）
        // expire_timer();
    }
}

// 参数：
//   reactor_t* R：服务器所属的 Reactor 实例
//   short port：监听端口号
//   event_callback_fn func：新连接事件的回调函数（监听套接字可读时触发）
// 返回值：int（0=成功，-1=失败）
// 功能：创建 TCP 服务器，绑定端口，监听连接，并注册到 Reactor
int create_server(reactor_t *R, short port, event_callback_fn func){
    // 1. 创建 TCP 套接字（AF_INET：IPv4，SOCK_STREAM：TCP）
    int listenfd = socket(AF_INET,SOCK_STREAM,0);
    if (listenfd < 0) {
        printf("create listenfd error!\n");
        return -1;
    }

    // 2. 配置服务器地址（IPv4，指定端口，监听所有网卡）
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    // 3. 设置地址复用（避免端口未释放时无法重启服务器）
    int reuse = 1;
    if(setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,(void *)&reuse,sizeof(int)) == -1){
        printf("reuse address error: %s\n", strerror(errno));
        return -1;
    }

    // 4. 绑定端口
    if(bind(listenfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in)) < 0){
        printf("bind error %s\n", strerror(errno));
        return -1;
    }

    // 5. 监听连接（backlog=5：最大等待连接数）
    if (listen(listenfd, 5) < 0) {
        printf("listen error %s\n", strerror(errno));
        return -1;
    }

    // 6. 设置监听套接字为非阻塞模式（关键：Reactor 要求非阻塞）
    if(set_nonblock(listenfd) < 0){
        printf("set_nonblock error %s\n", strerror(errno));
        return -1;
    }

    // 7. 记录监听套接字到 Reactor
    R->listenfd = listenfd;

    // 8. 创建事件对象（监听套接字的读事件触发时，调用 func 处理新连接）
    event_t *e = new_event(R,listenfd,func,0,0);

    // 9. 向 epoll 注册监听套接字的读事件（新连接到达时触发）
    add_event(R, EPOLLIN, e);

    printf("listen port : %d\n", port);
    return 0;
}

// 参数：event_t* e（待读取数据的事件对象）
// 返回值：int（读取的总字节数，0=连接关闭，-1=错误）
// 功能：循环读取套接字数据到输入缓冲区（处理非阻塞读的“读不完整”问题）
int event_buffer_read(event_t *e){
    int fd = e->fd;
    int num = 0;  //累计读取的字节数

    while(1){  // 非阻塞读可能需要多次读取（直到内核缓冲区无数据）
        char buf[1024] = {0};    //临时缓冲区（每次读 1KB）
        int n = read(fd,buf,1024); 

        if(n == 0){  // 对端关闭连接（read 返回 0 表示连接正常关闭）
            printf("close connection fd = %d\n", fd);
            if (e->error_fn)  // 触发错误回调（通知上层连接关闭）
                e->error_fn(fd, "close socket");
            del_event(e->r, e);  // 从 epoll 移除事件并释放资源
            close(fd);  // 关闭套接字
            return 0;
        }else if(n < 0){  // 读错误
            if(errno ==EINTR)  //被信号中断，继续循环
                continue;
            if (errno == EWOULDBLOCK)  // 内核缓冲区无数据（非阻塞特性），退出循环
                break;
            // 其他错误（如连接重置）
            printf("read error fd = %d err = %s\n", fd, strerror(errno));
            if (e->error_fn)  // 触发错误回调
                e->error_fn(fd, strerror(errno));
            del_event(e->r, e);  // 清理资源
            close(fd);
            return 0;
        }else {  // 成功读取 n 字节
            printf("recv data from client:%s", buf);  // 打印接收的数据（示例）
            buffer_add(evbuf_in(e), buf, n);  // 将数据追加到输入缓冲区（buffer_t 内部管理内存）
        }
        num += n;  // 累计读取字节数
    }
    return num;
}

// 参数：
//   event_t* e：待写数据的事件对象
//   void * buf：待写数据的缓冲区
//   int sz：待写数据的字节数
// 返回值：int（实际写入的字节数，0=内核缓冲区满，-1=错误）
// 功能：循环写入数据到套接字（处理非阻塞写的“写不完整”问题）
static int _write_socket(event_t *e,void *buf,int sz){
    int fd = e->fd;  // 事件关联的套接字
    
    while (1) {  // 非阻塞写可能需要多次写入（直到内核缓冲区满）
        int n = write(fd, buf, sz);  // 调用系统 write 发送数据
        
        if (n < 0) {  // 写错误
            if (errno == EINTR)  // 被信号中断，继续循环（重试）
                continue;
            if (errno == EWOULDBLOCK)  // 内核缓冲区满（非阻塞特性），退出循环
                break;
            // 其他错误（如连接断开）
            if (e->error_fn)  // 触发错误回调
                e->error_fn(fd, strerror(errno));
            del_event(e->r, e);  // 清理资源
            close(e->fd);
        }
        return n;  // 返回实际写入的字节数
    }
    return 0;  // 内核缓冲区满，未写入任何数据
}

// 参数：
//   event_t* e：待写数据的事件对象
//   void * buf：待写数据的缓冲区
//   int sz：待写数据的字节数
// 返回值：int（1=全部发送，0=部分发送，-1=错误）
// 功能：将数据写入输出缓冲区或直接发送（处理“写不完整”问题）

/*
若输出缓冲区（用户态）为空，优先尝试直接发送数据到内核缓冲区。
若发送不完全（内核缓冲区满），将剩余数据暂存到用户态输出缓冲区，注册 “可写事件”，等待下次触发时继续发送。
若输出缓冲区已有数据（之前未发完），直接追加新数据到用户态缓冲区，避免频繁系统调用。

完整示例：分两次发送 11 字节数据
第一次调用（内核缓冲区剩 5 字节）：
发送 5 字节，剩余 6 字节存入 out 缓冲区，注册 EPOLLOUT。
EPOLLOUT 触发（内核缓冲区空闲）：
从 out 取出 6 字节，全部发送成功，out 缓冲区清空，取消 EPOLLOUT 监听（或保持监听等待新数据）。
第二次调用（发送新的 10 字节数据）：
out 缓冲区为空，直接发送 10 字节（假设内核缓冲区空间足够），全部发送成功，无需暂存。
*/
int event_buffer_write(event_t *e, void * buf, int sz) {
    buffer_t *out = evbuf_out(e);  // 获取输出缓冲区

    if(buffer_len(out) == 0){
        int n = _write_socket(e,buf,sz);
        if (n == 0 || n < sz) {  // 未完全发送（内核缓冲区满）
            // 将未发送的数据追加到输出缓冲区（等待下次可写事件）
            buffer_add(out, (char *)buf+n, sz-n);
            // 注册写事件（下次可写时触发，继续发送）
            enable_event(e->r, e, 1, 1);
            return 0;
        } else if (n < 0)  // 写错误
            return 0;
        return 1;  // 全部发送成功
    }

    // 输出缓冲区已有数据，将新数据追加到缓冲区（等待统一发送）
    buffer_add(out, (char *)buf, sz);
    return 1;
}