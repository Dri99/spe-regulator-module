ARCH=arm64 
CROSS_COMPILE=aarch64-buildroot-linux-gnu-
TEGRA_KERNEL_OUT=/home/ubuntuale/Projects/spe_regulator/Linux_for_Tegra/out_kernel
MAKE_ARGS=ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)
#ccflags-y+=-DDEBUG

obj-m += spe_guard.o
spe_guard-objs := spe_ring.o spe_guard_core.o gic-v3-dump.o

all:
	$(MAKE) $(MAKE_ARGS) -C $(TEGRA_KERNEL_OUT) M=$(PWD) modules

clean:
	$(MAKE) $(MAKE_ARGS) -C $(TEGRA_KERNEL_OUT) M=$(PWD) clean

