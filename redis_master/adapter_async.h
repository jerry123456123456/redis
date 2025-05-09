#ifndef __ADAPTER__
#define __ADAPTER__

#include <stdio.h>
#include <hiredis/hiredis.h>
#include <hiredis/alloc.h>
#include <hiredis/async.h>
#include "reactor.h"

//作为桥梁，将 hiredis 的异步上下文（redisAsyncContext）与 Reactor 的事件对象（event_t）绑定，记录事件监听状态（mask）
typedef struct{
    event_t e;  // Reactor 中的事件对象（来自 reactor.h）
    int mask;   // 当前监听的事件掩码（如 EPOLLIN | EPOLLOUT）
    redisAsyncContext *ctx;  // hiredis 的异步上下文（管理 Redis 连接的异步操作）
}redis_event_t;

//当 Reactor 检测到 Redis 连接可读时，调用此函数，通过 redisAsyncHandleRead 触发 hiredis 处理实际的读操作
// Reactor 触发读事件时的回调函数
// 参数说明：
// fd: 触发事件的文件描述符（Redis 连接的套接字）
// events: 触发的事件类型（如 EPOLLIN）
// privdata: 私有数据指针（指向 Reactor 事件对象 event_t）
static void redisReadHandler(int fd,int events,void *privdata){
    ((void)fd);      // 显式标记未使用参数（避免编译器警告）
    ((void)events);  // 显式标记未使用参数

    printf("redisReadHandler %d\n", fd);

    event_t *e = (event_t *)privdata;
    // 由于 redis_event_t 的第一个成员是 event_t e，可通过地址转换得到 redis_event_t
    redis_event_t *re = (redis_event_t *)(char *)e;

    // 调用 hiredis 的异步读处理函数，处理 Redis 连接的可读事件
    // 内部逻辑：从套接字读取数据，解析 Redis 响应，触发用户注册的回调（如 redisAsyncCommand）
    redisAsyncHandleRead(re->ctx);
}

//当 Reactor 检测到 Redis 连接可写时，调用此函数，通过 redisAsyncHandleWrite 触发 hiredis 处理实际的写操作
// Reactor 触发写事件时的回调函数（逻辑与读事件类似）
static void redisWriteHandler(int fd, int events, void *privdata) {
    ((void)fd);
    ((void)events);

    event_t *e = (event_t*)privdata;
    redis_event_t *re = (redis_event_t *)(char *)e;

    // 调用 hiredis 的异步写处理函数，处理 Redis 连接的可写事件
    // 内部逻辑：将用户待发送的命令（如通过 redisAsyncCommand 提交的命令）写入套接字
    redisAsyncHandleWrite(re->ctx);
}

// 更新 Reactor 中 Redis 事件的监听状态（添加/删除事件）
// 参数说明：
// privdata: redis_event_t 结构体指针（桥接对象）
// flag: 要更新的事件类型（EPOLLIN 或 EPOLLOUT）
// remove: 是否移除事件（1=移除，0=添加）
static void redisEventUpdate(void *privdata,int flag,int remove){
    redis_event_t *re = (redis_event_t *)privdata;
    reactor_t *r = re->e.r;
    int prevMask = re->mask;  // 记录更新前的事件掩码
    int enable = 0;  // 是否启用事件（用于 enable_event）

    //根据remove标志更新事件掩码mask
    if(remove){
        // 若当前未监听该事件，直接返回
        if ((re->mask & flag) == 0) return;
        re->mask &= ~flag;  // 从掩码中移除 flag 事件（如 EPOLLIN）
        enable = 0;        // 标记为“禁用”事件
    }else{
        //若当前已经监听该事件，直接返回
        if(re->mask & flag) return;
        re->mask |= flag;
        enable = 1;
    }

    // 获取 Redis 连接的套接字（来自 hiredis 上下文）
    int fd = re->ctx->c.fd;
    // 根据新的掩码状态，操作 Reactor 的事件监听
    if (re->mask == 0) {
        // 若掩码为空（无事件监听），从 Reactor 中删除该事件
        del_event(r, &re->e);  // 调用 Reactor 接口删除事件（reactor.h 中定义）
    } else if (prevMask == 0) {
        // 若之前无监听（prevMask=0），现在有新事件，向 Reactor 添加事件
        add_event(r, re->mask, &re->e);  // 调用 Reactor 接口添加事件（监听 re->mask 中的事件）
    } else {
        // 若之前已有监听，更新事件状态（启用/禁用特定事件）
        if (flag & EPOLLIN) {
            // 若是读事件（EPOLLIN），启用/禁用读事件监听
            enable_event(r, &re->e, enable, 0);  // 第三个参数：是否启用读事件
        } else if (flag & EPOLLOUT) {
            // 若是写事件（EPOLLOUT），启用/禁用写事件监听
            enable_event(r, &re->e, 0, enable);  // 第四个参数：是否启用写事件
        }
    }
}

