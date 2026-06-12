
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
#include <linux/cpu_pm.h>
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
#include <asm/unaligned.h>
#include <linux/vmalloc.h>

#include <asm/barrier.h>
#include <asm/cpufeature.h>
#include <asm/mmu.h>
#include <asm/sysreg.h>

#define STR_BUFFER_LEN 255
//TODO: this should be a module parameter

#define BUFFER_SIZE (1 << 21)
#define SPE_BW_PERIOD 256
#define WATERMARK_NUM 1
#define WATERMARK_DEN 2
#define CONFIG_PERF_OPTIMISED 1
#define PKT_PAYLOAD_SZ_MASK (0x3)
#define PKT_PAYLOAD_SZ_SHIFT (4)
#define SYS_PMSCR_EL1_EE_SHIFT	8
#define ID_AA64DFR2_EL1		sys_reg(3, 0, 0, 5, 2)

#define BIT_GET(_fld, _reg) (_reg & BIT(_fld ## _SHIFT))
#define BIT_SET(_fld) (_reg & BIT(_fld ## _SHIFT))
#define FIELD_GET_LOCAL(_fld, _reg) (_reg >> _fld ## _SHIFT & _fld ## _MASK)
#define FIELD_SET_LOCAL(_fld, _reg) ((_reg & _fld ## _MASK) << _fld ## _SHIFT )
//assert(typeof(var1)==typeof(var2))
#define SWAP(var1, var2) 	\
do {				\
	typeof(var1) temp;	\
	temp = var1;		\
	var1 = var2;		\
	var2 = temp;		\
}while(0);
#define READ_CNTCPT_EL0()						\
({ 									\
	u64 __reg; asm volatile("mrs %0, cntpct_el0" : "=r" (__reg));	\
	__reg;								\
})

#if CONFIG_PERF_OPTIMISED > 0
#define pr_info_concat(_str,...)				\
({do {} while(0);})
#else
#define pr_info_concat(_str,...) 				\
({								\
	sprintf(string_short_buffer, _str, __VA_ARGS__);	\
	strncat(string_buffer, string_short_buffer,		\
		 STR_BUFFER_LEN - strlen(string_buffer) - 1);	\
})
#endif

#if CONFIG_PERF_OPTIMISED == 0
//To Enable pr_debug, just enable DEBUG
#define PR_TRACE(...) pr_debug(__VA_ARGS__)
#define PR_DEBUG(...) pr_info(__VA_ARGS__)
#elif CONFIG_PERF_OPTIMISED > 0
#define PR_TRACE(...) pr_debug(__VA_ARGS__)
#define PR_DEBUG(...) pr_debug(__VA_ARGS__)
#endif

#define OTHER_BUFFER(active_one) ((active_one + 1) % 2)

/**************************************************************************
 * Public Types
 **************************************************************************/
 /*
 * Unreachable codes are inserted to allow optimisations from the compiler,
 * since it will be able to group together similar codes
*/
enum spe_header_type
{
	PKT_PADDING = 		0x00,
	PKT_END = 		0x01,
	PKT_TIMESTAMP = 	0x71,
	PKT_EVENTS_1B = 	0x42,
	PKT_EVENTS_2B = 	0x52,
	PKT_EVENTS_4B = 	0x62,
	PKT_EVENTS_8B = 	0x72,
	PKT_DATA_SOURCE_1B = 	0x43,
	PKT_DATA_SOURCE_2B = 	0x53,
	// Unreachable
	PKT_DATA_SOURCE_4B = 	0x63,
	// Unreachable
	PKT_DATA_SOURCE_RES = 	0x73,
	PKT_CONTEXT_EL1 = 	0x64,
	PKT_CONTEXT_EL2 = 	0x65,
	// Unreachable
	PKT_CONTEXT_RES0 = 	0x66,
	// Unreachable
	PKT_CONTEXT_RES1 = 	0x67,
	PKT_OP_TYPE_OTHER = 	0x48,
	PKT_OP_TYPE_LDST = 	0x49,
	PKT_OP_TYPE_BRANCH = 	0x4A,
	PKT_OP_TYPE_RES0 = 	0x4B,	
	PKT_ADDR_SH_PC = 	0xB0,
	PKT_ADDR_SH_B_TARGET = 	0xB1,
	PKT_ADDR_SH_ACC_VA = 	0xB2,
	PKT_ADDR_SH_ACC_PA =	0xB3,
	// Unreachable
	PKT_ADDR_SH_RES0 =	0xB4,
	// Unreachable
	PKT_ADDR_SH_RES1 =	0xB5,
	PKT_ADDR_SH_IMPL_DEF_0 =0xB6,
	PKT_ADDR_SH_IMPL_DEF_1 =0xB7,
	PKT_CNT_SH_TOT_LAT = 	0x98,
	PKT_CNT_SH_ISSUE_LAT = 	0x99,
	PKT_CNT_SH_XLAT = 	0x9A,
	// Unreachable
	PKT_CNT_SH_RES0 =	0x9B,
	// Unreachable
	PKT_CNT_SH_RES1 =	0x9C,
	// Unreachable
	PKT_CNT_SH_RES2 =	0x9D,
	PKT_CNT_SH_IMPL_DEF_0 =	0x9E,
	PKT_CNT_SH_IMPL_DEF_1 =	0x9F,
	PKT_LONG_GENERIC_0 =	0x20,
	PKT_LONG_GENERIC_1 =	0x21,
	PKT_LONG_GENERIC_2 =	0x22,
	PKT_LONG_GENERIC_3 =	0x23,
	PKT_UNINITIALISED =	0xFF,
};

struct spe_buffer {
	void *buf;
	void *limit;
	void *watermark;
	char ready;
};

