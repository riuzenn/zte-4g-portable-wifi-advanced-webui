#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
// 定义宏：只适用于字符串常量，不能用于变量
#define WRITE_STR(fd, s) write(fd, s, sizeof(s) - 1)

// 重写libatutils.so的send_app_req，不再打印'send_app_req'
extern int ipc_send_message(key_t srcid, key_t targetid, unsigned int cmd, size_t data_len, const void *data, int ipc_nowait);
__attribute__((visibility("default")))
// send_req_and_wait的no_retry传给send_app_req后以之为判断条件得到命令字传给ipc_send_message，重命名为cmd，在两个函数里的作用不同。在zte_mifi里用作响应超时时间，这里只是个没用到的透传参数
void send_app_req(key_t srcid, const char *at_ptr, size_t at_len, unsigned int timeout_sec, int no_retry)
{
    char ipc_req[0xda8] = {0};// 0xda8 = 3496
    memcpy(ipc_req, at_ptr, at_len);
    // zte_mifi从at偏移0xda0/0xda4处读取at_len和timeout_sec
    memcpy(ipc_req + 0xda0, &at_len, sizeof(at_len));
    // 原版逻辑会复制timeout_sec，不过前面创建at字符数组清零了，后面send_req_and_wait传0，所以没必要
    (void)timeout_sec;
//    memcpy(ipc_req + 0xda4, &timeout_sec, sizeof(timeout_sec));
    // 后面send_req_and_wait的no_retry传1，不用判断了
    (void)no_retry;
    ipc_send_message(srcid, 0x100f, 0x152f, 0xda8, ipc_req, 0);
//    ipc_send_message(srcid, 0x100f, (no_retry == 1) ? 0x152f : 0x1530, 0xda8, ipc_req, 0);
}

// 重写libatutils.so的send_req_and_wait，常量值都来自原函数，精简逻辑，去除多余日志
// alarm超时标志
static volatile int g_timed_out = 0;
static void alarm_handler(int sig)
{
    (void)sig;
    g_timed_out = 1;
}

// 临时消息队列动态地址srcid范围（与modem服务端约定）
#define AT_MSG_KEY_START ((key_t)0x1132)
#define AT_MSG_KEY_END ((key_t)0x1233)
#define AT_MSG_FLAG ((int)0x780) // IPC_CREAT | IPC_EXCL | 0600, msgget获取消息队列的标志位组合

// modem服务端回复at请求的命令字
#define MSG_CMD_SEND_AT_MSG_RSP ((short)0x1531)

// at回复结构体和纯at回复的缓冲区大小
#define MSG_DATA_SIZE ((size_t)0xdbc) // msgrcv接收的整个回复结构体长度3516
#define RSP_MAX_LEN ((size_t)3504) // 回复结构体中data[]长度

/*
 * modem服务端发来的at请求回复的结构体
 * msgrcv从targetid字段开始写入，最大MSG_DATA_SIZE字节
 */
struct at_ipc_msg {
    long  mtype;        // msgrcv内部固定为1
    key_t targetid;     // 0x100f, 服务端固定队列
    key_t srcid;
    short cmd;          // 0x1531, 回复at请求的命令字
    short len;          // ipc_send_message的第四个参数，客户端请求数据的长度，固定为0xda8
    char  sep[4];       // "::::"分隔符
    char  data[RSP_MAX_LEN];
};

/*
 * 发送AT命令并等待响应。
 * 
 * 参数（固定调用方式）：
 *   at_ptr      — AT命令字符串（已含 \r\n）
 *   at_len      — AT命令长度
 *   info_fmt    — 固定 "%s"，本实现忽略，直接返回完整原始响应
 *   res_ptr_ptr — 指向用户缓冲区的指针（char **），用于输出响应
 *   safe_parse  — 固定 0，未使用
 *   timeout_sec — 固定 0，透传给send_app_req
 *   no_retry    — 固定 1，send_app_req据此选择0x152f
 * 
 * 返回：
 *    0 — 成功（响应中包含 OK）
 *   >0 — 错误码（从 ERROR 响应中解析）
 *   -1 — 内部错误（消息队列获取失败、参数非法等）
 */
 __attribute__((visibility("default")))
