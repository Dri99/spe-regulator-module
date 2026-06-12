#ifndef __GIV_V3_DUMP_H
#define __GIV_V3_DUMP_H

#include <linux/kernel.h>

int init_irq_dump(void);
void free_irq_dump(void);
void bsp_irq_dump(void);
void irq_dump_activate_pmbirq(void);
void irq_dump_activate_all_irq(void);

#endif //__GIV_V3_DUMP_H