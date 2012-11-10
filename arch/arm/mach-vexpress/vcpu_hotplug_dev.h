#define VCPU_HOTPLUG_DEVICE_MINOR_START 0
#define VCPU_HOTPLUG_DEVICE_COUNT 1
#define VCPU_HOTPLUG_DEVICE_NAME "vcpu_hotplug_dev"
#define VCPU_HOTPLUG_DEVICE_BASE 0x12000000

struct vcpu_hotplug_dev {
	struct resource *phy_base_addr;
	void __iomem *virt_base_addr;
	unsigned int irq;
	char *buffer;
	unsigned long size;
	struct semaphore sem;
	struct cdev cdev;
};
