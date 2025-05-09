#ifndef __REACTOR__
#define __REACTOR__

#include <sys/epoll.h>
#include <stdio.h>
#include <unistd.h> // read write
#include <fcntl.h> // fcntl
#include <sys/types.h> // listen
#include <sys/socket.h> // socket
#include <errno.h> // errno
#include <arpa/inet.h> // inet_addr htons
// #include <netinet/tcp.h>
#include <assert.h> // assert
#include<stdlib.h>
#include<string.h>
#include"chainbuffer/buffer.h"

//限制 epoll_wait 一次返回的最大事件数，平衡效率与处理压力
#define MAX_EVENT_NUM 512
//理论上单个 Reactor 能管理的最大连接数（受限于系统资源，实际可能更小）
#define MAX_CONN ((1<<16)-1) // 最大连接数（65535，理论端口号上限）

typedef struct event_s event_t;       // 事件对象前向声明
typedef struct reactor_s reactor_t;   // Reactor 核心对象前向声明

// 事件回调函数指针（参数：文件描述符、事件类型、私有数据）
typedef void (*event_callback_fn)(int fd, int events, void *privdata);
// 错误回调函数指针（参数：文件描述符、错误信息）
typedef void (*error_callback_fn)(int fd, char * err);

// 每个event_t 对应一个需要监控的 IO 实体（如客户端连接、监听套接字）
struct event_s {
    int fd;   //关联的文件描述符
    reactor_t *r;  //所属的reactor对象
    buffer_t *in;   //输入缓冲区（接收数据）
    buffer_t *out;  //输出缓冲区（发送数据）
    event_callback_fn read_fn;  //读事件回调函数
    event_callback_fn write_fn; //写事件回调
    error_callback_fn error_fn; //错误回调
};

/*
核心职责：作为事件驱动的核心管理器，负责：
epfd：持有 epoll 实例（通过 epoll_create 创建），实际执行 IO 事件监控。
listenfd：若作为服务器，存储监听套接字（通过 create_server 初始化）。
stop：控制事件循环的启停（通过 stop_eventloop 设置）。
events：所有注册的 event_t 对象数组（可能以 fd 为索引快速查找）。
fire：存储 epoll_wait 返回的触发事件（每次循环处理这些事件）。
*/
struct reactor_s {
    int epfd;   //epoll实例的文件描述符
    int listenfd;  //监听套接字的文件描述符
    int stop;  //事件循环的停止标志（0运行，1停止）
    event_t *events;  //所有注册的事件对象数组（按 fd 索引）
    int iter;   //事件遍历使用的迭代器
    struct epoll_event fire[MAX_EVENT_NUM];  //epoll触发的事件列表
};

int event_buffer_read(event_t *e);   // 从 fd 读取数据到输入缓冲区 in
int event_buffer_write(event_t *e, void * buf, int sz);  // 将 buf 数据写入输出缓冲区 out 或直接发送

reactor_t * create_reactor();        // 创建并初始化一个 Reactor 实例
void release_reactor(reactor_t * r);  // 释放 Reactor 及其所有资源

event_t * new_event(reactor_t *R, int fd,  // 创建事件对象并注册到 Reactor
    event_callback_fn rd,                 // 读回调
    event_callback_fn wt,                 // 写回调
    error_callback_fn err);               // 错误回调
void free_event(event_t *e);               // 释放事件对象及其缓冲区

buffer_t* evbuf_in(event_t *e);   // 获取事件的输入缓冲区（in）
buffer_t* evbuf_out(event_t *e);  // 获取事件的输出缓冲区（out）
reactor_t* event_base(event_t *e); // 获取事件所属的 Reactor

int set_nonblock(int fd);  // 设置文件描述符为非阻塞模式

int add_event(reactor_t *R, int events, event_t *e);  // 向 Reactor 添加事件（注册到 epoll）
int del_event(reactor_t *R, event_t *e);              // 从 Reactor 删除事件（从 epoll 移除）
int enable_event(reactor_t *R, event_t *e, int readable, int writeable);  // 动态启用/禁用读写事件监听

void eventloop_once(reactor_t * r, int timeout);  // 运行一次事件循环（处理一次 epoll_wait 结果）
void stop_eventloop(reactor_t * r);               // 停止事件循环（设置 r->stop=1）
void eventloop(reactor_t * r);                    // 启动事件循环（持续运行直到 stop）

int create_server(reactor_t *R, short port, event_callback_fn func);  // 创建服务器并注册监听事件

#endif