// 向 Reactor 注册 Redis 连接的读事件监听
static void redisAddRead(void *privdata){
    redis_event_t *re = (redis_event_t *)privdata;
    re->e.read_fn = redisReadHandler; // 设置 Reactor 事件的读回调函数（关键！）
    // 调用事件更新函数，添加 EPOLLIN 事件（不删除）
    redisEventUpdate(privdata, EPOLLIN, 0);
}

// 从 Reactor 注销 Redis 连接的读事件监听
static void redisDelRead(void *privdata) {
    redis_event_t *re = (redis_event_t *)privdata;  // 获取桥接对象
    re->e.read_fn = 0;                              // 清空读回调函数（不再处理读事件）
    // 调用事件更新函数，删除 EPOLLIN 事件（标记 remove=1）
    redisEventUpdate(privdata, EPOLLIN, 1);
}

// 向 Reactor 注册 Redis 连接的写事件监听（逻辑与读事件类似）
static void redisAddWrite(void *privdata) {
    redis_event_t *re = (redis_event_t *)privdata;  // 获取桥接对象
    re->e.write_fn = redisWriteHandler;             // 设置 Reactor 事件的写回调函数
    // 调用事件更新函数，添加 EPOLLOUT 事件（不删除）
    redisEventUpdate(privdata, EPOLLOUT, 0);
}

// 从 Reactor 注销 Redis 连接的写事件监听（逻辑与读事件类似）
static void redisDelWrite(void *privdata) {
    redis_event_t *re = (redis_event_t *)privdata;  // 获取桥接对象
    re->e.write_fn = 0;                             // 清空写回调函数
    // 调用事件更新函数，删除 EPOLLOUT 事件（标记 remove=1）
    redisEventUpdate(privdata, EPOLLOUT, 1);
}

//从 Reactor 移除事件监听，并释放 redis_event_t 结构体的内存，避免资源泄漏
// 清理 Redis 连接的事件资源（当连接关闭时调用）
static void redisCleanup(void *privdata){
    redis_event_t *re = (redis_event_t *)privdata;
    reactor_t *r = re->e.r;

    del_event(r, &re->e);  // 从 Reactor 中删除该事件（避免后续事件触发）
    hi_free(re);           // 使用 hiredis 的内存释放函数释放桥接对象（对应 hi_malloc）
}

/*
将 hiredis 的异步上下文（redisAsyncContext）与自定义的 Reactor 实例绑定
，使得 hiredis 可以通过 Reactor 监听 Redis 连接的 IO 事件（读 / 写），
并在事件触发时调用对应的处理函数（redisReadHandler/redisWriteHandler）
*/
// 将 hiredis 异步上下文与 Reactor 绑定（关键集成函数）
// 参数说明：
// r: Reactor 实例（来自自定义的 reactor.h）
// ac: hiredis 的异步上下文（redisAsyncContext，由 hiredis 提供）
// 返回值：REDIS_OK（成功）或 REDIS_ERR（失败）
static int redisAttach(reactor_t *r,redisAsyncContext *ac){
    redisContext *c = &(ac->c);  //获取hiredis同步上下文
    redis_event_t *re;

    /* 检查是否已绑定：hiredis 异步上下文的 ev.data 应为空 */
    if (ac->ev.data != NULL)
        return REDIS_ERR;  // 已绑定过其他 Reactor，返回错误

    re = (redis_event_t *)hi_malloc(sizeof(*re));
    if (re == NULL) return REDIS_ERR;  // 内存分配失败，返回错误

    /* 初始化桥接对象 */
    re->ctx = ac;                  // 绑定 hiredis 异步上下文
    re->e.fd = c->fd;              // 设置 Reactor 事件的文件描述符（Redis 连接的套接字）
    re->e.r = r;                   // 绑定 Reactor 实例
    re->e.in = NULL;               // 不使用 Reactor 的输入缓冲区（hiredis 内部管理）
    re->e.out = NULL;              // 不使用 Reactor 的输出缓冲区（hiredis 内部管理）
    re->mask = 0;                  // 初始无事件监听

    /* 将事件操作函数注册到 hiredis 异步上下文 */
    // hiredis 的异步上下文需要通过 ev 结构体的函数指针与外部事件循环集成
    ac->ev.addRead = redisAddRead;   // 注册“添加读事件”函数（hiredis 内部会调用）
    ac->ev.delRead = redisDelRead;   // 注册“删除读事件”函数
    ac->ev.addWrite = redisAddWrite; // 注册“添加写事件”函数
    ac->ev.delWrite = redisDelWrite; // 注册“删除写事件”函数
    ac->ev.cleanup = redisCleanup;   // 注册“清理事件”函数
    ac->ev.data = re;                // 将桥接对象关联到 hiredis 上下文（后续回调通过此指针传递）

    return REDIS_OK;  // 绑定成功
}   

#endif