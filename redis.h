#ifndef __REDIS_H_
#define __REDIS_H_

#include "fmacros.h"
#include "dict.h"
#include "adlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <syslog.h>
#include <netinet/in.h>
#include <signal.h>
#include "anet.h"
#include "ae.h"
#include "sds.h"
#include "zmalloc.h"

/* Error codes */
#define REDIS_OK                0
#define REDIS_ERR                -1

/* Objects encoding. Some kind of objects like Strings and Hashes can be
 * internally represented in multiple ways. The 'encoding' field of the object
 * is set to one of this fields for this object. */
// 对象编码
#define REDIS_ENCODING_RAW 0     /* Raw representation */

/* 对象类型 */
#define REDIS_STRING 0
#define REDIS_LIST 1
#define REDIS_SET 2
#define REDIS_ZSET 3
#define REDIS_HASH 4

/* Protocol and I/O related defines */
#define REDIS_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */
#define REDIS_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define REDIS_INLINE_MAX_SIZE   (1024*64) /* Max size of inline reads */
#define REDIS_MBULK_BIG_ARG     (1024*32)
#define REDIS_MIN_RESERVED_FDS 32
#define REDIS_EVENTLOOP_FDSET_INCR (REDIS_MIN_RESERVED_FDS+96) //eventloop中fd增量
#define REDIS_MAX_CLIENTS 10000 /* 最大所支持的用户数目 */

/* Static server configuration */
#define REDIS_DEFAULT_HZ        10   /* 服务器默认运行频率 */
#define REDIS_SERVERPORT        6379 /* TCP port */
#define REDIS_TCP_BACKLOG       511  // TCP连接队列的大小，该队列用来存放待处理的请求，当服务器处理请求之后，将其从队列中移除，Linux系统中通过net/core/somaxconn参选来限制，Linux默认是128，如果队列满了，则后续的请求会被直接丢弃，这种情况下，会限制redis的性能发挥，建议修改为2048，该参数就是redis的backlog，最终全连接队列的大小由backlog和somaxconn两个值中最小的决定，所以要修改TCP最终全连接队列的大小的话，得同时修改这两个值才能起作用，关于TCP的全连接和半连接队列请参考:https://juejin.im/post/6844904071367753736#heading-10
#define REDIS_BINDADDR_MAX        16 //绑定地址的最大数量
#define REDIS_IP_STR_LEN INET6_ADDRSTRLEN
#define REDIS_DEFAULT_DBNUM     16 //默认支持的数据库数量
#define REDIS_DEFAULT_TCP_KEEPALIVE 0 //TCP保活检测，60代表server端每60秒发起一次ack请求来检查client是否挂掉，对于无响应的client会关闭其连接，如果设置为0，则不会进行保活检测

/* Client request types */
#define REDIS_REQ_INLINE    1
#define REDIS_REQ_MULTIBULK 2 /* 多条查询 */

/* 对象编码 */
#define REDIS_ENCODING_RAW 0     /* Raw representation */
#define REDIS_ENCODING_INT 1     /* Encoded as integer */
#define REDIS_ENCODING_EMBSTR 8  /* Embedded sds string encoding */

/* 命令标志 */
#define REDIS_CMD_WRITE 1                   /* "w" flag */
#define REDIS_CMD_READONLY 2                /* "r" flag */
#define REDIS_CMD_DENYOOM 4                 /* "m" flag */
#define REDIS_CMD_NOT_USED_1 8              /* no longer used flag */
#define REDIS_CMD_ADMIN 16                  /* "a" flag */
#define REDIS_CMD_PUBSUB 32                 /* "p" flag */
#define REDIS_CMD_NOSCRIPT  64              /* "s" flag */
#define REDIS_CMD_RANDOM 128                /* "R" flag */
#define REDIS_CMD_SORT_FOR_SCRIPT 256       /* "S" flag */
#define REDIS_CMD_LOADING 512               /* "l" flag */
#define REDIS_CMD_STALE 1024                /* "t" flag */
#define REDIS_CMD_SKIP_MONITOR 2048         /* "M" flag */
#define REDIS_CMD_ASKING 4096               /* "k" flag */

/* Command call flags, see call() function */
#define REDIS_CALL_NONE 0
#define REDIS_CALL_SLOWLOG 1
#define REDIS_CALL_STATS 2
#define REDIS_CALL_PROPAGATE 4
#define REDIS_CALL_FULL (REDIS_CALL_SLOWLOG | REDIS_CALL_STATS | REDIS_CALL_PROPAGATE)