int send_req_and_wait(const char *at_ptr, int at_len, const char *info_fmt, char **res_ptr_ptr, int safe_parse, unsigned int timeout_sec, int no_retry)
{
    int msqid = -1;//消息队列标识符
    key_t srcid;
    struct at_ipc_msg msg;
    int ret;
    char *err_ptr;
    int err_code = -1;
    int result = -1;

    (void)info_fmt;
    (void)safe_parse;
    (void)no_retry;

    /* ---------- 1. 参数校验 ---------- */
    if (at_ptr == NULL || res_ptr_ptr == NULL || *res_ptr_ptr == NULL)
        goto cleanup;
    if (at_len <= 0 || at_len >= 0xda0)
        goto cleanup;

    /* ---------- 2. 获取临时消息队列 ---------- */
    for (srcid = AT_MSG_KEY_START; srcid < AT_MSG_KEY_END; srcid++) {
        msqid = msgget(srcid, AT_MSG_FLAG);
        if (msqid != -1)
            break;
    }
    if (msqid == -1)
        goto cleanup;

    /* ---------- 3. 发送 AT 请求 ---------- */
    send_app_req(srcid, at_ptr, at_len, timeout_sec, no_retry);

    /* ----- 4. 接收响应（alarm3秒总超时） ----- */
    signal(SIGALRM, alarm_handler); // 注册：收到 SIGALRM 时执行 alarm_handler
    g_timed_out = 0; // 清零：每次发送前重置状态
    alarm(3); // 启动：内核在 3 秒后向本进程发送 SIGALRM
    
    do {
        memset(&msg, 0, sizeof(msg));
        ret = msgrcv(msqid, &msg, MSG_DATA_SIZE, 0, 0);
            if (ret < 0) {
            if (errno == EINTR && g_timed_out)
                break; // 超时被alarm信号中断
            if (errno != EINTR)
                break; // msgrcv遇到真正的致命错误（队列被删、权限丢失等）
        }
    } while (ret < 0 || msg.cmd != MSG_CMD_SEND_AT_MSG_RSP);
    
    alarm(0); // 取消未触发的闹钟（如果提前收到了响应）
    signal(SIGALRM, SIG_DFL); // 恢复SIGALRM的默认处理（终止进程）
    
    if (ret < 0) {
        result = -1; // msgrcv出问题
        goto cleanup;
    }

    /* ---------- 5. 复制完整响应到用户缓冲区 ---------- */
    /* 直接复制msg.data的完整内容。*/
    snprintf(*res_ptr_ptr, RSP_MAX_LEN, "%s", msg.data);

    /* ---------- 6. 判断响应状态 ---------- */
    err_ptr = strstr(msg.data, "ERROR");
    if (err_ptr != NULL) {
        /* 格式通常为"ERROR: <code>"，
         * 跳过"ERROR"（5 字节）和随后的2字节分隔符读取数字 */
        sscanf(err_ptr + 7, "%d", &err_code);
        result = err_code;// 有数字码则返回数字码，无则返回-1
        goto cleanup;
    }

    if (strstr(msg.data, "OK") != NULL) {
        result = 0;
        goto cleanup;
    }

    /* 非 OK 非 ERROR（理论上不会发生） */
    result = -1;

cleanup:
    if (msqid != -1)
        msgctl(msqid, IPC_RMID, NULL); // 销毁申请到的消息队列
    return result;
}

