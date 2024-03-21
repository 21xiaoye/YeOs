#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "../../device/timer.h"

void init_all(){
    put_str("init_all");
    idt_init(); // 初始化中断
    timer_init();
}