#define REDIS_LRU_BITS 24

//
// redisObject Redis对象
// 
typedef struct redisObject {

    unsigned type: 4; // 类型

    unsigned encoding: 4; // 编码

    unsigned lru: REDIS_LRU_BITS; // 对象最后一次被访问的时间

    int refcount; // 引用计数

    void *ptr; // 指向实际值的指针
} robj;


typedef struct redisDb {

    dict *dict;                 // 数据库键空间，保存着数据库中的所有键值对

    dict *expires;              // 键的过期时间，字典的键为键，字典的值为过期事件 UNIX 时间戳

    dict *blocking_keys;        // 正处于阻塞状态的键

    dict *ready_keys;           // 可以解除阻塞的键

    int id;                     // 数据库号码
} redisDb;

/*
 * 因为 I/O 复用的缘故，需要为每个客户端维持一个状态。
 *
 * 多个客户端状态被服务器用链表连接起来。
 */
typedef struct redisClient {
    int fd; //  套接字描述符

    redisDb *db; // 当前正在使用的数据库

    int dictid; //  当前正在使用的数据库的 id （号码）

    robj *name; // 客户端的名字

    sds querybuf; // 查询缓冲区

    size_t querybuf_peak; // 查询缓冲区长度峰值

    int argc; // 参数数量

    robj **argv; // 参数对象数组

    struct redisCommand *cmd, *lastcmd; // 记录被客户端执行的命令

    int reqtype; // 请求的类型,是内联命令还是多条命令

    int multibulklen; // 剩余未读取的命令内容数量

    long bulklen; // 命令内容的长度

    list *reply; // 回复链表

    unsigned long reply_bytes; // 回复链表中对象的总大小

    int bufpos; // 回复偏移量

    char buf[REDIS_REPLY_CHUNK_BYTES];

} redisClient;


struct redisServer {

    /* General */
    char *configfile;   // 配置文件的绝对路径,要么就是NULL

    int hz;             // serverCron() 每秒调用的次数

    redisDb *db;        // 一个数组,保存着服务器中所有的数据库

    dict *commands;     // 命令表（受到 rename 配置选项的作用）

    dict *orig_commands;        // 重命名之前的命令表

    aeEventLoop *el; // 事件状态

    int shutdown_asap; // 关闭服务器的标识

    int port;                    // TCP 监听端口
    int tcp_backlog;            // TCP listen() backlog
    char *bindaddr[REDIS_BINDADDR_MAX]; // ip地址
    int bindaddr_count; // 绑定的地址数量

    int ipfd[REDIS_BINDADDR_MAX];  // TCP 描述符
    int ipfd_count;                   // 已经使用了的描述符的数目

    list *clients;    // 一个链表,保存了所有的客户端状态结构

    list *clients_to_close; // 链表,保存了所有待关闭的客户端

    redisClient *current_client; // 服务器当前服务的客户端,仅用于崩溃报告

    char neterr[ANET_ERR_LEN]; // 用于记录网络错误

    int tcpkeepalive;    // 是否开启 SO_KEEPALIVE选项
    int dbnum;            // 数据库的总数目

    /* Limits */
    int maxclients;      // Max number of simultaneous clients
};


typedef void redisCommandProc(redisClient *c);

typedef int *redisGetKeysProc(struct redisCommand *cmd, robj **argv, int argc, int *numkeys);

//
// redisCommand Redis 命令
//
struct redisCommand {

    // 命令名字
    char *name;

    // 实现函数
    redisCommandProc *proc;

    // 参数个数
    int arity;

    // 字符串表示的 FLAG
    char *sflags; /* Flags as string representation, one char per flag. */

    // 实际 FLAG
    int flags;    /* The actual flags, obtained from the 'sflags' field. */

    // 从命令中判断命令的键参数。在 Redis 集群转向时使用。
    redisGetKeysProc *getkeys_proc;

    /* What keys should be loaded in background when calling this command? */
    // 指定哪些参数是 key
    int firstkey; /* The first argument that's a key (0 = no keys) */
    int lastkey;  /* The last argument that's a key */
    int keystep;  /* The step between first and last key */

    // 统计信息
    // microseconds 记录了命令执行耗费的总毫微秒数
    // calls 是命令被执行的总次数
    long long microseconds, calls;
};

/* api */
int processCommand(redisClient *c);

#endif