/* Generic GPIO driver for LEDs connected to Intel ICH7 (as used in various NAS etc)
 * Copyright (c) 2010 John Cronin
 *
 * Based in part on SE4200-E Hardware API by Dave Hansen
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Author: John Cronin <johncronin@scifa.co.uk>
 */


#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>

MODULE_AUTHOR("John Cronin <johncronin@scifa.co.uk>");
MODULE_DESCRIPTION("ICH7 GPIO LED Driver");
MODULE_LICENSE("GPL");

#define DRIVER_NAME (THIS_MODULE->name)
#define PROCFS_NAME "ich7_gpio"

// ICH7 LPC/GPIO PCI Config register offsets
#define GPIO_BASE	0x048
#define GPIO_CTRL	0x04c
#define GPIO_EN		0x010

// The ICH7 GPIO register block is 64 bytes in size
#define ICH7_GPIO_SIZE	64

// THe ICH7 GPIO register block is pointed to by the address in GPIO_BASE
//
// Within it, various registers are defined by the intel ICH docs (chap 10.10)
// Define those here

#define GPIO_USE_SEL	0x00
#define GP_IO_SEL	0x04
#define GP_LVL		0x0c
#define GPO_BLINK	0x18
#define GPI_INV		0x2c
#define GPIO_USE_SEL2	0x30
#define GP_IO_SEL2	0x34
#define GP_LVL2		0x38

// Spinlock to protect access to gpio ports
static DEFINE_SPINLOCK(ich7_gpio_lock);

// PCI IDs to search for
static struct pci_device_id ich7_lpc_pci_id[] =
{
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_1) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_30) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_31) },
	{ }
};

MODULE_DEVICE_TABLE(pci, ich7_lpc_pci_id);

// The PCI device found
static struct pci_dev *nas_gpio_pci_dev;

// Base IO address assigned to the ICH7 GPIO register block
static u32 nas_gpio_io_base;

// Store the ioport region we are given so we can release it on exit
static struct resource *gp_gpio_resource;

// A structure for storing register assignments
struct ich7_register {
	char *name;
	u32 base_reg;
	u32 offset;
};

static struct ich7_register ich7_registers[] = {
	{ .name = "GP_LVL",			.base_reg = GPIO_BASE,		.offset = GP_LVL },
	{ .name = "GPIO_USE_SEL",	.base_reg = GPIO_BASE,		.offset = GPIO_USE_SEL },
	{ .name = "GP_IO_SEL",		.base_reg = GPIO_BASE,		.offset = GP_IO_SEL },
	{ .name = "GPO_BLINK",		.base_reg = GPIO_BASE,		.offset = GPO_BLINK },
	{ .name = "GP_LVL2",		.base_reg = GPIO_BASE,		.offset = GP_LVL2 },
	{ .name = "GPIO_USE_SEL2",	.base_reg = GPIO_BASE,		.offset = GPIO_USE_SEL2 },
	{ .name = "GP_IO_SEL2",		.base_reg = GPIO_BASE,		.offset = GP_IO_SEL2 },
	{ .name = "GPI_INV",		.base_reg = GPIO_BASE,		.offset = GPI_INV },
};

// A structure for storing led commands
struct ich7_led_command {
	struct ich7_register *reg;
	int bitno;
	int val_or_invert;
};

#define LED_NAME_LEN		31
#define LED_COLOR_LEN		31
#define LED_FULLNAME_LEN	95
#define FUNC_LEN			4

#define LED_FUNC_NULL			0
#define LED_FUNC_BRIGHT0		1
#define LED_FUNC_BRIGHT1		2
#define LED_FUNC_BLINK_ON		3
#define LED_FUNC_GET_BRIGHT		5
#define LED_FUNC_GET_BLINK		6

// A structure for storing led info
struct ich7_led {
	char name[LED_NAME_LEN + 1];
	char color[LED_COLOR_LEN + 1];

	char fullname[LED_FULLNAME_LEN + 1];

	int registered;
	struct led_classdev led_cdev;

	struct ich7_led_command set_brightness_0[FUNC_LEN];
	struct ich7_led_command set_brightness_1[FUNC_LEN];
	struct ich7_led_command set_blink_on[FUNC_LEN];
	struct ich7_led_command get_brightness[FUNC_LEN];
	struct ich7_led_command get_blink[FUNC_LEN];
};

#define LED_MAX			16
struct ich7_led ich7_leds[LED_MAX];

