#include <stdio.h>
#include <unistd.h>
#include <crypt.h>

int main()
{
    printf("DES: %s\n", crypt("123456", "ab"));
    printf("MD5: %s\n", crypt("123456", "$1$testsalt$"));
    printf("Blowfish: %s\n", crypt("123456", "$2a$05$usesomesillystring$"));
    printf("SHA256: %s\n", crypt("123456", "$5$testsalt$"));
    printf("SHA512: %s\n", crypt("123456", "$6$testsalt$"));
    return 0;
}