struct core_info{
	struct spe_buffer primary;
	struct spe_buffer secondary;
	unsigned int active_buffer;
	void *buffer_base;
	/*
	 * TODO: [IDEA] pass only core_info and have it pointing to 
	 * global spe_ctrl
	 * struct spe_ctrl *spe_ctrl;
	*/
};

struct spe_ctrl {
	size_t size;
	//interrupts = <0x01 0x05 0x04>;
	//		<Type(PPI) Number TriggerType(active high level sensitive)>
	int irq;

	bool filter_ld;
	bool filter_st;
	bool ts_enable;
	bool pa_enable;
	bool pct_enable;
	bool exclude_user;
	bool exclude_kernel;
	bool cx_enable;
	unsigned int period;
	unsigned int advance;

	cpumask_var_t target_cpu;
	int reader_cpu;

	struct task_struct *reader_task;
	struct platform_device *pdev;
	bool running;
};

enum spe_bw_buf_fault_action {
	SPE_BW_BUF_FAULT_ACT_SPURIOUS,
	SPE_BW_BUF_FAULT_ACT_FATAL,
	SPE_BW_BUF_FAULT_ACT_OK,
};

static const size_t NUM_STATS_RECORDS=1<<(2+20);
/**************************************************************************
 * Global Variables
 **************************************************************************/
static struct spe_ctrl spe;
static struct core_info __percpu *core_info;
static unsigned int extended_packets = 0;

static struct kobject *spe_kobj;

unsigned int irq_called = 0;
unsigned int spe_bw_management_called[NR_CPUS];
#if CONFIG_PERF_OPTIMISED < 1
static char string_buffer[STR_BUFFER_LEN];
static char string_short_buffer[STR_BUFFER_LEN];
#endif
static u64 first_ts = 0,last_ts = 0;
static unsigned long long timestamp_delta = 0, precess_record_time;
static unsigned long long read_records;
struct ts_stats{
	unsigned int spe_reader_delay;
	unsigned int reader_process_delay;
};
static struct ts_stats *ts_stats_arr;
//struct kobj_attribute etx_attr = __ATTR(etx_value, 0660, sysfs_show, sysfs_store);
/**************************************************************************
 * Local Function Prototypes
 **************************************************************************/

static ssize_t sysfs_store_control(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t count);
static ssize_t sysfs_store_target_cpu(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count);
static ssize_t sysfs_store_reader_cpu(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count);

static struct kobj_attribute control_attr = __ATTR(control, 0664, NULL,
							 sysfs_store_control);
static struct kobj_attribute reader_cpu_attr =  __ATTR(reader_cpu, 0664, NULL, 
							sysfs_store_reader_cpu);
static struct kobj_attribute target_cpu_attr = __ATTR(target_cpu, 0664, NULL, 
							sysfs_store_target_cpu);


/**************************************************************************
 * Module main code
 **************************************************************************/
static void spe_bw_disable_and_drain_local(void)
{
	/* Disable profiling at EL0 and EL1 */
	write_sysreg_s(0, SYS_PMSCR_EL1);
	isb();

	/* Drain any buffered data */
	psb_csync();
	dsb(nsh);

	/* Disable the profiling buffer */
	write_sysreg_s(0, SYS_PMBLIMITR_EL1);
	isb();
}

/* Driver and device probing */
static int spe_bw_irq_probe(struct spe_ctrl *spe_ctrl)
{
	struct platform_device *pdev = spe_ctrl->pdev;
	int irq = platform_get_irq(pdev, 0);

	if (irq < 0)
		return -ENXIO;

	if (!irq_is_percpu(irq)) {
		dev_err(&pdev->dev, "expected PPI but got SPI (%d)\n", irq);
		return -EINVAL;
	}

	spe_ctrl->irq = irq;

	return 0;
}

/* Convert between user ABI and register values */
static u64 spe_bw_fill_pmscr(void)
{
	u64 reg = 0;

	reg |= spe.ts_enable << SYS_PMSCR_EL1_TS_SHIFT;
	reg |= spe.pa_enable << SYS_PMSCR_EL1_PA_SHIFT;
	reg |= spe.pct_enable << SYS_PMSCR_EL1_PCT_SHIFT;
	// Useless until not running on a FEAT_SPE_EXC cpu
	reg |= 0x11 << SYS_PMSCR_EL1_EE_SHIFT;
	
	if (!spe.exclude_user)
		reg |= BIT(SYS_PMSCR_EL1_E0SPE_SHIFT);

	if (!spe.exclude_kernel)
		reg |= BIT(SYS_PMSCR_EL1_E1SPE_SHIFT);

	if (spe.cx_enable)
		reg |= BIT(SYS_PMSCR_EL1_CX_SHIFT);

	return reg;
}

static u64 spe_bw_get_record_timestamp(void *payload_addr){
	u64 timestamp;
	if(likely(((u64)payload_addr & (0x7U)) == 0U)){
		timestamp = *(u64 *)payload_addr;
	}else{
		timestamp = get_unaligned((u64 *)payload_addr);
	}
	return timestamp;
}