static u32 get_reg_port(struct ich7_register *reg)
{
	if(reg == NULL)
		return -1;
	switch(reg->base_reg)
	{
	case GPIO_BASE:
		return nas_gpio_io_base + reg->offset;
	default:
		return -1;
	}
}

static u32 read_reg(struct ich7_register *reg)
{
	u32 port = get_reg_port(reg);
	u32 ret;
	if(port == -1)
		return 0x0;
	spin_lock(&ich7_gpio_lock);
	ret = inl(port);
	spin_unlock(&ich7_gpio_lock);
	return ret;
}

static void write_reg(struct ich7_register *reg, u32 value)
{
	u32 port = get_reg_port(reg);
	if(port == -1)
		return;
#ifdef DEBUG
	printk("Writing 0x%08x to 0x%04x\n", value, port);
#endif
	spin_lock(&ich7_gpio_lock);
	outl(value, port);
	spin_unlock(&ich7_gpio_lock);
}

static void set_reg_bit(struct ich7_register *reg, int bitno, int val)
{
	u32 in_val;
	u32 port = get_reg_port(reg);

#ifdef DEBUG
	printk("Setting reg %s bit %i to %i\n", reg->name, bitno, val);
#endif

	if((bitno < 0) || (bitno >= 32))
		return;
	if((val < 0) || (val >= 2))
		return;
	if(port == -1)
		return;
	
	spin_lock(&ich7_gpio_lock);
	in_val = inl(port);
	if(val)
		in_val |= (1 << bitno);
	else
		in_val &= ~(1 << bitno);
	outl(in_val, port);
	spin_unlock(&ich7_gpio_lock);
}

static u32 get_reg_bit(struct ich7_register *reg, int bitno)
{
	u32 in_val;

	if((bitno < 0) || (bitno >= 32))
		return -1;

	in_val = read_reg(reg);
	if(in_val & (1 << bitno))
		return 1;
	else
		return 0;
}

static struct ich7_register *get_reg_by_name(const char *name)
{
	int i;
	for(i = 0; i < ARRAY_SIZE(ich7_registers); i++) {
		if(!strcmp(name, ich7_registers[i].name))
		{
#ifdef DEBUG
			printk("Found reg %s\n", name);
#endif
			return &ich7_registers[i];
		}
	}
	return NULL;
}

// led stuff
static struct ich7_led *get_led_by_name(const char *name)
{
	int i;
	if(name == NULL)
		return NULL;
	if(name[0] == '\0')
		return NULL;
	for(i = 0; i < ARRAY_SIZE(ich7_leds); i++) {
		if(!strcmp(name, ich7_leds[i].name))
			return &ich7_leds[i];
	}
	return NULL;
}

static struct ich7_led *get_next_free_led(void)
{
	int i;
	for(i = 0; i < ARRAY_SIZE(ich7_leds); i++) {
		if(ich7_leds[i].name[0] == '\0') {
			sprintf(ich7_leds[i].name, "Unnamed");
			return &ich7_leds[i];
		}
	}
	return NULL;
}

static void delete_cmd(struct ich7_led_command *table)
{
	int i;
	for(i = 0; i < FUNC_LEN; i++)
	{
		memset(&table[i], 0, sizeof(struct ich7_led_command));
		table[i].reg = NULL;
	}
}

static void unregister_led(struct ich7_led *led)
{
	if(led->registered)
		led_classdev_unregister(&led->led_cdev);
}

static void delete_led(struct ich7_led *led)
{
	if(led == NULL)
		return;
	unregister_led(led);
	memset(led, 0, sizeof(struct ich7_led));
	delete_cmd(led->set_brightness_0);
	delete_cmd(led->set_brightness_1);
	delete_cmd(led->set_blink_on);
	delete_cmd(led->get_brightness);
	delete_cmd(led->get_blink);
}

static void init_leds(void)
{
	int i;
	for(i = 0; i < ARRAY_SIZE(ich7_leds); i++)
		delete_led(&ich7_leds[i]);
}

static int get_led_func_by_name(const char *name)
{
	if(!strcmp(name, "bright_0"))
		return LED_FUNC_BRIGHT0;
	else if(!strcmp(name, "bright_1"))
		return LED_FUNC_BRIGHT1;
	else if(!strcmp(name, "blink_on"))
		return LED_FUNC_BLINK_ON;
	else if(!strcmp(name, "get_bright"))
		return LED_FUNC_GET_BRIGHT;
	else if(!strcmp(name, "get_blink"))
		return LED_FUNC_GET_BLINK;
	else
		return LED_FUNC_NULL;
}

