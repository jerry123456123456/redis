//gcc -o redis-async \
    redis-cmd-async.c reactor.c chainbuffer/buffer.c \
    -lhiredis -lm -pthread 

// 引入 hiredis 核心库（同步操作）、异步库（非阻塞IO）、sds动态字符串库（高效字符串处理）
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/sds.h>

// 引入自定义的事件循环框架（reactor模型，管理IO事件）和异步适配器（对接hiredis事件）
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
    "STATUS",    // REDIS_REPLY_STATUS（状态消息，如"OK"）
    "ERROR",     // REDIS_REPLY_ERROR（错误消息）
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
//   privdata: 用户自定义数据（通过redisAsyncCommand的第三个参数传递，用于标识请求）
void dumpReply(struct redisAsyncContext *c, void *r, void *privdata) {
    // 将响应转换为redisReply结构体指针（hiredis定义的响应数据结构）
    redisReply *reply = (redisReply*)r;

    // 根据响应类型（reply->type）分支处理
    switch (reply->type) {
        // 状态消息（如"OK"）或字符串类型（如GET命令结果）
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_STRING:
            // 打印：请求标识（privdata）、响应类型、具体内容（reply->str）
            printf("[req = %s]reply:(%s)%s\n", (char*)privdata, rtype[reply->type], reply->str);
            break;

        // 空值（如查询不存在的键）
        case REDIS_REPLY_NIL:
            printf("[req = %s]reply:(%s)nil\n", (char*)privdata, rtype[reply->type]);
            break;

        // 整数类型（如INCR命令结果、LPUSH返回的元素个数）
        case REDIS_REPLY_INTEGER:
            // 注意：redisReply->integer是long long类型（支持大整数）
            printf("[req = %s]reply:(%s)%lld\n", (char*)privdata, rtype[reply->type], reply->integer);
            break;

        // 数组类型（如HGETALL返回的键值对列表、LRANGE返回的列表）
        case REDIS_REPLY_ARRAY:
            // 打印数组元素数量（reply->elements）
            printf("[req = %s]reply(%s):number of elements=%lu\n", (char*)privdata, rtype[reply->type], reply->elements);
            // 遍历数组元素（reply->element是redisReply*数组）
            for (size_t i = 0; i < reply->elements; i++) {
                // 打印每个元素的字符串内容（假设元素是字符串类型）
                printf("\t %lu : %s\n", i, reply->element[i]->str);
            }
            break;

        // 错误类型（如命令语法错误、操作不支持）
        case REDIS_REPLY_ERROR:
            printf("[req = %s]reply(%s):err=%s\n", (char*)privdata, rtype[reply->type], reply->str);
            break;

        // 其他未显式处理的类型（如DOUBLE、BOOL等）
        default:
            printf("[req = %s]reply(%s)\n", (char*)privdata, rtype[reply->type]);
            break;
    }
}

void test_string_cmd(redisAsyncContext *c) {
    printf("===============> begin test string\n");
    // 发送SET命令（设置字符串键）
    redisAsyncCommand(c, dumpReply, "set mark 1000", "set mark 1000");
    // 发送GET命令（获取字符串键的值）
    redisAsyncCommand(c, dumpReply, "get mark", "get mark");
    // 发送DEL命令（删除字符串键）
    redisAsyncCommand(c, dumpReply, "del mark", "del mark");
}

void test_list_cmd(redisAsyncContext *c) {
    printf("===============> begin test list\n");
    // 发送LPUSH命令（向左插入列表元素）
    // 注意：命令参数使用格式化字符串（"darren", "mark", "1000"）
    redisAsyncCommand(c, dumpReply, "lpush list", "lpush list %s %s %s", "darren", "mark", "1000");
    // 发送LPOP命令（向左弹出列表元素）
    redisAsyncCommand(c, dumpReply, "lpop list", "lpop list");
    // 发送LRANGE命令（获取列表所有元素）
    redisAsyncCommand(c, dumpReply, "lrange list 0 -1", "lrange list 0 -1");
}

void test_hash_cmd(redisAsyncContext *c) {
    printf("===============> begin test hash\n");
    // 发送HMSET命令（设置哈希字段）
    redisAsyncCommand(c, dumpReply, "hmset role:10001", "hmset role:10001 name mark age 31 sex male");
    // 发送HGETALL命令（获取哈希所有字段和值）
    redisAsyncCommand(c, dumpReply, "hgetall role:10001", "hgetall role:10001");
    // 发送HSET命令（更新哈希字段）
    redisAsyncCommand(c, dumpReply, "hset role:10001 age 32","hset role:10001 age 32");
    // 再次发送HGETALL验证更新结果
    redisAsyncCommand(c, dumpReply, "hgetall role:10001", "hgetall role:10001");
}