static void spe_bw_manage_buffer(void *info){
	unsigned int cpu = smp_processor_id();
	struct spe_ctrl *spe_ctrl = (struct spe_ctrl*) info;
	struct core_info *cinfo = this_cpu_ptr(core_info);
	void* secondary_buf,*secondary_limit;
	u64 reg;

	spe_bw_management_called[cpu]++;
	pr_debug("Management function triggered on cpu %d", cpu);
	spe_bw_disable_and_drain_local();
	reg = read_sysreg_s(SYS_PMBPTR_EL1);
	
	//Communicate limit
	cinfo->primary.limit = (void*) reg;
	
	reg = read_sysreg_s(SYS_PMBSR_EL1);
	//TODO: Missing check for syndrome
	if(reg & BIT(SYS_PMBSR_EL1_COLL_SHIFT)){
		/*
		 * TODO: For now we don't care, however, we can levarege this
		 * instead of the PMC to consider increasing the profiling 
		 * period.
		*/
	}
	// Mask COLL as we already checked it
	reg = reg & ~BIT(SYS_PMBSR_EL1_COLL_SHIFT);
	if(reg != 0){
		pr_warn("spe_bw_manage_buffer(): SYS_PMBSR_EL1: %llx",reg);
	} 

	write_sysreg_s(0, SYS_PMBSR_EL1);

	//Allocate secondary_buffer
	secondary_buf = cinfo->buffer_base
			+ OTHER_BUFFER(cinfo->active_buffer) 
			* spe_ctrl->size;
	memset(secondary_buf,0xff,spe_ctrl->size);
	secondary_limit = secondary_buf + spe_ctrl->size;
	// Ensure memset is completed
	wmb();
	write_sysreg_s(secondary_buf, SYS_PMBPTR_EL1);

	reg = (u64)secondary_limit;
	reg |= BIT(SYS_PMBLIMITR_EL1_E_SHIFT);
	write_sysreg_s(reg, SYS_PMBLIMITR_EL1);
	isb();

	cinfo->secondary.buf = secondary_buf;
	cinfo->secondary.limit = secondary_limit;
	cinfo->secondary.watermark = secondary_buf 
			+ (spe_ctrl->size)
			* WATERMARK_NUM
			/ WATERMARK_DEN;
	wmb();
	cinfo->secondary.ready = 1;
	// Set other flags and start sampling
	reg = spe_bw_fill_pmscr();
	write_sysreg_s(reg, SYS_PMSCR_EL1);
	isb();
	
}

static int process_record(void *base){
	bool end = false;
	void *header_addr = base;
	u8 header;
	unsigned int payload_size = 0;
	unsigned int header_size = 0;
	u64 timestamp;
	bool header_match;
#if CONFIG_PERF_OPTIMISED == 0
	int i;
	unsigned int packets_found = 0;
	string_buffer[0] = '\0';
#endif
	while(header_addr < (base + 64UL) && !end){
		header = READ_ONCE(*(u8 *)header_addr);
		header_size = 1;
		payload_size = 0;
		if(header < 0x20){
			pr_debug("Header-only: %x\n", header);
			header_size = 1;
			payload_size = 0;
		}else if(header < 0x40){
			pr_debug("Extended:");
			header = *(u8 *)(header_addr+1U);
			header_size = 2;
			payload_size = 1 << FIELD_GET_LOCAL(PKT_PAYLOAD_SZ,
								header);
			
		}else{
			header_size = 1;
			payload_size = 1 << FIELD_GET_LOCAL(PKT_PAYLOAD_SZ,
								header);
		}

				header_match = true;
		switch(header){
			case PKT_PADDING:
			//Do Nothing
				break;
			case PKT_END:
#if CONFIG_PERF_OPTIMISED == 0
				pr_info("%s",string_buffer);
#endif
				pr_debug("End packet\n");
				end = true;
				break;
			case PKT_TIMESTAMP:
				timestamp = spe_bw_get_record_timestamp(header_addr+1);
				pr_debug("Timestamp addr:%llx",(u64)(header_addr+1));
				pr_debug("TImestamp:%lld", timestamp);
				pr_info_concat("Timestamp packet(%d B), end\n", payload_size);
#if CONFIG_PERF_OPTIMISED == 0
				PR_TRACE("%s",string_buffer);
#endif
				last_ts = timestamp;
				end = true;
				break;
			case PKT_EVENTS_1B:
			case PKT_EVENTS_2B:
			case PKT_EVENTS_4B:
			case PKT_EVENTS_8B:
				pr_info_concat("Events packet (%d B), ", payload_size);
				break;
			case PKT_DATA_SOURCE_1B:
			case PKT_DATA_SOURCE_2B:
			case PKT_DATA_SOURCE_4B:
			case PKT_DATA_SOURCE_RES:
				pr_info_concat("Data src packet (%d B), ", payload_size);
				break;
			case PKT_CONTEXT_EL1:
			case PKT_CONTEXT_EL2:
			case PKT_CONTEXT_RES0:
			case PKT_CONTEXT_RES1:
				pr_info_concat("Context packet (%d B), ", payload_size);
				break;
			case PKT_OP_TYPE_OTHER:
			case PKT_OP_TYPE_LDST:
			case PKT_OP_TYPE_BRANCH:
			case PKT_OP_TYPE_RES0:
				pr_info_concat("OP type packet (%d B), ", payload_size);
				break;
			case PKT_ADDR_SH_PC:
			case PKT_ADDR_SH_B_TARGET:
			case PKT_ADDR_SH_ACC_VA:
			case PKT_ADDR_SH_ACC_PA:
			case PKT_ADDR_SH_RES0:
			case PKT_ADDR_SH_RES1:
			case PKT_ADDR_SH_IMPL_DEF_0:
			case PKT_ADDR_SH_IMPL_DEF_1:
				pr_info_concat("Address packet (%d B), ", payload_size);
				break;
			case PKT_CNT_SH_TOT_LAT:
			case PKT_CNT_SH_ISSUE_LAT:
			case PKT_CNT_SH_XLAT:
			case PKT_CNT_SH_RES0:
			case PKT_CNT_SH_RES1:
			case PKT_CNT_SH_RES2:
			case PKT_CNT_SH_IMPL_DEF_0:
			case PKT_CNT_SH_IMPL_DEF_1:
				pr_info_concat("Counter packet (%d B), ", payload_size);
				break;
			case PKT_LONG_GENERIC_0:
			case PKT_LONG_GENERIC_1:
			case PKT_LONG_GENERIC_2:
			case PKT_LONG_GENERIC_3:
				extended_packets++;
			break;
			case PKT_UNINITIALISED:
				pr_warn("Uninitialised!\n");
				//TODO:  better return from else-where
				return -((int)0xFF);
				break;
				default:
				header_match = false;
				break;
			}
		if(!header_match){
#if CONFIG_PERF_OPTIMISED == 0
			PR_TRACE("%s",string_buffer);
			string_buffer[0] = '\0';
			for(i=0; i<64;i++){
				pr_info_concat("%02X ",((u8*)base)[i] );
			}
			PR_DEBUG("%s\n", string_buffer);
#endif				
			pr_debug("Unknown packet (%d B)(%x)", payload_size,header);
			return -((int)header);
		}
#if CONFIG_PERF_OPTIMISED == 0
		// This counts all non-padding packets
		if(header != 0x00){
			packets_found++;		
		}
#endif
		header_addr = header_addr + payload_size + header_size;
	}
#if CONFIG_PERF_OPTIMISED < 1
	PR_DEBUG("Found %d packets\n", packets_found);
#endif
	return 0;
}