// 解析res[]，得到格式化输出
static int extract_respond(char *res_ptr, size_t res_ptr_len)
{
    // 成功的res回复格式为\r\n+命令大写: 返回值\r\n\r\nOK\r\n
    char *colon = strstr(res_ptr, ": ");
    // send_req_and_wait和main函数确保到这里都是成功得到modem的回复且回复里有OK
    if (!colon) return 1; // 错误码1代表非查询类的at命令成功执行, 不需要返回信息，main函数里打印_OK_方便js正则匹配
    const char *p = colon + 2; // 跳过': '，直接来到目标返回值的指针
    const char *end = p;
    while (*end && *end != '\r' && *end != '\n' && end < res_ptr + res_ptr_len)
        end++;
    size_t val_len = end - p;
    if (val_len == 0) return -1; //错误码-1代表res回复里有':'，但返回信息为空，main函数里打印_ERROR_

    // 覆写res[]，格式为 "_value_\n"
    res_ptr[0] = '_';
    memmove(res_ptr + 1, p, val_len);
    res_ptr[val_len + 1] = '_';
    res_ptr[val_len + 2] = '\n';
    res_ptr[val_len + 3] = '\0';
    write(STDOUT_FILENO, res_ptr, val_len + 3);
    return 0;
}
// 不用get_modem_info，改为调用重写的send_req_and_wait, 后者有返回值且at命令长度可以存进变量复用
// extern void get_modem_info(const char *at_ptr, const char *info_fmt, char **res_ptr_ptr);
int main(int argc, char *argv[]) {
    // 1. 参数检查
    if (argc < 2 || argc > 3) {
        // 使用write避免stdout和stderr未初始化导致的写野指针
        WRITE_STR(STDERR_FILENO,
                "用法：at <AT+命令> [格式化输出标志]\n"
                "AT+命令不区分大小写，可以没有引号，不要有空格\n"
                "0=非格式化输出（默认），1=格式化输出\n"
                "by孙石讷@酷安、riuzenn@GitHub\n");
        return 1; // 错误码1代表参数非法
    }

    int at_len = strlen(argv[1]);
    // 长度必须大于3，以at+开头
    if (at_len < 4 || strncasecmp(argv[1], "AT+", 3) != 0) {
        WRITE_STR(STDERR_FILENO, "at AT+命令, 不区分大小写\n");
        return 1;
    }

    if (at_len > 250) {
//    if (strlen(argv[1]) > 250) {
        WRITE_STR(STDERR_FILENO, "AT+命令过长, 最大250字节\n");
        return 1;
    }
    
    // 解析第三个参数, 默认为0
    int more = 0;
    if (argc == 3) {
        if (strcmp(argv[2], "0") == 0) {
            more = 0;
        } else if (strcmp(argv[2], "1") == 0) {
            more = 1;
        } else {
            WRITE_STR(STDERR_FILENO, "格式化输出标志必须是0或1\n");
            return 1;
        }
    }
    
    // 2. 缓冲区
    char at[256];// 后面snprintf会在末尾自动添加\0，所以不特别清零。send_req_and_wait里硬编码最大0xda0字节
    char res[3520] = {0};// send_req_and_wait里硬编码最大3504字节
    
    // 3. 构建 AT 命令（末尾加 \r\n）
    snprintf(at, sizeof(at), "%s\r\n", argv[1]);

    // 4. 调用函数
    char *res_ptr = res;
    // safe_parse=0, timeout_sec=0, no_retry=1, 与libatutils.so里get_modem_info使用的参数一致
    int ret = send_req_and_wait(at, at_len + 2, "%s", &res_ptr, 0, 0, 1);
//    get_modem_info(at, "%s", &res_ptr);

    // 5. 输出结果
    if (more == 0) {
        write(STDOUT_FILENO, res, strlen(res));
        WRITE_STR(STDOUT_FILENO, "\n");
        return (ret == 0) ? 0 : ret; // 返回send_req_and_wait函数的错误码
    }

//    if (more == 0) {return (res[0] != '\0') ? 0 : 1;}
    // 需要格式化输出，方便js正则匹配
    if (ret != 0) {
        WRITE_STR(STDERR_FILENO, "_ERROR_\n");
        return ret;
    }
    
    int ret2 = extract_respond(res_ptr, strlen(res_ptr));
    if (ret2 == 0) {return 0;}
    if (ret2 == 1) {
        WRITE_STR(STDERR_FILENO, "_OK_\n");
        return 1; // 错误码1代表非查询类at命令成功执行
    }
    // 应该很少出现
    WRITE_STR(STDERR_FILENO, "_ERROR_\n");
    return -1; // 错误码-1代表没获取到查询信息
}