static struct ich7_led_command *get_led_func_table(struct ich7_led *led, int func_id)
{
	if(led == NULL)
		return NULL;

	switch(func_id)
	{
	case LED_FUNC_BRIGHT0:
		return &led->set_brightness_0[0];
	case LED_FUNC_BRIGHT1:
		return &led->set_brightness_1[0];
	case LED_FUNC_BLINK_ON:
		return &led->set_blink_on[0];
	case LED_FUNC_GET_BRIGHT:
		return &led->get_brightness[0];
	case LED_FUNC_GET_BLINK:
		return &led->get_blink[0];
	}
	return NULL;
}

static struct ich7_led_command *get_next_free_command(struct ich7_led_command *table)
{
	int i;
	for(i = 0; i < FUNC_LEN; i++)
	{
		if(table[i].reg == NULL)
			return &table[i];
	}
	return NULL;
}

static void led_runcmd(struct ich7_led_command *cmd)
{
	int i;
	for(i = 0; i < FUNC_LEN; i++)
	{
		if(cmd[i].reg == NULL)
			return;
		set_reg_bit(cmd[i].reg, cmd[i].bitno, cmd[i].val_or_invert);
	}
}

static int led_rungetcmd(struct ich7_led_command *cmd)
{
	u32 val;
	if(cmd[0].reg == NULL)
		return 0;
	val = get_reg_bit(cmd[0].reg, cmd[0].bitno);
	if(((val == 0) && (cmd[0].val_or_invert == 0)) || ((val == 1) && (cmd[0].val_or_invert == 1)))
		return 0;
	return 1;
}

static void led_cdev_set_brightness(struct led_classdev *led_cdev,
	enum led_brightness brightness)
{
	struct ich7_led *led = container_of(led_cdev, struct ich7_led, led_cdev);
	if(brightness >= LED_HALF)
		led_runcmd(led->set_brightness_1);
	else
		led_runcmd(led->set_brightness_0);
}

// Tokenize a string
static int token(char *src, char **out, int out_size)
{
	int count, i, len;
	int start_string_with_next = 1;
	len = strlen(src);

	for(count = 0, i = 0; ((i < len) && (count < out_size)); src++, i++)
	{
		if(start_string_with_next)
		{
			out[count] = src;
			start_string_with_next = 0;
			count++;
		}

		if(((*src == ' ') || (*src == '\n')) && (count < out_size))
		{
			*src = '\0';
			start_string_with_next = 1;
		}
	}

	return count;
}

// Proc file support stuff
int procfile_read(char *buffer, char **buffer_location, off_t offset,
		int buffer_length, int *eof, void *data)
{
	int ret;
	if(offset > 0)
		ret = 0;
	else
	{
		ret = snprintf(buffer, buffer_length,
				"%s", DRIVER_NAME);
	}
	return ret;
}

struct proc_dir_entry *ich7_proc;
#define PROC_MESSAGE_LENGTH 512
static char proc_message[PROC_MESSAGE_LENGTH];

static int ich7_proc_open_event(struct inode *inode, struct file *file)
{ return 0; }
static int ich7_proc_close_event(struct inode *inode, struct file *file)
{ return 0; }
static void make_proc_message(void)
{
	char *c = proc_message;
	size_t pm_length = PROC_MESSAGE_LENGTH;
	int i;

	for(i = 0; i < ARRAY_SIZE(ich7_registers); i++)
	{
		u32 value = read_reg(&ich7_registers[i]);

		snprintf(c, pm_length, "%-15s 0x%08x\n",
				ich7_registers[i].name, value);
		pm_length -= strlen(c);
		c += strlen(c);
	}
}
static ssize_t ich7_proc_read_event(struct file *file, char __user *buffer,
		size_t length, loff_t *ppos)
{
	static int finished = 0;
	int i;

	if(*ppos == 0)
		make_proc_message();
	if(finished) {
		finished = 0;
		return 0;
	}

	for(i = 0; i < length && proc_message[i]; i++)
		put_user(proc_message[i], buffer + i);

	finished = 1;
	*ppos += i;
	return i;
}

#define READ_BUF	127
#define ARG_BUF		12

#define FUNC_NULL			0
#define FUNC_SET			1
#define FUNC_SETBIT			2
#define FUNC_CREATELED		3
#define FUNC_ADDCMD			4
#define FUNC_DELETELED		5
#define FUNC_REGISTERLED	6
#define FUNC_RUNLEDCMD		7