static void spe_enable_cpu(void *info)
{
	struct spe_ctrl *s = info;
	u64 reg = 0;
	struct core_info *cinfo = this_cpu_ptr(core_info);
	smp_rmb();
	pr_info("cpu %d : spe_enable",smp_processor_id());
	pr_debug("core_info address %llx",(u64)core_info);
	pr_debug("Buffer at address %llx\n", (u64)READ_ONCE(cinfo->buffer_base));

	enable_percpu_irq(s->irq, IRQ_TYPE_NONE);
	pr_debug("Enabling irq %d on cpu %d", s->irq, smp_processor_id());
	// Enable Filter by type
	if(s->filter_ld)
		reg |= BIT(SYS_PMSFCR_EL1_LD_SHIFT);
	if(s->filter_st)
		reg |= BIT(SYS_PMSFCR_EL1_ST_SHIFT);
	
	// If at least one filter is active, activate them all
	if (reg)
		reg |= BIT(SYS_PMSFCR_EL1_FT_SHIFT);

	write_sysreg_s(reg, SYS_PMSFCR_EL1);

	// Set Interval counter
	reg = 0;
	//reg |= FIELD_SET_LOCAL(SYS_PMSIRR_EL1_INTERVAL, s->period);
	reg |=  s->period;
	write_sysreg_s(reg, SYS_PMSIRR_EL1);
	
	/* Program SPE buffer registers */
	write_sysreg_s(cinfo->primary.buf, SYS_PMBPTR_EL1);

	reg = (u64)(cinfo->primary.limit);
	reg |= BIT(SYS_PMBLIMITR_EL1_E_SHIFT);
	write_sysreg_s(reg, SYS_PMBLIMITR_EL1);
	isb();
	reg = read_sysreg_s(SYS_PMBLIMITR_EL1);
	pr_debug("SYS_PMBLIMITR_EL1 status: %llx\n",reg);
	
	
	//Clear any interrupt state
	write_sysreg_s(0, SYS_PMBSR_EL1);
	isb();

	// Set other flags and start sampling
	reg = spe_bw_fill_pmscr();
	write_sysreg_s(reg, SYS_PMSCR_EL1);
    	
    	isb();
	pr_debug("SPE engine started on CPU %d\n",smp_processor_id());
}

static void spe_disable_cpu(void *info)
{
    /* Disable SPE */
    u64 reg;
    u64 pmblimitr;
    struct spe_ctrl* spe_ctrl = (struct spe_ctrl*)info;

    disable_percpu_irq(spe_ctrl->irq);

    pmblimitr = read_sysreg_s(SYS_PMBLIMITR_EL1);
    write_sysreg_s(0, SYS_PMBLIMITR_EL1);
    write_sysreg_s(read_sysreg_s(SYS_PMSCR_EL1) & ~1, SYS_PMSCR_EL1);
    isb();
    reg = read_sysreg_s(SYS_PMBSR_EL1);
    PR_DEBUG("PMBSR_EL1 status: %llx\n",reg);
    reg = read_sysreg_s(SYS_PMBPTR_EL1);
    PR_DEBUG("SYS_PMBPTR_EL1 status: %llx\n", reg);
    PR_DEBUG("SYS_PMBLIMITR_EL1 status: %llx\n", pmblimitr);
    PR_DEBUG("The IRQ had been called %d times", irq_called);
}