void test_set_cmd(redisAsyncContext *c) {
    printf("===============> begin test set\n");
    // 发送SADD命令（向集合添加元素）
    redisAsyncCommand(c, dumpReply, "sadd teachers", "sadd teachers mark darren king");
    // 发送SMEMBERS命令（获取集合所有元素）
    redisAsyncCommand(c, dumpReply, "smembers teachers", "smembers teachers");
    // 发送SPOP命令（随机弹出集合元素）
    redisAsyncCommand(c, dumpReply, "spop teachers", "spop teachers");
    // 再次发送SMEMBERS验证弹出结果
    redisAsyncCommand(c, dumpReply, "smembers teachers", "smembers teachers");
}

void test_zset_cmd(redisAsyncContext *c) {
    printf("===============> begin test zset\n");
    // 发送ZADD命令（向有序集合添加带分数的元素）
    redisAsyncCommand(c, dumpReply, "zadd ranks", "zadd ranks 80 mark 90 darren 100 king");
    // 发送ZRANGE命令（按顺序获取有序集合元素）
    redisAsyncCommand(c, dumpReply, "zrange ranks", "zrange ranks 0 -1");
    // 发送ZINCRBY命令（增加指定元素的分数）
    redisAsyncCommand(c, dumpReply, "zincrby ranks", "zincrby ranks 11 mark");
    // 再次发送ZRANGE验证分数更新
    redisAsyncCommand(c, dumpReply, "zrange ranks", "zrange ranks 0 -1");
}

void test_lua_cmd(redisAsyncContext *c) {
    printf("===============> begin test lua\n");
    // 先设置初始值（score=2）
    redisAsyncCommand(c, dumpReply, "set score 2", "set score 2");

    // 使用sds动态字符串构建Lua脚本（hiredis推荐的字符串处理方式）
    sds dval = sdsnew("local val = redis.call('get', 'score');if val then redis.call('set', 'score', 2*val); return 2*val; end;return 0;");
    
    // 组装Lua脚本的参数（对应eval命令的参数格式）
    char *doubleValueScriptArgv[] = {
        (char*)"eval",        // 固定为"eval"命令
        dval,                 // Lua脚本内容
        (char*)"1",           // 键的数量（numkeys）
        (char*)"score",       // 具体的键名（对应脚本中的KEYS[1]）
    };

    // 发送4次异步命令（测试Lua脚本的幂等性）
    for (int i = 0; i < 4; i++) {
        // 使用redisAsyncCommandArgv发送带参数的命令（更灵活的参数管理）
        redisAsyncCommandArgv(c, dumpReply, "eval script double value", 
            4, (const char **)doubleValueScriptArgv, NULL);
    }

    // 释放sds动态字符串（避免内存泄漏）
    sdsfree(dval);
}

// 功能：Redis连接成功或失败时的回调函数
// 参数：
//   c: 异步Redis上下文（只读，连接状态已确定）
//   status: 连接状态（REDIS_OK=成功，非0=失败）
void connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {  // 连接失败
        printf("Error: %s\n", c->errstr);  // 打印错误信息（如连接超时、地址错误）
        stop_eventloop(R);  // 停止事件循环（避免无效等待）
        return;
    }
    // 连接成功，开始测试（当前注释了其他测试，仅测试Lua脚本）
    printf("Connected...\n");
    // test_string_cmd((redisAsyncContext *)c);   // 测试字符串命令（可取消注释）
    // test_list_cmd((redisAsyncContext *)c);     // 测试列表命令（可取消注释）
    // test_hash_cmd((redisAsyncContext *)c);     // 测试哈希命令（可取消注释）
    // test_set_cmd((redisAsyncContext *)c);      // 测试集合命令（可取消注释）
    // test_zset_cmd((redisAsyncContext *)c);     // 测试有序集合命令（可取消注释）
    test_lua_cmd((redisAsyncContext *)c);         // 测试Lua脚本（当前启用）
}

// 功能：Redis断开连接时的回调函数（正常或异常断开）
void disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {  // 异常断开
        printf("Error: %s\n", c->errstr);
        stop_eventloop(R);
        return;
    }
    // 正常断开连接，停止事件循环
    printf("Disconnected...\n");
    stop_eventloop(R);
}

int main(int argc, char **argv) {
    // 1. 初始化自定义事件循环（reactor框架，管理IO事件）
    R = create_reactor();

    // 2. 异步连接Redis服务器（非阻塞操作，立即返回上下文）
    // 参数：Redis服务器地址（"127.0.0.1"）和端口（6379）
    redisAsyncContext *c = redisAsyncConnect("127.0.0.1", 6379);
    if (c->err) {  // 连接初始化失败（如地址无法解析）
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    // 3. 将hiredis异步上下文附加到自定义事件循环（关键操作！）
    // 作用：将hiredis内部的IO事件（读/写）注册到reactor，由reactor驱动事件循环
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
