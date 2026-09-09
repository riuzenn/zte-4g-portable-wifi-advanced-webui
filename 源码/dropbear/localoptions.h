/* ================================================================
   1. 强行屏蔽目标系统缺失的 API，改用 Dropbear 内部实现
   ================================================================ */
#undef HAVE_EXPLICIT_BZERO
#undef HAVE_FEXECVE
#undef HAVE_OPENPTY
#define USE_DEV_PTMX 1

/* ================================================================
   2. 密钥交换算法 (KEX) 优化 —— 只保留Curve25519
   ================================================================ */
#define DROPBEAR_CURVE25519 1              /* 保留 Curve25519（轻量且安全） */
#define DROPBEAR_ECDH 0                    /* 关闭 NIST 椭圆曲线（省 ~30KB 内存） */
#define DROPBEAR_DH_GROUP14_SHA256 0       /* 可以保留基础 DH 2048-bit + SHA-256（兼容） */
#define DROPBEAR_DH_GROUP14_SHA1 0         /* 关闭弱 SHA-1 版本 */
#define DROPBEAR_DH_GROUP1 0               /* 关闭 1024-bit DH（不安全） */
#define DROPBEAR_DH_GROUP16 0              /* 关闭 4096-bit DH（体积大、用不上） */
#define DROPBEAR_SNTRUP761 0               /* 关闭后量子 sntrup761（~9KB） */
#define DROPBEAR_MLKEM768 0                /* 关闭后量子 ML-KEM（~34KB！） */

/* ================================================================
   3. 主机密钥 / 公钥认证算法 —— 只保留ED25519
   ================================================================ */
#define DROPBEAR_ED25519 1                 /* 强烈推荐，速度快且体积小 */
#define DROPBEAR_RSA 0                     /* 可以保留 RSA 保证 100% 兼容性 */
#define DROPBEAR_RSA_SHA1 0                /* 关闭 RSA-SHA1（不安全，默认已关） */
#define DROPBEAR_ECDSA 0                   /* 关闭 ECDSA（省 TomMath 大块代码） */
#define DROPBEAR_DSS 0                     /* 关闭 DSS（不安全，默认已关） */
#define DROPBEAR_SK_KEYS 0                 /* 关闭 FIDO/U2F 硬件密钥支持（嵌入式用不上） */

/* ================================================================
   4. 对称加密算法 (Cipher) —— 只保留 CHACHA20POLY1305
   ================================================================ */
#define DROPBEAR_CHACHA20POLY1305 1        /* 首选，ARM 上无 AES 硬件时极快 */
#define DROPBEAR_AES128 0                  /* 可以保留 AES128 作为备胎（兼容老旧客户端） */
#define DROPBEAR_AES256 0                  /* 关闭 AES256（省内存） */
#define DROPBEAR_3DES 0                    /* 关闭老旧 3DES */
#define DROPBEAR_TWOFISH 0                 /* 关闭 Twofish */
#define DROPBEAR_BLOWFISH 0                /* 关闭 Blowfish */
#define DROPBEAR_ENABLE_CTR_MODE 0         /* AES-CTR 模式（AES128 必须开启此模式才能用） */
#define DROPBEAR_ENABLE_CBC_MODE 0         /* 关闭 CBC（不安全） */
#define DROPBEAR_ENABLE_GCM_MODE 0         /* 关闭 GCM（体积大，已有 ChaCha） */

/* ================================================================
   5. 消息认证算法 (MAC) —— 只用 SHA2-256，其余关闭
   ================================================================ */
#define DROPBEAR_SHA1_HMAC 0               /* 关闭 SHA1-HMAC（不安全） */
#define DROPBEAR_SHA1_96_HMAC 0            /* 关闭 SHA1-96（更弱） */
#define DROPBEAR_SHA2_256_HMAC 1           /* 保留标准 SHA2-256 */
#define DROPBEAR_SHA2_512_HMAC 0           /* 关闭 SHA2-512（体积大，用不上） */

/* ================================================================
   6. 转发与重执行 —— 全部禁用（嵌入式设备不需要）
   ================================================================ */
#define DROPBEAR_X11FWD 0                  /* 关闭 X11 转发 */
#define DROPBEAR_SVR_AGENTFWD 0            /* 关闭服务端 Agent 转发 */
#define DROPBEAR_CLI_AGENTFWD 0            /* 关闭客户端 Agent 转发 */

/* ================================================================
   7. TCP 端口转发 —— 全部禁用（只用 SSH 会话和 SCP）
   ================================================================ */
#define DROPBEAR_CLI_LOCALTCPFWD 0
#define DROPBEAR_CLI_REMOTETCPFWD 0
#define DROPBEAR_SVR_LOCALTCPFWD 0
#define DROPBEAR_SVR_REMOTETCPFWD 0
#define DROPBEAR_SVR_LOCALSTREAMFWD 0
#define DROPBEAR_SVR_REMOTESTREAMFWD 0

/* ================================================================
   8. 客户端相关功能 —— 本设备仅作为服务端，全部关闭
   ================================================================ */
#define DROPBEAR_CLI_PASSWORD_AUTH 0
#define DROPBEAR_CLI_PUBKEY_AUTH 0
#define DROPBEAR_CLI_PROXYCMD 0
#define DROPBEAR_CLI_NETCAT 0
#define DROPBEAR_CLI_COMPRESSION 0         /* 关闭客户端压缩（服务端无压缩） */
#define DROPBEAR_USE_SSH_CONFIG 0
#define DROPBEAR_USE_PASSWORD_ENV 0
#define DROPBEAR_CLI_ASKPASS_HELPER 0
#define DROPBEAR_CLI_IMMEDIATE_AUTH 0

/* ================================================================
   9. 其他可节省 ROM 的选项
   ================================================================ */
#define DO_MOTD 0                          /* 关闭登录后的 MOTD 显示 */

/* ================================================================
   10. 安全与并发限制（降低资源占用）
   ================================================================ */
#define MAX_UNAUTH_CLIENTS 2               /* 未认证并发连接数限制（防内存耗尽） */
#define MAX_AUTH_TRIES 3                   /* 最大认证尝试次数 */
#define MAX_UNAUTH_PER_IP 2                /* 单 IP 未认证连接限制 */
#define MAX_PUBKEY_QUERIES 2               /* 最大公钥查询次数（减少 CPU 开销） */
#define DROPBEAR_REEXEC 0
/* 这里设置sftp-server的放置路径 */
#define SFTPSERVER_PATH "/usr/sbin/sftp-server"