static int spe_reader(void *data)
{
	struct spe_ctrl *s = data;
	int ret;
	unsigned int cpu;
	u64 ones_mask = ~0x0; // This should make an all ones mask
	bool at_least_one_reading = true;
	unsigned long long ts_before_process, ts_after_process;

	pr_info("spe_reader(): Starting on CPU %d\n", smp_processor_id());


	while (at_least_one_reading || !kthread_should_stop() ) {
		at_least_one_reading = false;
		
		for_each_cpu(cpu, s->target_cpu){
			struct core_info * cinfo = per_cpu_ptr(core_info,cpu);
			/*
			 * spe_buf::limit can change out of the control of 
			 * current thread's control.
			*/
			void *limit = READ_ONCE(cinfo->primary.limit);
			//TODO: evaluate this barrier, maybe it can be removed
			smp_rmb();
			if (cinfo->primary.buf < limit && 
				READ_ONCE(
					*(u64 *)(cinfo->primary.buf + s->advance)
				) 
				!= ones_mask) 
			{

				pr_debug("SPE record at %llx\n",(u64) cinfo->primary.buf);
#if CONFIG_PERF_OPTIMISED < 2
				dsb(ish);
				isb();
				ts_before_process = READ_CNTCPT_EL0();
				dsb(ish);
				isb();
#endif
				ret = process_record(cinfo->primary.buf);
				/*
				* Can be removed because depending on last_ts, 
				* that is modified as last step inside 
				* process_record()
				*/
#if CONFIG_PERF_OPTIMISED < 2
				dsb(ish);
				isb();
#endif
				if(ret < 0){
					PR_DEBUG("Unrecognised packet header: %x", (unsigned int)(-ret));
				} else {
					cinfo->primary.buf += 64UL;
#if CONFIG_PERF_OPTIMISED < 2
					read_records++;
					ts_after_process = READ_CNTCPT_EL0();
					ts_stats_arr[read_records%NUM_STATS_RECORDS].spe_reader_delay = 
						ts_before_process - last_ts ;
					ts_stats_arr[read_records%NUM_STATS_RECORDS].reader_process_delay = 
						ts_after_process - ts_before_process ;
					timestamp_delta = timestamp_delta 
								+ (ts_before_process - last_ts);
#endif
					//With the equal comparison, it will only trigger once
					if (cinfo->primary.buf == cinfo->primary.watermark ){
						pr_debug("spe_reader():watermark hit\n");
						if(0 != smp_call_function_single(cpu,
							spe_bw_manage_buffer,
							s, 0))
						{				
							pr_warn("error in calling " 
								"spe_bw_manage_buffer");
						}	
						
					}
				}
			}
		
			// Restore a waiting buffer, if the secondary is ready
			if(cinfo->primary.buf == limit && 
				READ_ONCE(cinfo->secondary.ready))
			{
				cinfo->active_buffer = OTHER_BUFFER(cinfo->active_buffer);
				SWAP(cinfo->primary.buf,cinfo->secondary.buf);
				SWAP(cinfo->primary.limit,cinfo->secondary.limit);
				SWAP(cinfo->primary.watermark,cinfo->secondary.watermark);
				smp_wmb();
				cinfo->secondary.ready = 0;
				// cinfo->primary
				// s->buf = s->secondary_buf;
				// s->limit = s->secondary_limit;
				// s->water_mark = s->secondary_watermark;
			}
		}
		cpu_relax();
	}
	pr_info("spe_reader(): stopped on CPU %d\n", smp_processor_id());
	return 0;
}

#if CONFIG_PERF_OPTIMISED < 2
static void compute_stats(unsigned long long *read_delay_avg,
				unsigned long long *read_delay_err,
				unsigned long long *proc_delay_avg,
				unsigned long long *proc_delay_err)
{
	unsigned int i;
	unsigned long long effective_saved_stats = min(read_records,
							(unsigned long long)NUM_STATS_RECORDS);

	*read_delay_avg = *read_delay_err = *proc_delay_avg = *proc_delay_err = 0;
	for(i=0; i<effective_saved_stats; i++){
		*read_delay_avg += ts_stats_arr[i].spe_reader_delay;
		*proc_delay_avg += ts_stats_arr[i].reader_process_delay;
		PR_DEBUG("%d\n",ts_stats_arr[i].spe_reader_delay);
	}
	*read_delay_avg = *read_delay_avg / effective_saved_stats;
	*proc_delay_avg = *proc_delay_avg / effective_saved_stats;

	for(i=0; i<effective_saved_stats; i++){
		*read_delay_err += abs(*read_delay_avg - ts_stats_arr[i].spe_reader_delay);
		*proc_delay_err += abs(*proc_delay_avg - ts_stats_arr[i].reader_process_delay);
	}
	*read_delay_err = *read_delay_err / effective_saved_stats;
	*proc_delay_err = *proc_delay_err / effective_saved_stats;
}
#endif 

