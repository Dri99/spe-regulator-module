
#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "spe_ring.h"
#include "arch_io.h"

// From marron tegra234/hardware.xml
/*
* <!-- I/O needed by the kernel -->
*	<io name="gic_dist"   start="0x0f400000" size="0x10000" write="1" cache="1"/>
*	<io name="gic_redist" start="0x0f440000" size="0x200000" write="1" cache="1"/>
*/
#define CFG_IO_ADDR_gic_dist 0x0f400000
#define CFG_IO_ADDR_gic_redist 0x0f440000
/** GIC specific addresses */
#define GIC_ICC_MODE		1	/* use ICC_x_EL1 registers to access GICC */
#define GIC_DIST_BASE		CFG_IO_ADDR_gic_dist
#define GIC_REDIST_BASE		CFG_IO_ADDR_gic_redist
#define GIC_REDIST_SIZE		0x20000		/* GICv3: 128K, GICv4: 256K */
#define GIC_NUM_IRQS		(32 + 960)
#define GIC_NUM_CORES		8
/* GICv3: translate logical CPU ID to REDIST offset, SGIR ID, or IROUTER ID */
#define GIC_CPU_REDIST_ID(x)	(x)
#define GIC_CPU_SGIR_ID(x)		gic_cpu_sgir_id(x)
#define GIC_CPU_ROUTER_ID(x)	gic_cpu_router_id(x)

#define GICD_CTLR 	0x0000
#define GICD_TYPER	0x0004	/* read-only type register */
#define GICR_WAKER	(0x0014)
#define GICD_PENDING	0x0280	/* write 1 to clear an interrupt */

#define GICR_TYPER	0x0008

#define SGI_base	0x10000

#define GICR_IGROUPR0 	(SGI_base + 0x0080)
#define GICR_ISENABLER0 (SGI_base + 0x0100)
#define GICR_ISPENDR0	(SGI_base + 0x0200)
#define GICR_ISACTIVER0	(SGI_base + 0x0300)
#define GICR_ICPENDR0	(SGI_base + 0x0280) //0x10280	/* write 1 to clear an interrupt */
#define GICR_IGRPMODR0 	(SGI_base + 0x0D00)


/* accessors to GIC system register interface registers */
#define gen_icc_get_reg(name, reg) \
static inline unsigned long icc_get_##name(void)	\
{	\
	unsigned long val;	\
	__asm__ volatile ("mrs %0, " __stringify(reg) : "=r"(val));	\
	return val;	\
}

gen_icc_get_reg(hppir, ICC_HPPIR1_EL1)	/* highest priority pending interrupt register */


/* real number of interrupts (the GIC supports up to 1024) */
static unsigned int gic_num_irqs;
void __iomem *gicd;
void __iomem *gicr;
struct device_node *np;

/* accessors to GICR registers (with CPU-specific offset to GIC_REDIST_BASE) */
static inline uintptr_t gicr_percpu_addr(unsigned int cpu)
{
	return GIC_CPU_REDIST_ID(cpu) * GIC_REDIST_SIZE;
}

static inline uint32_t gicr_read32(unsigned int cpu, unsigned long reg)
{
	return readl(gicr + gicr_percpu_addr(cpu) + reg);
}

static inline void gicr_write32(u32 value, unsigned int cpu, unsigned long reg)
{
	return writel(value, gicr + gicr_percpu_addr(cpu) + reg);
}

static inline uint64_t gicr_read64(unsigned int cpu, unsigned long reg)
{
	return readq(gicr + gicr_percpu_addr(cpu) + reg);
}

/* accessors to GICD registers */
static inline uint32_t gicd_read32(unsigned long reg)
{
	return readl(gicd + reg);
}

int init_irq_dump(void)
{
	unsigned int val;
	np = of_find_compatible_node(NULL, NULL, "arm,gic-v3");
	if (!np) {
		pr_err("GIC node not found\n");
		return -ENODEV;
	}

	/* reg[0] = Distributor */
	gicd = of_iomap(np, 0);
	if (!gicd) {
		pr_err("Failed to map GICD\n");
		of_node_put(np);
		return -ENOMEM;
	}

	/* reg[1] = Redistributor region */
	gicr = of_iomap(np, 1);
	if (!gicr) {
		pr_err("Failed to map GICR\n");
		iounmap(gicd);
		of_node_put(np);
		return -ENOMEM;
	}
	
	val = readl(gicd + GICD_TYPER);
	gic_num_irqs = ((val & 0x1f) + 1) * 32;
	pr_info("GIC supports %u IRQs\n", gic_num_irqs);
	return 0;
}

void free_irq_dump(void)
{
	iounmap(gicd);
	iounmap(gicr);
	of_node_put(np);
}

void bsp_irq_dump(void)
{
	unsigned int cpu_id;
	unsigned int i;
	pr_info("GIC HPPIR: %08llx\n", read_sysreg_s(SYS_ICC_HPPIR1_EL1));
	pr_info("SYS_ICC_IGRPEN1_EL1: %08llx\n", read_sysreg_s(SYS_ICC_IGRPEN1_EL1));
	pr_info("SYS_ICC_PMR_EL1: %08llx\n", read_sysreg_s(SYS_ICC_PMR_EL1));;

	cpu_id = smp_processor_id();

	pr_info("GICD_CTLR  = 0x%08x\n", readl(gicd + 0x0000));
	pr_info("GICD_TYPER = 0x%08x\n", readl(gicd + 0x0004));
	pr_info("GICD_IIDR  = 0x%08x\n", readl(gicd + 0x0008));

	pr_info("All GICR_TYPER");
	for_each_possible_cpu(i){
		pr_info(" %016llx", gicr_read64(i, GICR_TYPER));
	}
	pr_info("GICR_IGROUPR0: %08x\n", gicr_read32(0, GICR_IGROUPR0));
	pr_info("GICR_ISENABLER0: %08x\n", gicr_read32(0, GICR_ISENABLER0));
	pr_info("GICR_ISPENDR0: %08x\n", gicr_read32(0, GICR_ISPENDR0));
	pr_info("GICR_ISACTIVER0: %08x\n", gicr_read32(0, GICR_ISACTIVER0));
	pr_info("GICR_ICPENDR0: %08x\n", gicr_read32(0, GICR_ICPENDR0));
	
	pr_info("GICR_IGRPMODR0: %08x\n", gicr_read32(0, GICR_IGRPMODR0));
	
	pr_info("\n");
}

void irq_dump_activate_pmbirq(int hwirq)
{
	u32 ispendr0 = gicr_read32(0, GICR_ISPENDR0);
	gicr_write32((ispendr0 | (0x1 << hwirq)), 0, GICR_ISPENDR0);
}

void irq_dump_activate_all_irq(void)
{
	u32 isactiver0 = gicr_read32(0, GICR_ISACTIVER0);
	gicr_write32((isactiver0 | 0xffff0000), 0, GICR_ISACTIVER0);
}

void irq_dump_print_irq_data(int irq)
{
	struct irq_data *data = irq_get_irq_data(irq);
	pr_info("irq_data::hwirq = %ld\n", data->hwirq);
}