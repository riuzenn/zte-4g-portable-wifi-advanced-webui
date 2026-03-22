#include <stdio.h>
#include <time.h>

// 强制不内联，以便观察函数调用时的寄存器传参
__attribute__((noinline)) 
float multiply(float a, float b) {
    return a * b;
}

int main() {
    float x = 1.234f;
    float y = 5.678f;
    float res;

    res = multiply(x, y);

    printf("Result: %f\n", (double)res);
    return 0;
}