static ssize_t sysfs_store_control(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{	
	// Resetting extended packets counter;
	bool alloc_failed = false;
	unsigned int i;
	extended_packets = 0;
	if (sysfs_streq(buf, "start")) {
		if (spe.running)
			return count;
		timestamp_delta = 0;
		read_records = 0;
		precess_record_time = 0;
		first_ts = 0;
		
		spe.size = BUFFER_SIZE;
		for_each_cpu(i, spe.target_cpu){
			struct core_info *cinfo = per_cpu_ptr(core_info,i);
			// Allocate 2 buffers of size BUFFER_SIZE per core
			cinfo->buffer_base = kmalloc(spe.size * 2, GFP_KERNEL);
			if(!cinfo->buffer_base)
				alloc_failed = true;
			else
				memset(cinfo->buffer_base, 0xff, spe.size * 2);
			//TODO: This will be refactored to be more concise
			cinfo->primary = (struct spe_buffer){
				.buf = cinfo->buffer_base,
				.limit = cinfo->buffer_base + spe.size,
				.watermark = cinfo->buffer_base + 
					(
						spe.size
						*WATERMARK_DEN
						/WATERMARK_DEN
					),
				.ready = 0
			};
			cinfo->secondary = (struct spe_buffer){
				.buf = cinfo->buffer_base + spe.size,
				.limit = cinfo->buffer_base + 2 * spe.size,
				.watermark = cinfo->buffer_base 
					+ spe.size
					+(
						spe.size
						*WATERMARK_DEN
						/WATERMARK_DEN
					),
				.ready = 0
			};

			cinfo->active_buffer = 0;
		}
		if(alloc_failed){
			for_each_cpu(i, spe.target_cpu){
				struct core_info *cinfo = per_cpu_ptr(core_info,i);
				kfree(cinfo-> buffer_base);
				cinfo-> buffer_base = NULL;
			}
			return -ENOMEM;
		}
		
		smp_wmb();
		memset(spe_bw_management_called,0,sizeof(spe_bw_management_called));
		/* Start reader thread pinned */
		spe.reader_task =
			kthread_create(spe_reader, &spe, "spe_reader");
		if(!spe.reader_task){
			return -ENOMEM;
		}

		kthread_bind(spe.reader_task, spe.reader_cpu);

		wake_up_process(spe.reader_task);

		/* Enable SPE on target CPU */
		// TODO: remove wait true and change with semaphore
		for_each_cpu(i, spe.target_cpu){
			smp_call_function_single(i,
					spe_enable_cpu,
					&spe, 1);
		}
		
		spe.running = true;
		pr_info("SPE started\n");
	}

	else if (sysfs_streq(buf, "stop")) {

		if (!spe.running)
			return count;

		for_each_cpu(i, spe.target_cpu){
			smp_call_function_single(i, spe_disable_cpu,
					&spe, 1);
		}

		if (spe.reader_task)
			kthread_stop(spe.reader_task);

		for_each_cpu(i, spe.target_cpu){
			struct core_info *cinfo = per_cpu_ptr(core_info,i);
			kfree(cinfo-> buffer_base);
			cinfo-> buffer_base = NULL;
		}

		spe.running = false;

		pr_info("sysfs_store_control: SPE stopped\n");
		pr_debug("sysfs_store_control: Found %d extended packets\n",extended_packets);	
		pr_debug("First TS seen: %lld, last TS seen: %lld\n", first_ts,last_ts);
#if CONFIG_PERF_OPTIMISED < 2
		pr_info("Average timestamp drift: %lld\n", timestamp_delta/read_records);
		pr_info("Total records analysed: %lld\n",read_records);
		{
			unsigned long long read_delay_avg = 0;
			unsigned long long read_delay_err = 0;
			unsigned long long proc_delay_avg = 0;
			unsigned long long proc_delay_err = 0;
			compute_stats( &read_delay_avg, &read_delay_err,
					&proc_delay_avg, &proc_delay_err);
			pr_info("Average delay in reception: %lld +- %lld\n", 
				read_delay_avg, read_delay_err);
			pr_info("Average delay in processing: %lld +- %lld\n", 
				proc_delay_avg, proc_delay_err);
		}
#endif
	}

	return count;
}

static ssize_t sysfs_store_target_cpu(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	int ret;
	if((ret = cpumask_parse(buf, spe.target_cpu)) < 0)
		return ret;
	return count;
}

static ssize_t sysfs_store_reader_cpu(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
    if(kstrtoint(buf, 10, &spe.reader_cpu) < 0)
	return 0;
    return count;
}

static int spe_bw_init_debugfs(void){
	//TODO: to add some files here
	return 0;
}

/* IRQ handling */
static enum spe_bw_buf_fault_action
spe_bw_buf_fault_act_get(struct core_info *cinfo)
{
	const char *err_str;
	u64 pmbsr;
	enum spe_bw_buf_fault_action ret;

	/*
	 * Ensure new profiling data is visible to the CPU and any external
	 * aborts have been resolved.
	 */
	psb_csync();
	dsb(nsh);

	/* Ensure hardware updates to PMBPTR_EL1 are visible */
	isb();

	/* Service required? */
	pmbsr = read_sysreg_s(SYS_PMBSR_EL1);
	pr_info("spe_bw: irq_handler PMBSR_EL1: %8llx", pmbsr);
	if (!(pmbsr & BIT(SYS_PMBSR_EL1_S_SHIFT)))
		return SPE_BW_BUF_FAULT_ACT_SPURIOUS;

	/*
	 * If we've lost data, disable profiling and also set the PARTIAL
	 * flag to indicate that the last record is corrupted.
	 */
	if (pmbsr & BIT(SYS_PMBSR_EL1_DL_SHIFT)){
		//TODO: Manage it, may just cancel align backwards the PMBPTR
	}
	/* Report collisions to userspace so that it can up the period */
	if (pmbsr & BIT(SYS_PMBSR_EL1_COLL_SHIFT)){
		//TODO: We may don't care this
	}

	/* We only expect buffer management events */
	switch (pmbsr & (SYS_PMBSR_EL1_EC_MASK << SYS_PMBSR_EL1_EC_SHIFT)) {
	case SYS_PMBSR_EL1_EC_BUF:
		/* Handled below */
		break;
	case SYS_PMBSR_EL1_EC_FAULT_S1:
	case SYS_PMBSR_EL1_EC_FAULT_S2:
		err_str = "Unexpected buffer fault";
		goto out_err;
	default:
		err_str = "Unknown error code";
		goto out_err;
	}

	/* Buffer management event */
	switch (pmbsr &
		(SYS_PMBSR_EL1_BUF_BSC_MASK << SYS_PMBSR_EL1_BUF_BSC_SHIFT)) {
	case SYS_PMBSR_EL1_BUF_BSC_FULL:
		ret = SPE_BW_BUF_FAULT_ACT_OK;
		goto out_stop;
	default:
		err_str = "Unknown buffer status code";
	}

out_err:
	pr_err_ratelimited("%s on CPU %d [PMBSR=0x%016llx, PMBPTR=0x%016llx, PMBLIMITR=0x%016llx]\n",
			   err_str, smp_processor_id(), pmbsr,
			   read_sysreg_s(SYS_PMBPTR_EL1),
			   read_sysreg_s(SYS_PMBLIMITR_EL1));
	ret = SPE_BW_BUF_FAULT_ACT_FATAL;

out_stop:
	/*
	 * TODO: Fix this as I don't like calling spe_disable_cpu from 2 
	 * different places
	 */
	spe_disable_cpu(&spe);
	if (spe.reader_task)
		kthread_stop(spe.reader_task);

	/*
	 * TODO: We just stopped the thread, so maybe freeing 
	 * already the buffer is too early
	*/
	kfree(cinfo->buffer_base);
	cinfo-> buffer_base = NULL;

	spe.running = false;
	return ret;
}


static irqreturn_t spe_bw_irq_handler(int irq, void *dev)
{
	struct core_info *cinfo = this_cpu_ptr(core_info);
	
	enum spe_bw_buf_fault_action act;
	if (!cinfo->buffer_base)
		return IRQ_NONE;
	
	pr_info("Irq called\n");
	act = spe_bw_buf_fault_act_get(cinfo);
	if (act == SPE_BW_BUF_FAULT_ACT_SPURIOUS)
		return IRQ_NONE;
	irq_called ++;
	switch (act) {
	case SPE_BW_BUF_FAULT_ACT_FATAL:
		/*
		 * If a fatal exception occurred then leaving the profiling
		 * buffer enabled is a recipe waiting to happen. Since
		 * fatal faults don't always imply truncation, make sure
		 * that the profiling buffer is disabled explicitly before
		 * clearing the syndrome register.
		 */
		spe_bw_disable_and_drain_local();
		break;
	case SPE_BW_BUF_FAULT_ACT_OK:
		/*
		 * We handled the fault (the buffer was full), so resume
		 * profiling as long as we didn't detect truncation.
		 * PMBPTR might be misaligned, but we'll burn that bridge
		 * when we get to it.
		 */
		//TODO: We might need to reset in some way the buffer
		// if (!(handle->aux_flags & PERF_AUX_FLAG_TRUNCATED)) {
		// 	arm_spe_perf_aux_output_begin(handle, event);
		// 	isb();
		// }
		break;
	case SPE_BW_BUF_FAULT_ACT_SPURIOUS:
		/* We've seen you before, but GCC has the memory of a sieve. */
		break;
	}

	/* The buffer pointers are now sane, so resume profiling. */
	write_sysreg_s(0, SYS_PMBSR_EL1);
	return IRQ_HANDLED;
}

static int spe_pm_callback(struct notifier_block *nb,
                           unsigned long action,
                           void *data)
{
    switch (action) {

    case CPU_PM_ENTER:
        PR_DEBUG("CPU %d going sleep.\n", smp_processor_id());
        break;

    case CPU_PM_EXIT:
        /* CPU resumed from low power state */
        PR_DEBUG("CPU %d waking up.\n", smp_processor_id());
        break;
    }

    return NOTIFY_OK;
}

static int spe_bw_device_probe(struct platform_device *pdev)
{
	int ret;
	struct spe_ctrl *spe_ctrl = &spe;
	//struct device *dev = &pdev->dev;

	pr_info("Hello from probe\n");
	// spe_pmu = devm_kzalloc(dev, sizeof(*spe_pmu), GFP_KERNEL);
	// if (!spe_pmu) {
	// 	dev_err(dev, "failed to allocate spe_pmu\n");
	// 	return -ENOMEM;
	// }

	// spe_pmu->handle = alloc_percpu(typeof(*spe_pmu->handle));
	// if (!spe_pmu->handle)
	// 	return -ENOMEM;

	spe_ctrl->pdev = pdev;
	/*
	 * Only needed if allocating stuff inside probe, as from remove we 
	 * would not have it 
	 */
	platform_set_drvdata(pdev, spe_ctrl);

	ret = spe_bw_irq_probe(spe_ctrl);
	if (ret)
		goto out_free_handle;


	/* Request our PPIs (note that the IRQ is still disabled) */
	ret = request_percpu_irq(spe_ctrl->irq, spe_bw_irq_handler, "spe_bw",
				 spe_ctrl);
	if (ret)
		return ret;

	// ret = arm_spe_pmu_dev_init(spe_pmu);
	// if (ret)
	// 	goto out_free_handle;

	return 0;

//out_teardown_dev:
	// Again, only when will allocate here 
	//arm_spe_pmu_dev_teardown(spe_pmu);
out_free_handle:
	//Again, only when will allocate per cpu
	//free_percpu(spe_ctrl->handle);
	return ret;
}

static int spe_bw_device_remove(struct platform_device *pdev)
{
	struct spe_ctrl *spe_ctrl = platform_get_drvdata(pdev);
	pr_info("Hello from remove\n");
	//arm_spe_pmu_perf_destroy(spe_ctrl);
	//arm_spe_pmu_dev_teardown(spe_ctrl);
	free_percpu_irq(spe_ctrl->irq, spe_ctrl);
	//free_percpu(spe_ctrl->handle);
	return 0;
}

static const struct of_device_id spe_bw_of_match[] = {
	{ .compatible = "arm,statistical-profiling-extension-v1", .data = (void *)1 },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, spe_bw_of_match);

static struct platform_driver spe_bw_driver = {
	.driver	= {
		.name		= "spe_bw",
		.of_match_table	= of_match_ptr(spe_bw_of_match),
		.suppress_bind_attrs = true,
	},
	.probe	= spe_bw_device_probe,
	// No dynamic allocation to free
	.remove	= spe_bw_device_remove, 
};

static struct notifier_block spe_pm_nb = {
    .notifier_call = spe_pm_callback,
};

static void enable_el0_cntpct(void *data){
	u64 reg = read_sysreg_s(SYS_CNTKCTL_EL1);
	pr_debug("CPU %d:: SYS_CNTKCTL_EL1: %llx\n",smp_processor_id(), reg);
	// Setting EL0PCTEN bit
	reg |= BIT(0);
	write_sysreg_s(reg, SYS_CNTKCTL_EL1); 
}

// Initialization function (called when the module is loaded)
static int __init spe_guard_init(void)
{
	struct spe_ctrl *global = &spe;
	u64 reg;
	u64 reg_after;
	int fld;
	int interval;
	int max_record_sz;
	int counter_sz;
	int ret;
	u16 pmsver;

	fld = cpuid_feature_extract_unsigned_field(read_cpuid(ID_AA64DFR0_EL1),
						ID_AA64DFR0_PMSVER_SHIFT);
	pmsver = (u16)fld;
	pr_info("Probe spe v1.%d\n", pmsver-1);

	//reg = read_sysreg_s(SYS_CPUECTLR_EL1);
	__asm__ volatile ("mrs %0, S3_0_C15_C1_4": "=r"(reg)::);
	pr_debug("CPUECTLR_EL1=%llx/n",reg);

	reg = read_sysreg_s(SYS_CNTFRQ_EL0);
	pr_debug("CNTFRQ_EL0=%llu/n",reg);
	pr_debug("ID_AA64DFR2_EL1=0x%llx",read_sysreg_s(ID_AA64DFR2_EL1));
	pr_debug("Sleeping 100ms...");
	//reg = read_sysreg_s(cntpct_el0);
	asm volatile("mrs %0, cntpct_el0" : "=r" (reg));
	mdelay(100);
	//reg = read_sysreg_s(SYS_CNTPCT_EL0) - reg;
	asm volatile("mrs %0, cntpct_el0" : "=r" (reg_after));
	pr_debug("Slept for %llu clocks", reg_after - reg);
	
	reg = read_sysreg_s(SYS_PMBIDR_EL1);
	pr_debug("SYS_PMBIDR_EL1=%08llx/n",reg);
	reg = read_sysreg_s(SYS_PMBIDR_EL1);
	if (reg & BIT(SYS_PMBIDR_EL1_P_SHIFT)) {
		pr_warn("profiling buffer owned by higher exception level\n");
	}
	else {
		pr_debug("profiling available\n");
	}

	/* Minimum alignment. If it's out-of-range, then fail the probe */
	fld = reg >> SYS_PMBIDR_EL1_ALIGN_SHIFT & SYS_PMBIDR_EL1_ALIGN_MASK;
	pr_debug("Minimum alignment: %d\n", 1 << fld);

	/* It's now safe to read PMSIDR and figure out what we've got */
	pr_debug("Reading PMSIDR\n");
	reg = read_sysreg_s(SYS_PMSIDR_EL1);
	pr_debug("Entire PMSIDR: 0x%08llx\n", reg);

	if (BIT_GET(SYS_PMSIDR_EL1_FE, reg))
		pr_debug("FE bit set\n");

	// if (BIT_GET(SYS_PMSIDR_EL1_FnE, reg))
	// 	pr_info("FnE bit set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_FT, reg))
		pr_debug("FT bit set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_FL, reg))
		pr_debug("FL bit set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_ARCHINST, reg))
		pr_debug("ARCHINST bit set\n");
	else
			pr_debug("ARCHINST bit not set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_LDS, reg))
		pr_debug("LDS bit set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_ERND, reg))
		pr_debug("ERND bit set\n");
	else
		pr_debug("ERND bit not set\n");

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
	pr_debug("Interval=%d\n",interval);

	/* Maximum record size. If it's out-of-range, then fail the probe */

	fld = FIELD_GET_LOCAL(SYS_PMSIDR_EL1_MAXSIZE, reg);
	max_record_sz = 1 << fld;
	pr_debug("Max record size: %d\n", max_record_sz);


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
	pr_debug("Countsize is %d\n",counter_sz);

	for_each_cpu(interval, cpu_possible_mask){
		smp_call_function_single(interval,
				enable_el0_cntpct,
				NULL, 1);
	}

	memset(global, 0, sizeof(struct spe_ctrl));
	zalloc_cpumask_var(&global->target_cpu, GFP_NOWAIT);
	ts_stats_arr = vmalloc(sizeof(struct ts_stats)*NUM_STATS_RECORDS);
	if(!ts_stats_arr){
		return -ENOMEM;
	}
		
	global->filter_ld = true;
	global->filter_st = false;
	global->ts_enable = true;
	global->pa_enable = true;
	global->pct_enable = true;
	global->exclude_user = false;
	global->exclude_kernel = false;
	global->cx_enable = true;
	
	global->period = SPE_BW_PERIOD;
	global->advance = 0;

	core_info = alloc_percpu(struct core_info);

	cpu_pm_register_notifier(&spe_pm_nb);

	spe_kobj = kobject_create_and_add("spe_regulator", kernel_kobj);
	if (!spe_kobj){
		ret = -ENOMEM;
		goto failed_kobj;
	}

	if(sysfs_create_file(spe_kobj, &control_attr.attr)<0){
		ret = -ENOMEM;
		goto failed_file_creation;
	}
	if(sysfs_create_file(spe_kobj, &target_cpu_attr.attr)<0){
		ret = -ENOMEM;
		goto failed_file_creation;
	}
	if(sysfs_create_file(spe_kobj, &reader_cpu_attr.attr)<0){
		ret = -ENOMEM;
		goto failed_file_creation;
	}

	spe_bw_init_debugfs();
	
	ret = platform_driver_register(&spe_bw_driver);
	if(ret)
		return ret;
	
	return 0;  // Return 0 means success
	//platform_driver_unregister(&spe_bw_driver);
	failed_file_creation:
	sysfs_remove_file(spe_kobj, &control_attr.attr);
	sysfs_remove_file(spe_kobj, &target_cpu_attr.attr);
	sysfs_remove_file(spe_kobj, &reader_cpu_attr.attr);
	failed_kobj:

	kobject_put(spe_kobj);
	return ret;
}

// Exit function (called when the module is removed)
static void __exit spe_guard_exit(void)
{
	platform_driver_unregister(&spe_bw_driver);
	sysfs_remove_file(spe_kobj, &control_attr.attr);
	sysfs_remove_file(spe_kobj, &target_cpu_attr.attr);
	sysfs_remove_file(spe_kobj, &reader_cpu_attr.attr);
	kobject_put(spe_kobj);

	cpu_pm_unregister_notifier(&spe_pm_nb);
	printk(KERN_INFO "Goodbye, world!\n");
}

// Register the functions
module_init(spe_guard_init);
module_exit(spe_guard_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SPE based Memeguard Implementation");
MODULE_AUTHOR("Alessandro Mandrile");