#include <stdio.h>
#include <unistd.h>
#include <crypt.h>

int main()
{
    printf("%-22s %s\n", "DES:", crypt("123456", "ab"));
    printf("%-22s %s\n", "MD5:", crypt("123456", "$1$testsalt$"));
    printf("%-22s %s\n", "Blowfish:", crypt("123456", "$2a$05$usesomesillystring$"));
    printf("%-22s %s\n", "SHA256:", crypt("123456", "$5$testsalt$"));
    printf("%-22s %s\n", "SHA512:", crypt("123456", "$6$testsalt$"));
    printf("%-22s %s\n", "Blowfish(fallback):", crypt("123456", "$2"));
    printf("%-22s %s\n", "SHA256(fallback):", crypt("123456", "$5"));
    printf("%-22s %s\n", "SHA512(fallback):", crypt("123456", "$6"));
    return 0;
}
