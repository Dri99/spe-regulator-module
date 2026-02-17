
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h> 

#include <asm/cpufeature.h>
#include <asm/sysreg.h>

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/capability.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/perf/arm_pmu.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/vmalloc.h>

#include <asm/barrier.h>
#include <asm/cpufeature.h>
#include <asm/mmu.h>
#include <asm/sysreg.h>

#define BIT_GET(_fld, _reg) (_reg & BIT(_fld ## _SHIFT))
#define FIELD_GET_LOCAL(_fld, _reg) (_reg >> _fld ## _SHIFT & _fld ## _MASK)
//#define MY_READ_SYSREG(reg) 

// Initialization function (called when the module is loaded)
static int __init spe_guard_init(void)
{
	u64 reg;
	u64 reg_after;
	int fld;
	int interval;
	int max_record_sz;
	int counter_sz;
	u16 pmsver;

	fld = cpuid_feature_extract_unsigned_field(read_cpuid(ID_AA64DFR0_EL1),
						ID_AA64DFR0_PMSVER_SHIFT);
	pmsver = (u16)fld;
	pr_info("Probe spe v1.%d\n", pmsver-1);

	//reg = read_sysreg_s(SYS_CPUECTLR_EL1);
	__asm__ volatile ("mrs %0, S3_0_C15_C1_4": "=r"(reg)::);
	pr_info("CPUECTLR_EL1=%llx/n",reg);

	reg = read_sysreg_s(SYS_CNTFRQ_EL0);
	pr_info("CNTFRQ_EL0=%llu/n",reg);

	pr_info("Sleeping 100ms...");
	//reg = read_sysreg_s(cntpct_el0);
	asm volatile("mrs %0, cntpct_el0" : "=r" (reg));
	mdelay(100);
	//reg = read_sysreg_s(SYS_CNTPCT_EL0) - reg;
	asm volatile("mrs %0, cntpct_el0" : "=r" (reg_after));
	pr_info("Slept for %llu clocks", reg_after - reg);
	
	reg = read_sysreg_s(SYS_PMBIDR_EL1);
	pr_info("SYS_PMBIDR_EL1=%08llx/n",reg);
	reg = read_sysreg_s(SYS_PMBIDR_EL1);
	if (reg & BIT(SYS_PMBIDR_EL1_P_SHIFT)) {
		pr_info("profiling buffer owned by higher exception level\n");
	}
	else {
		pr_info("profiling available\n");
	}

	/* Minimum alignment. If it's out-of-range, then fail the probe */
	fld = reg >> SYS_PMBIDR_EL1_ALIGN_SHIFT & SYS_PMBIDR_EL1_ALIGN_MASK;
	pr_info("Minimum alignment: %d\n", 1 << fld);

	/* It's now safe to read PMSIDR and figure out what we've got */
	pr_info("Reading PMSIDR\n");
	reg = read_sysreg_s(SYS_PMSIDR_EL1);
	pr_info("Entire PMSIDR: 0x%08llx\n", reg);

	if (BIT_GET(SYS_PMSIDR_EL1_FE, reg))
		pr_info("FE bit set\n");

	// if (BIT_GET(SYS_PMSIDR_EL1_FnE, reg))
	// 	pr_info("FnE bit set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_FT, reg))
		pr_info("FT bit set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_FL, reg))
		pr_info("FL bit set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_ARCHINST, reg))
		pr_info("ARCHINST bit set\n");
	else
			pr_info("ARCHINST bit not set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_LDS, reg))
		pr_info("LDS bit set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_ERND, reg))
		pr_info("ERND bit set\n");
	else
		pr_info("ERND bit not set\n");

	/* This field has a spaced out encoding, so just use a look-up */
	fld = FIELD_GET(SYS_PMSIDR_EL1_INTERVAL_MASK<<SYS_PMSIDR_EL1_INTERVAL_SHIFT, reg);
	switch (fld) {
	case 0:
		interval = 256;
		break;
	case 2:
		interval = 512;
		break;
	case 3:
		interval = 768;
		break;
	case 4:
		interval = 1024;
		break;
	case 5:
		interval = 1536;
		break;
	case 6:
		interval = 2048;
		break;
	case 7:
		interval = 3072;
		break;
	default:
		fallthrough;
	case 8:
		interval = 4096;
	}
	pr_info("Interval=%d\n",interval);

	/* Maximum record size. If it's out-of-range, then fail the probe */

	fld = FIELD_GET_LOCAL(SYS_PMSIDR_EL1_MAXSIZE, reg);
	max_record_sz = 1 << fld;
	pr_info("Max record size: %d\n", max_record_sz);


	fld = FIELD_GET_LOCAL(SYS_PMSIDR_EL1_COUNTSIZE, reg);
	switch (fld) {
	default:
		counter_sz = -1;
		fallthrough;
	case 2:
		counter_sz = 12;
		break;
	case 3:
		counter_sz = 16;
	}
	pr_info("Countsize is %d\n",counter_sz);
		
	return 0;  // Return 0 means success
}

// Exit function (called when the module is removed)
static void __exit spe_guard_exit(void)
{
	printk(KERN_INFO "Goodbye, world!\n");
}

// Register the functions
module_init(spe_guard_init);
module_exit(spe_guard_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SPE based Memeguard Implementation");
MODULE_AUTHOR("Alessandro Mandrile");


