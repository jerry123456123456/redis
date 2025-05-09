// gcc -o redis-async redis-async.c reactor.c chainbuffer/buffer.c -lhiredis -lm -pthread

// 

// 引入 hiredis 同步库（基础操作）、异步库（非阻塞IO）、sds字符串处理库（高效动态字符串）
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/sds.h>

// 引入自定义的事件循环框架（reactor模型）和异步适配器（对接hiredis事件）
#include "reactor.h"
#include "adapter_async.h"

// 全局变量：指向事件循环实例的指针（用于控制事件循环的启动/停止）
static reactor_t *R;

// 将 redisReply 类型枚举（REDIS_REPLY_*）映射为可读字符串，方便日志输出
char *rtype[] = {
    "^o^",       // 占位（枚举从0开始，但REDIS_REPLY_NONE=0无实际意义）
    "STRING",    // REDIS_REPLY_STRING（字符串）
    "ARRAY",     // REDIS_REPLY_ARRAY（数组）
    "INTEGER",   // REDIS_REPLY_INTEGER（整数）
    "NIL",       // REDIS_REPLY_NIL（空值）
    "STATUS",    // REDIS_REPLY_STATUS（状态消息）
    "ERROR",     // REDIS_REPLY_ERROR（错误）
    "DOUBLE",    // REDIS_REPLY_DOUBLE（双精度浮点数）
    "BOOL",      // REDIS_REPLY_BOOL（布尔值）
    "MAP",       // REDIS_REPLY_MAP（映射）
    "SET",       // REDIS_REPLY_SET（集合）
    "ATTR",      // REDIS_REPLY_ATTR（属性）
    "PUSH",      // REDIS_REPLY_PUSH（推送消息）
    "BIGNUM",    // REDIS_REPLY_BIGNUM（大整数）
    "VERB",      // REDIS_REPLY_VERB（命令）
};

// 功能：异步执行Redis命令后，处理服务端返回的响应
// 参数：
//   c: 异步Redis上下文（包含连接状态、错误信息等）
//   r: Redis响应结构体（redisReply* 类型，保存具体数据）
//   privdata: 用户自定义数据（通过redisAsyncCommand的第三个参数传递）
void dumpReply(struct redisAsyncContext *c, void *r, void *privdata) {
    // 将响应转换为redisReply结构体指针（hiredis定义的响应数据结构）
    redisReply *reply = (redisReply*)r;

    // 根据响应类型（reply->type）分支处理
    switch (reply->type) {
        // 状态消息（如"OK"）或字符串类型
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_STRING:
            // 打印：请求标识（privdata）、响应类型、具体内容（reply->str）
            printf("[req = %s]reply:(%s)%s\n", (char*)privdata, rtype[reply->type], reply->str);
            break;

        // 空值（如查询不存在的键）
        case REDIS_REPLY_NIL:
            printf("[req = %s]reply:(%s)nil\n", (char*)privdata, rtype[reply->type]);
            break;

        // 整数类型（如INCR命令结果）
        case REDIS_REPLY_INTEGER:
            // 注意：redisReply->integer是long long类型（支持大整数）
            printf("[req = %s]reply:(%s)%lld\n", (char*)privdata, rtype[reply->type], reply->integer);
            break;

        // 数组类型（如HGETALL返回的键值对列表）
        case REDIS_REPLY_ARRAY:
            // 打印数组元素数量（reply->elements）
            printf("[req = %s]reply(%s):number of elements=%lu\n", (char*)privdata, rtype[reply->type], reply->elements);
            // 遍历数组元素（reply->element是redisReply*数组）
            for (size_t i = 0; i < reply->elements; i++) {
                // 打印每个元素的字符串内容（假设元素是字符串类型）
                printf("\t %lu : %s\n", i, reply->element[i]->str);
            }
            break;

        // 错误类型（如命令语法错误）
        case REDIS_REPLY_ERROR:
            printf("[req = %s]reply(%s):err=%s\n", (char*)privdata, rtype[reply->type], reply->str);
            break;

        // 其他未显式处理的类型（如DOUBLE、BOOL等）
        default:
            printf("[req = %s]reply(%s)\n", (char*)privdata, rtype[reply->type]);
            break;
    }
}

// 功能：当与Redis服务器建立连接（或连接失败）时触发
// 参数：
//   c: 异步Redis上下文（只读，因连接状态已确定）
//   status: 连接状态（REDIS_OK=成功，非0=失败）
void connectCallback(const redisAsyncContext *c, int status) {
    // 连接失败处理
    if (status != REDIS_OK) {
        // 打印错误信息（c->errstr保存具体错误描述）
        printf("Error: %s\n", c->errstr);
        // 停止事件循环（避免无效的事件监听）
        stop_eventloop(R);
        return;
    }

    // 连接成功提示
    printf("Connected...\n");

    // 发送异步命令：HSET（设置哈希表字段）
    // 参数说明：
    //   (redisAsyncContext *)c: 异步上下文（需转换为可写指针）
    //   dumpReply: 响应回调函数（处理该命令的返回结果）
    //   "hmset role:10001": 用户自定义数据（传递给回调函数的privdata参数）
    //   "hmset role:10001 name mark age 31 sex male": 具体执行的Redis命令
    redisAsyncCommand((redisAsyncContext *)c, dumpReply, 
        "hmset role:10001", 
        "hmset role:10001 name mark age 31 sex male");

    // 发送另一个异步命令：HGETALL（获取哈希表所有字段）
    // 注意：命令会被排队，待前一个命令发送完成后执行（hiredis内部维护命令队列）
    redisAsyncCommand((redisAsyncContext *)c, dumpReply, "hgetall role:10001", "hgetall role:10001");
    // ....（可继续添加其他异步命令）
}

// 功能：当与Redis服务器断开连接（正常或异常）时触发
// 参数同上（connectCallback）
void disconnectCallback(const redisAsyncContext *c, int status) {
    // 异常断开处理
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        stop_eventloop(R);
        return;
    }

    // 正常断开提示，并停止事件循环
    printf("Disconnected...\n");
    stop_eventloop(R);
}

int main(int argc, char **argv) {
    // 1. 创建事件循环实例（自定义reactor框架，负责管理IO事件）
    R = create_reactor();

    // 2. 异步连接Redis服务器（非阻塞操作，立即返回）
    // 参数：Redis服务器地址（"127.0.0.1"）和端口（6379）
    redisAsyncContext *c = redisAsyncConnect("127.0.0.1", 6379);
    // 检查连接初始化错误（如地址无效）
    if (c->err) {
        // 注意：redisAsyncConnect可能在初始化时直接报错（如无法解析地址）
        printf("Error: %s\n", c->errstr);
        return 1;  // 程序退出（连接未建立）
    }

    // 3. 将Redis异步上下文附加到事件循环（关键操作！）
    // 作用：将hiredis内部的IO事件（读/写）注册到自定义reactor中，由reactor驱动事件循环
    redisAttach(R, c);

    // 4. 设置连接状态回调函数（连接成功/失败时触发）
    redisAsyncSetConnectCallback(c, connectCallback);
    // 设置断开连接回调函数（连接断开时触发）
    redisAsyncSetDisconnectCallback(c, disconnectCallback);

    // 5. 启动事件循环（开始监听IO事件，驱动hiredis异步操作）
    eventloop(R);

    // 6. 清理资源（事件循环结束后释放reactor实例）
    release_reactor(R);

    return 0;
}

