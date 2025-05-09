// gcc redis-test-async.c reactor.c chainbuffer/buffer.c -o redis-test-async \
  -lhiredis -lm -pthread
// ./redis-async-test 2000

// 包含hiredis异步客户端头文件（核心功能）
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
// 包含系统时间函数头文件（用于计算耗时）
#include <time.h>

// 包含自定义头文件：事件循环（reactor）和异步适配器
#include "reactor.h"
#include "adapter_async.h"

// 全局变量声明
static reactor_t *R;          // 自定义事件循环实例（管理I/O事件）
static int cnt = 0;           // 命令执行计数器（统计已完成的Redis操作）
static int before = 0;        // 记录操作开始时间（毫秒级时间戳）
static int num = 0;           // 需要执行的Redis命令总次数（由参数传入）

// 功能：获取当前毫秒级时间戳（基于单调时钟，避免系统时间调整影响）
int current_tick() {
    int t = 0;
    struct timespec ti;       // 高精度时间结构体（秒+纳秒）
    // 获取单调时钟时间（CLOCK_MONOTONIC：从系统启动开始计时，不可调）
    clock_gettime(CLOCK_MONOTONIC, &ti);
    // 转换为毫秒：秒*1000 + 纳秒/1e6（1秒=1e9纳秒 → 1毫秒=1e6纳秒）
    t = (int)ti.tv_sec * 1000;
    t += ti.tv_nsec / 1000000;
    return t;
}

// Redis命令执行回调函数（当INCR命令完成时触发）
// 参数说明：
// - redisAsyncContext *c：Redis异步上下文（连接句柄）
// - void *r：命令执行结果（redisReply类型指针）
// - void *privdata：用户自定义数据（通过redisAsyncCommand传递）
void getCallback(redisAsyncContext *c,void *r,void *privdata){
    redisReply *reply = r;
    if(reply == NULL) return;

    // 打印结果：privdata是传递的"count"字符串，reply->integer是INCR返回的计数值
    printf("argv[%s]: %lld\n", (char*)privdata, reply->integer);

    cnt++;  // 完成1次命令，计数器+1

    // 当所有命令执行完成时（cnt等于设定的总次数num）
    if (cnt == num) {
        // 计算总耗时（当前时间 - 开始时间）
        int used = current_tick() - before;
        // 输出统计结果：执行次数、耗时（毫秒）
        printf("after %d exec redis command, used %d ms\n", num, used);
        // 断开Redis异步连接（触发disconnectCallback）
        redisAsyncDisconnect(c);
    }
}

// Redis连接成功/失败回调函数（当连接状态变化时触发）
// 参数说明：
// - const redisAsyncContext *c：Redis异步上下文
// - int status：状态码（REDIS_OK表示成功，非0表示失败）
void connectCallback(const redisAsyncContext *c,int status){
    if (status != REDIS_OK) { 
        // 连接失败时打印错误信息，并停止事件循环
        printf("Error: %s\n", c->errstr);  // 输出错误详情
        stop_eventloop(R);                 // 停止自定义事件循环
        return;
    }
    // 连接成功提示
    printf("Connected...\n");
}

// Redis断开连接回调函数（当连接关闭时触发）
void disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) { 
        // 异常断开时打印错误信息，并停止事件循环
        printf("Error: %s\n", c->errstr);
        stop_eventloop(R);
        return;
    }

    // 正常断开提示，并停止事件循环（程序退出）
    printf("Disconnected...\n");
    stop_eventloop(R);
}

int main(int argc,char *argv[]){
    // 1. 初始化Redis异步连接（目标地址：127.0.0.1:6379）
    redisAsyncContext *c = redisAsyncConnect("127.0.0.1", 6379);
    if (c->err) {  // 检查连接初始化是否失败（如地址错误）
        // 打印错误信息并退出（注意：此处未释放c，可能存在资源泄漏）
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    // 2. 创建自定义事件循环实例（reactor模式，管理I/O事件）
    R = create_reactor();
    // 将Redis异步上下文附加到事件循环（实现事件驱动）
    redisAttach(R, c);  // 内部可能将Redis的socket注册到reactor的事件监听中

    // 3. 设置Redis连接相关回调函数
    redisAsyncSetConnectCallback(c, connectCallback);   // 连接状态回调
    redisAsyncSetDisconnectCallback(c, disconnectCallback); // 断开状态回调

    // 4. 记录操作开始时间（用于后续统计耗时）
    before = current_tick();
    // 确定要执行的Redis命令次数：从命令行参数获取（默认1000次）
    num = (argc > 1) ? atoi(argv[1]) : 1000;

    // 5. 批量发送异步Redis命令（INCR counter：原子递增计数器）
    for (int i = 0; i < num; i++) {
        // 发送异步命令：
        // - c：Redis异步上下文
        // - getCallback：命令完成时的回调函数
        // - "count"：传递给回调函数的自定义数据（privdata）
        // - "INCR counter"：具体的Redis命令（递增名为counter的键）
        redisAsyncCommand(c, getCallback, "count", "INCR counter");
    }

    // 6. 启动事件循环（开始处理I/O事件，直到所有操作完成）
    eventloop(R);

    // 7. 释放资源（事件循环实例）
    release_reactor(R);

    return 0;
}