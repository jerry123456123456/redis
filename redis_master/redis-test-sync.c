// gcc -o redis-test-sync redis-test-sync.c -lhiredis

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <hiredis/hiredis.h>

// 获取当前时间戳（毫秒级，基于单调时钟，适合测量耗时）
int current_tick() {
    int t = 0;
    struct timespec ti;
	clock_gettime(CLOCK_MONOTONIC, &ti);
	t = (int)ti.tv_sec * 1000;
	t += ti.tv_nsec / 1000000;
    return t;
}

int main(int argc,char *argv[]){
    redisContext *c;  // Redis连接上下文（核心结构体，管理连接状态）
    redisReply *reply; // Redis命令回复对象（存储服务器返回结果）
    const char *hostname = "127.0.0.1";

    int port = 6379;

    // 连接超时时间（struct timeval：秒+微秒）
    // 此处设置为1.5秒（1秒 + 500000微秒）
    struct timeval timeout = { 1, 500000 }; 

    // 连接到Redis服务器（带超时设置）
    // 参数：主机名、端口、超时时间
    c = redisConnectWithTimeout(hostname, port, timeout);
    // 检查连接是否失败
    if (c == NULL || c->err) { 
        if (c) { // 若上下文存在但有错误
            // 打印错误信息（如连接拒绝、超时等）
            printf("Connection error: %s\n", c->errstr); 
            redisFree(c); // 释放Redis上下文（避免内存泄漏）
        } else { // 上下文分配失败（内存不足）
            printf("Connection error: can't allocate redis context\n");
        }
        exit(1); // 终止程序（错误码1表示异常）
    }

    // 获取命令行参数中的执行次数（默认1000次）
    // argc：参数个数，argv：参数数组（argv[1]为第一个用户参数）
    int num = (argc > 1) ? atoi(argv[1]) : 1000;

    //记录开始时间
    int before = current_tick();

    // 循环执行Redis命令（INCR counter）
    for (int i=0; i<num; i++) {
        // 发送同步命令并获取回复（阻塞直到服务器响应）
        // 参数：连接上下文、命令字符串（支持可变参数，类似printf）
        reply = redisCommand(c,"INCR counter"); 
        // 打印命令执行结果（INCR返回递增后的整数值）
        printf("INCR counter: %lld\n", reply->integer); 
        // 释放回复对象（hiredis要求手动释放，否则内存泄漏）
        freeReplyObject(reply); 
    }

    // 计算总耗时（当前时间 - 开始时间）
    int used = current_tick()-before; 

    // 打印性能统计结果
    printf("after %d exec redis command, used %d ms\n", num, used); 

    /* 断开连接并释放上下文 */
    redisFree(c); // 释放Redis连接资源（必须调用，否则内存泄漏）

    return 0;
}