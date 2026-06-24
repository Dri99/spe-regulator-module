#ifndef __GIV_V3_DUMP_H
#define __GIV_V3_DUMP_H

#include <linux/kernel.h>

int init_irq_dump(void);
void free_irq_dump(void);
void bsp_irq_dump(void);
void irq_dump_activate_pmbirq(int hwirq);
void irq_dump_activate_all_irq(void);
void irq_dump_print_irq_data(int irq);

#endif //__GIV_V3_DUMP_H