static ssize_t ich7_proc_write_event(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	char read_buf[READ_BUF + 1];
	int argc;
	char *argv[ARG_BUF];
	
	if(!buf)
		return -EINVAL;

	if(copy_from_user(read_buf, buf, (count < READ_BUF) ? count : READ_BUF))
		return -EINVAL;
	read_buf[count] = '\0';

	argc = token(read_buf, argv, ARG_BUF);
	if(argc >= 1)
	{
		int func = FUNC_NULL;
		if(!strcmp(argv[0], "set"))
			func = FUNC_SET;
		else if(!strcmp(argv[0], "setbit"))
			func = FUNC_SETBIT;
		else if(!strcmp(argv[0], "createled"))
			func = FUNC_CREATELED;
		else if(!strcmp(argv[0], "addledcmd"))
			func = FUNC_ADDCMD;
		else if(!strcmp(argv[0], "deleteled"))
			func = FUNC_DELETELED;
		else if(!strcmp(argv[0], "registerled"))
			func = FUNC_REGISTERLED;
		else if(!strcmp(argv[0], "runledcmd"))
			func = FUNC_RUNLEDCMD;

		switch(func)
		{
		case FUNC_SET:
		case FUNC_SETBIT:
			if(argc >= 2)
			{
				struct ich7_register *reg;
				reg = get_reg_by_name(argv[1]);
				if(reg == NULL)
					break;
					
				if((func == FUNC_SETBIT) && (argc >= 4)) {
					int bitno, val;
					sscanf(argv[2], "%i", &bitno);
					sscanf(argv[3], "%i", &val);

					set_reg_bit(reg, bitno, val);						
				}
				else if((func == FUNC_SET) && (argc >= 3)) {
					u32 val;
					sscanf(argv[2], "%i", &val);
					write_reg(reg, val);
				}
			}
			break;

		case FUNC_CREATELED:
			if(argc == 3)
			{
				struct ich7_led *led = get_next_free_led();
				if(led == NULL)
				{
					printk("No more free led slots\n");
					break;
				}
				strncpy(led->name, argv[1], LED_NAME_LEN);
				strncpy(led->color, argv[2], LED_COLOR_LEN);
				snprintf(led->fullname, LED_FULLNAME_LEN, "ich7:%s:%s", led->color, led->name);

#ifdef DEBUG
				printk("Created LED %s\n", led->name);
#endif
			}
			break;

		case FUNC_ADDCMD:
			if(argc == 6)
			{
				struct ich7_led *led = get_led_by_name(argv[1]);
				struct ich7_led_command *table;
				struct ich7_led_command *cmd;
				struct ich7_register *reg;

				if(led == NULL)
				{
					printk("led %s not found\n", argv[1]);
					break;
				}
				
				table = get_led_func_table(led, get_led_func_by_name(argv[2]));
				if(table == NULL)
				{
					printk("command %s not found\n", argv[2]);
					break;
				}

				cmd = get_next_free_command(table);
				if(cmd == NULL)
				{
					printk("no more free command slots\n");
					break;
				}

				reg = get_reg_by_name(argv[3]);
				if(reg == NULL)
				{
					printk("reg %s not found\n", argv[3]);
					break;
				}

				cmd->reg = reg;
				sscanf(argv[4], "%i", &cmd->bitno);
				sscanf(argv[5], "%i", &cmd->val_or_invert);

#ifdef DEBUG
				printk("Added command: LED: %s cmd: %s reg: %s bitno: %i val %i\n", led->name, argv[2], reg->name, cmd->bitno, cmd->val_or_invert);
#endif
			}
			break;

		case FUNC_DELETELED:
			if(argc == 2)
			{
				struct ich7_led *led = get_led_by_name(argv[1]);
				if(led == NULL)
				{
					printk("led %s not found\n", argv[1]);
					break;
				}
				
				delete_led(led);
			}
			break;

		case FUNC_RUNLEDCMD:
			if(argc == 3)
			{
				struct ich7_led *led = get_led_by_name(argv[1]);
				struct ich7_led_command *table;

				if(led == NULL)
				{
					printk("led %s not found\n", argv[1]);
					break;
				}

				table = get_led_func_table(led, get_led_func_by_name(argv[2]));
				if(table == NULL)
				{
					printk("command %s not found\n", argv[2]);
					break;
				}

				led_runcmd(table);
			}
			break;

		case FUNC_REGISTERLED:
			if(argc == 2)
			{
				struct ich7_led *led = get_led_by_name(argv[1]);
				int ret;

				if(led == NULL)
				{
					printk("led %s not found\n", argv[1]);
					break;
				}

				unregister_led(led);

				led->led_cdev.name = led->fullname;
				led->led_cdev.brightness = LED_OFF;
				
				if(led_rungetcmd(led->get_brightness) == 1)
					led->led_cdev.brightness = LED_FULL;

				led->led_cdev.brightness_set = led_cdev_set_brightness;

				ret = led_classdev_register(&nas_gpio_pci_dev->dev, &led->led_cdev);
				if(!ret)
					led->registered = 1;
			}
			break;
		}
	}

	return count;
}

