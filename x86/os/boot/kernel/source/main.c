#include "print.h"
#include "init.h"
int main(void)
{
    put_str("I am kernel!\n");
    init_all();
    asm volatile("sti");
    while (1);
}