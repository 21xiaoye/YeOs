#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "print.h"
#include "io.h"

#define PIC_M_CTRL 0x20 // 主片
#define PIC_M_DATA 0x21
#define PIC_S_CTRL 0xA0 // 从片
#define PIC_S_DATA 0xA1

#define IDT_DESC_CNT 0x30 // 目前总共支持的中断数量

/*中断描述结构体*/
struct gate_desc
{
        uint16_t func_offset_low_word;
        uint16_t selector;
        uint8_t dcount; // 此项位双字计数字段，是门描述符第四字节，是固定值
        uint8_t attribute;
        uint16_t func_offset_high_word;
};

// 静态函数声明
static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function);
static struct gate_desc idt[IDT_DESC_CNT];          // idt中断门描述符数组
char *intr_name[IDT_DESC_CNT];                      // 保存异常的名字
intr_handler idt_table[IDT_DESC_CNT];               // 中断处理程序数组
extern intr_handler intr_entry_table[IDT_DESC_CNT]; // 中断处理程序入口地址
// intr_handler 实际上是 void* 在 interrupt.h 里定义的
/* 初始化可编程中断控制器 8259A */
static void pic_init(void)
{
        // 初始化主片
        outb(PIC_M_CTRL, 0x11); // ICW1: 0001 0001 ,边沿触发，级联 8259，需要ICW4
        /**
         * bochs进行模拟的时候，初始向量设置为0x20应该为0x20但是触发的是0x06的中断处理程序，设置为0x01~0x19则正常
         * 此处未知晓产生的原因，猜测是bochs模拟不完整的原因
        */
        outb(PIC_M_DATA, 0x20); // ICW2: 0010 0000 ,起始中断向量号为 0x20(0x20-0x27) 0~19 处理器内部固定中断 20~31 intel保留
        outb(PIC_M_DATA, 0x04); // ICW3: 0000 0100 ,IR2 接从片
        outb(PIC_M_DATA, 0x01); // ICW4: 0000 0001 ,8086 模式，正常EOI

        // 初始化从片
        outb(PIC_S_CTRL, 0x11); // ICW1: 0001 0001 ,边沿触发，级联 8259，需要ICW4
        outb(PIC_S_DATA, 0x28); // ICW2: 0010 1000 ,起始中断向量号为 0x28(0x28-0x2f)
        outb(PIC_S_DATA, 0x02); // ICW3: 0000 0010 ,设置连接到主片的 IR2 引脚
        outb(PIC_S_DATA, 0x01); // ICW4: 0000 0001 ,8086 模式，正常EOI

        // 打开主片上的 IR0 也就是目前只接受时钟产生的中断
        // eflags 里的 IF 位对所有外部中断有效，但不能屏蔽某个外设的中断了
        outb(PIC_M_DATA, 0xfe);
        outb(PIC_S_DATA, 0xff);

        put_str("    pic init done\n");
}

/*创建中断门描述符*/
// 参数：中断描述符，中断描述符内的属性，中断处理函数地址
// 功能：向中断描述符填充属性和地址
static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function)
{
        p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000FFFF;
        p_gdesc->selector = SELECTOR_K_CODE;
        p_gdesc->dcount = 0;
        p_gdesc->attribute = attr;
        p_gdesc->func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}

/*初始化中断描述符表*/
static void idt_desc_init(void)
{
        int i;
        for (i = 0; i < IDT_DESC_CNT; i++)
        {
                make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]); // IDT_DESC_DPL0在global.h定义的
        }
        put_str("       idt_desc_init done\n");
}

/**
 * 通用中断处理函数，用于异常出现时的处理
 */
static void general_intr_handler(uint8_t vec_nr)
{
        if (vec_nr == 0x27 || vec_nr == 0x2f)
        {
                // 0x2f为8259A最后一个引脚，保留项
                // IRQ7和IRQ15会产生伪中断
                return;
        }
        put_str("int vector : 0x");
        put_int(vec_nr);
        put_char(' ');
        put_str(intr_name[vec_nr]);
        put_char('\n');
}

/**
 * 完成一般处理中断处理函数及其异常名称注册
 */
static void exception_init(void)
{
        int i;
        for (int i = 0; i < IDT_DESC_CNT; i++)
        {
                idt_table[i] = general_intr_handler;
                intr_name[i] = "unknown";
        }
        intr_name[0] = "#DE Divide Error";
        intr_name[1] = "#DB Debug Exception";
        intr_name[2] = "NMI Interrupt";
        intr_name[3] = "#BP Breakpoint Exception";
        intr_name[4] = "#OF Overflow Exception";
        intr_name[5] = "#BR BOUND Range Exceeded Exception";
        intr_name[6] = "#UD Invalid Opcode Exception";
        intr_name[7] = "#NM Device No七 Available Exception";
        intr_name[8] = "JIDF Double Fault Exception";
        intr_name[9] = "Coprocessor Segment Overrun";
        intr_name[10] = "#TS Invalid TSS Exception";
        intr_name[11] = "#NP Segment Not Present";
        intr_name[12] = "#SS Stack Fault Exception";
        intr_name[13] = "#GP General Protection Exception";
        intr_name[14] = "#PF Page-Fault Exception";
        // intr_name[l5]第15项是intel保留项，未使用
        intr_name[16] = "#MF x87 FPU F'loating-Point Error";
        intr_name[17] = "#AC Alignment Check Exception";
        intr_name[18] = "#MC Machine-Check Exception";
        intr_name[19] = "#XF SIMD Floating-Point Exception";
}

/*完成有关中断的所有初始化工作*/
void idt_init()
{
        put_str("idt_init start\n");
        idt_desc_init();        //初始化中断描述符表
        exception_init();       //初始化异常名称并注册通用处理程序
        pic_init();             //初始化 8259A

        /*加载 idt*/
        /**
         * (sizeof(idt) - 1) 得出段界限，因为没有48位数据类型，使用64位
         * (uint64_t)((uint32_t)idt << 16)) 得到高32位左移16位，低16位存段界限
         */
        uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)((uint32_t)idt << 16)));
        asm volatile("lidt %0" ::"m"(idt_operand));
        put_str("idt_init done\n");
}