static struct file_operations proc_ich7_operations = {
	.open = ich7_proc_open_event,
	.release = ich7_proc_close_event,
	.read = ich7_proc_read_event,
	.write = ich7_proc_write_event,
};

static int ich7_gpio_init(struct device *dev)
{
	init_leds();
	return 0;
}

static void ich7_lpc_cleanup(struct device *dev)
{
	// return the gpio io address range if we have obtained it
	if(gp_gpio_resource)
	{
#ifdef DEBUG
		printk("ICH7: releasing GPIO IO addresses");
#endif
		release_region(nas_gpio_io_base, ICH7_GPIO_SIZE);
		gp_gpio_resource = NULL;
	}
}

static int ich7_lpc_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int status = 0;
	u32 gc = 0;

	status = pci_enable_device(dev);
	if(status)
		goto out;

	nas_gpio_pci_dev = dev;

	status = pci_read_config_dword(dev, GPIO_CTRL, &gc);
	if(!(GPIO_EN & gc)) {
		status = -EEXIST;
		printk("ICH7: ERROR: The LPC GPIO Block has not been enabled.\n");
		goto out;
	}
#ifdef DEBUG
	printk("ICH7: GPIO_CTRL %x\n", gc);
#endif

	status = pci_read_config_dword(dev, GPIO_BASE, &nas_gpio_io_base);
	if(0 > status) {
		printk("ICH7: ERROR: Unable to read GPIOBASE.\n");
		goto out;
	}
	nas_gpio_io_base &= 0x0000ffc0;
#ifdef DEBUG
	printk("ICH7: GPIOBASE %x\n", nas_gpio_io_base);
#endif

	// Ensure exclusive access to GPIO IO range
	gp_gpio_resource = request_region(nas_gpio_io_base,
			ICH7_GPIO_SIZE, DRIVER_NAME);
	if(gp_gpio_resource == NULL)
	{
		printk("ICH6: ERROR: Unable to register GPIO IO addresses.\n");
		status = -1;
		goto out;
	}

	// Initialize the GPIO
	ich7_gpio_init(&dev->dev);

out:
	if(status)
		ich7_lpc_cleanup(&dev->dev);
	return status;
}

static void ich7_lpc_remove(struct pci_dev *dev)
{
	ich7_lpc_cleanup(&dev->dev);
}

// pci_driver structure to pass to the PCI modules
static struct pci_driver nas_gpio_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = ich7_lpc_pci_id,
	.probe = ich7_lpc_probe,
	.remove = ich7_lpc_remove,
};

// module load/unload
static int __init nas_gpio_init(void)
{
	int ret = 0;

	printk(KERN_INFO "registering %s driver\n", DRIVER_NAME);
	ret = pci_register_driver(&nas_gpio_pci_driver);
	if(ret)
		return ret;
	
	// Register the proc device
	ich7_proc = create_proc_entry(PROCFS_NAME, 0, NULL);
	if(ich7_proc == NULL)
	{
		remove_proc_entry(PROCFS_NAME, NULL);
		printk(KERN_ALERT "ICH7: could not initialize /proc/%s\n",
				PROCFS_NAME);
		ret = -ENOMEM;
		goto out;
	}
	ich7_proc->proc_fops = &proc_ich7_operations;

	return 0;

out:
	remove_proc_entry(PROCFS_NAME, NULL);
	pci_unregister_driver(&nas_gpio_pci_driver);
	return ret;
}

static void __exit nas_gpio_exit(void)
{
	// TODO unregister LEDs
	printk(KERN_INFO "unregistering %s driver\n", DRIVER_NAME);
	init_leds();
	remove_proc_entry(PROCFS_NAME, NULL);
	pci_unregister_driver(&nas_gpio_pci_driver);
}

module_init(nas_gpio_init);
module_exit(nas_gpio_exit);

