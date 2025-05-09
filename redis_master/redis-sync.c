// gcc redis-sync.c -o redis-sync -lhiredis
// ./redis-sync

#include <stdio.h>      // 标准输入输出（用于printf/scanf）
#include <stdlib.h>     // 标准库（用于exit等）
#include <string.h>     // 字符串操作（如strlen等，本例未显式使用，但hiredis可能依赖）
#include <hiredis/hiredis.h>  // hiredis库头文件（Redis客户端核心接口）

int main() {
    unsigned int j, isunix = 0;  // j未使用（保留可能的循环变量），isunix标记是否为Unix域套接字（本例用TCP，设为0）
    redisContext *c;             // Redis连接上下文（核心结构体，包含连接状态、错误信息等）
    redisReply *reply;           // Redis命令回复结构体（存储命令执行结果）
    const char *hostname = "127.0.0.1";  // Redis服务器主机名（本地回环地址）
    int port = 6379;              // Redis服务器端口（默认端口）

    struct timeval timeout = { 1, 500000 }; // 超时时间结构体（秒, 微秒）→ 1.5秒（1秒+500000微秒）

    c = redisConnectWithTimeout(hostname, port, timeout);
    /*
    redisConnectWithTimeout函数说明：
    - 参数：
      1. hostname：服务器地址（域名或IP，Unix域套接字时为路径，需配合isunix参数）
      2. port：端口号（Unix域套接字时该参数被忽略，设为0）
      3. timeout：struct timeval类型的超时时间
    - 返回值：
      redisContext指针：
      - 成功：非NULL，c->err为REDIS_OK（0）
      - 失败：NULL或c->err非0（错误信息在c->errstr）
    */
    if (c == NULL || c->err) {  // 连接失败的两种情况：上下文分配失败 或 连接过程出错
        if (c) {  // 若上下文存在（分配成功但连接失败）
            printf("Connection error: %s\n", c->errstr);  // 打印具体错误信息（如连接拒绝、超时等）
            redisFree(c);  // 释放连接上下文（必须调用，避免内存泄漏）
        } else {  // 上下文分配失败（内存不足等）
            printf("Connection error: can't allocate redis context\n");
        }
        exit(1);  // 终止程序（非0退出码表示错误）
    }   

    int roleid = 10001;  // 目标哈希表的键（格式：role:{roleid}，本例查询role:10001）
    // 执行redis命令，支持格式化字符串（类似printf的参数替换）
    reply = redisCommand(c, "hgetall role:%d", roleid);
    /*
    redisCommand函数说明：
    - 参数：
      1. c：redisContext连接上下文
      2. 命令字符串（支持%s/%d等格式占位符，后续参数为替换值）
      3. ...：可选的格式化参数（本例中为roleid）
    - 返回值：
      redisReply指针：
      - 成功：指向命令结果结构体
      - 失败：需通过reply->type判断错误类型（如REDIS_REPLY_ERROR）
    */
    if (reply->type != REDIS_REPLY_ARRAY) {  // hgetall正常返回REDIS_REPLY_ARRAY类型（键值对数组）
        printf("reply error: %s\n", reply->str);  // 非数组类型（如错误类型），打印错误信息
    } else {  // 正常返回数组（每个元素是键或值，交替出现）
        printf("reply:number of elements=%lu\n", reply->elements);  // 打印元素总数（键值对总数×2）
        for (size_t i = 0; i < reply->elements; i++) {  // 遍历所有元素
            // reply->element[i]是redisReply指针，指向第i个元素
            // 元素类型为REDIS_REPLY_STRING（hgetall的键和值都是字符串）
            printf("\t %lu : %s\n", i, reply->element[i]->str);  // 打印索引和字符串值
        }
    }

    freeReplyObject(reply);  // 释放redisReply结构体及内部资源（必须调用，否则内存泄漏）

    /* Disconnects and frees the context */
    redisFree(c);  // 关闭连接并释放redisContext结构体（必须调用，否则内存泄漏）

    return 0;
}