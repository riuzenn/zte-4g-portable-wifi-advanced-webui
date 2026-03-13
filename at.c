#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
int g_customer_type = 1;
int g_ap_lock = 0;
int get_mac_config_ssid_key_nv(void) {return 0;}
extern int get_modem_info(const char *at, const char *fmt, ...);
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fputs("用法：at \'AT命令\'。AT命令不区分大小写，可以没有引号，不要有空格。\nby孙石讷@酷安、riuzenn@GitHub\n", stderr);
        return 1;
    }
    else if (strncasecmp(argv[1], "AT", 2) != 0 ) {
        fputs("必须AT开头, 不区分大小写\n", stderr);
        return 1;
    }
    else if (strlen(argv[1]) > 250) {
        fputs("命令过长, 最大250字符\n", stderr);
        return 1;
    }
    char at[256];
    memset(at, 0, sizeof(at));
    snprintf(at, sizeof(at), "%s\r\n", argv[1]);
    char res[1024];
    memset(res, 0, sizeof(res));
    char *res_ptr = res;
    int ret = get_modem_info(at, "%s", &res_ptr);
    if (ret == 0 && res_ptr != NULL) {
        printf("_%s_\n", res_ptr);
        return 0;
    } else {
        printf("%d\n_ERROR_\n", ret);
        return 1;
    }
}