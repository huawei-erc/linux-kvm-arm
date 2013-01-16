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
	//struct semaphore sem;
	struct cdev cdev;
};



#define VCPU_HP_MAX_CPUS 256
#define VCPU_HP_MASK_SIZE (VCPU_HP_MAX_CPUS / BITS_PER_BYTE)

/* VCPU HP header bytes offsets */
enum vcpu_hp_header {
    VCPU_HP_HEADER_MASK_SZ = 0,
    VCPU_HP_HEADER_CTRL = 1,

    VCPU_HP_HEADER_RES2 = 2,
    VCPU_HP_HEADER_RES3 = 3,
    VCPU_HP_HEADER_RES4 = 4,
    VCPU_HP_HEADER_RES5 = 5,
    VCPU_HP_HEADER_RES6 = 6,
    VCPU_HP_HEADER_RES7 = 7,

    VCPU_HP_HEADER_N
};

/* VCPU HP control byte bit offsets */
/* note that a Hotplug is pending just before firing the IRQ,
   up until the hotplug is completed in the guest and confirmed.
   The Interrupt is Pending just until the guest reaches the ISR,
   at which point it will acknowledge by writing 0 to the IPR. */
enum vcpu_hp_ctrl {
    VCPU_HP_CTRL_IPR = 0, /* Interrupt Pending Register */
    VCPU_HP_CTRL_HPR = 1, /* Hotplug Pending Register */

    VCPU_HP_CTRL_RES2 = 2,
    VCPU_HP_CTRL_RES3 = 3,
    VCPU_HP_CTRL_RES4 = 4,
    VCPU_HP_CTRL_RES5 = 5,
    VCPU_HP_CTRL_RES6 = 6,
    VCPU_HP_CTRL_RES7 = 7,

    VCPU_HP_CTRL_N
};
/* CTRLreg helpers */
static int vcpu_hp_get_creg(unsigned char *ctrl, enum vcpu_hp_ctrl reg)
{
    return *ctrl & (1 << reg) ? 1 : 0;
}

static void vcpu_hp_set_creg(unsigned char *ctrl, enum vcpu_hp_ctrl reg)
{
    *ctrl |= (1 << reg);
}

static void vcpu_hp_clear_creg(unsigned char *ctrl, enum vcpu_hp_ctrl reg)
{
    *ctrl &= ~(1 << reg);
}




