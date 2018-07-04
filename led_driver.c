/*
 * Written by Patryk Łaś, 2018 (patryk.las@slabs.pl).
 *
 * (C) Patryk Łaś
 * Simplify usage off RGB LED connected to 4 PWM driver (PWM_RED, PWM_GREEN, PWM_BLUE, PWM_BLINK
 *
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#include <linux/pwm.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kdev_t.h>

#include <linux/errno.h>
#include <dt-bindings/pwm/pwm.h>


#include "led_states.h"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Patryk Las");
MODULE_DESCRIPTION("Simplify usage off RGB LED connected to 4 PWM driver (PWM_RED, PWM_GREEN, PWM_BLUE, PWM_BLINK");

#define DRIVER_NAME "slabs-pwm-led"
#define PWM_PERIOD_1MS  1000000


static dev_t dev_nr;
static struct cdev c_dev;
static struct class *device_class;


static bool led_level_invertion = false;
struct pwm_device *pwm_devices[4];

static blink_t blink_config;
static rgb_luminance_t luminance_config;


enum hrtimer_restart reset_timer_callback(struct hrtimer *timer);
ktime_t ktime;
struct work_struct reset_workq;

struct timer_list reset_timer;

static struct hrtimer hr_timer;

static bool toggle_color = true;

static state_t current_state;
static bool is_opened = false;

static void state_booting(void);
static void state_booted(void);
static void state_disclosure(void);
static void state_connected(void);
static void state_provisioned(void);
static void state_reset(void);


s_state led_states[STATE_SIZE] = {
		{
				.state_string = "booted", //booted or factory
				.fblink_ptr = state_booted
		},

		{
				.state_string = "booting",
				.fblink_ptr = state_booting
		},

		{
				.state_string = "disclosure",
				.fblink_ptr = state_disclosure
		},

		{
				.state_string = "connected", //connected or provisioning
				.fblink_ptr = state_connected
		},

		{
				.state_string = "provisioned",
				.fblink_ptr = state_provisioned
		},

		{
				.state_string = "reset",
				.fblink_ptr = state_reset
		}
};


static void get_pwm_devices(struct device *dev)
{
	u8 i;
	const char pwm_labels[SIZE][10] = {
			"pwm-red",
			"pwm-green",
			"pwm-blue",
			"pwm-blink"
	};


	for(i=0; i<SIZE; i++)
	{
		pwm_devices[i] = of_pwm_get(dev->of_node, pwm_labels[i]);
		printk(KERN_DEBUG "Id: %d, pwmdev state: %d\n", i, pwm_devices[i]->state.enabled);
	}
}


static void enable_pwms(void)
{
	u8 i;
	for(i=0; i<SIZE; i++)
	{
		if(false == pwm_devices[i]->state.enabled)
		{
			pwm_enable(pwm_devices[i]);
			printk(KERN_DEBUG "Enabled pwm: %d, value: %d\n", i, pwm_devices[i]->state.enabled);
		}
	}
}

static void disable_pwms(void)
{
	u8 i;
	for(i=0; i<SIZE; i++)
	{
		if(true == pwm_devices[i]->state.enabled)
		{
			pwm_disable(pwm_devices[i]);
			printk(KERN_DEBUG "Disable pwm: %d, value: %d\n", i, pwm_devices[i]->state.enabled);
		}
	}
}

static int calculate_duty(u8 luminance)
{
	return  PWM_PERIOD_1MS * luminance / 0xFF;
}

static void rgb_config(blink_t *bp, rgb_luminance_t *rgb)
{
	disable_pwms();

	if(led_level_invertion)
	{
		pwm_set_polarity(pwm_devices[RED], PWM_POLARITY_INVERSED);
		pwm_set_polarity(pwm_devices[GREEN], PWM_POLARITY_INVERSED);
		pwm_set_polarity(pwm_devices[BLUE], PWM_POLARITY_INVERSED);
		pwm_set_polarity(pwm_devices[BLINK], PWM_POLARITY_INVERSED);
	}
	else
	{
		pwm_set_polarity(pwm_devices[RED], PWM_POLARITY_NORMAL);
		pwm_set_polarity(pwm_devices[GREEN], PWM_POLARITY_NORMAL);
		pwm_set_polarity(pwm_devices[BLUE], PWM_POLARITY_NORMAL);
		pwm_set_polarity(pwm_devices[BLINK], PWM_POLARITY_NORMAL);
	}

	if(bp->blink)
	{
		pwm_config(pwm_devices[BLINK], bp->ton_ms * PWM_PERIOD_1MS, (bp->ton_ms+bp->toff_ms) * PWM_PERIOD_1MS);
	}
	else
	{
		pwm_config(pwm_devices[BLINK], PWM_PERIOD_1MS, PWM_PERIOD_1MS);
	}

	pwm_config(pwm_devices[RED], calculate_duty(rgb->lred), PWM_PERIOD_1MS);
	pwm_config(pwm_devices[GREEN], calculate_duty(rgb->lgreen), PWM_PERIOD_1MS);
	pwm_config(pwm_devices[BLUE], calculate_duty(rgb->lblue), PWM_PERIOD_1MS);

	enable_pwms();
}


enum hrtimer_restart reset_timer_callback(struct hrtimer *timer_for_restart)
{
	ktime_t currtime , interval;

	currtime  = ktime_get();
	interval = ktime_set(0, 125000000);
	hrtimer_forward(timer_for_restart, currtime , interval);
//	printk(KERN_NOTICE "Timer\n");

	schedule_work(&reset_workq);

	return HRTIMER_RESTART;
}


static void pwm_workqueue_handler(struct work_struct *work)
{
	rgb_luminance_t olum, blum;
	blink_t bl;

	// ORANGE
	olum.lred = 0xFF;
	olum.lgreen = 0x5F;
	olum.lblue = 0x00;

	// BLUE
	blum.lred = 0x00;
	blum.lgreen = 0x00;
	blum.lblue = 0xFF;

	bl.blink = false;

	if(toggle_color)
	{
		rgb_config(&blink_config, &olum);
	}
	else
	{
		rgb_config(&blink_config, &blum);
	}

	toggle_color ^= 1;
}


static void state_booting(void)
{
	luminance_config.lred = 0x00;
	luminance_config.lgreen = 0xFF;
	luminance_config.lblue = 0x00;

	blink_config.blink = true;

	blink_config.ton_ms = 750;
	blink_config.toff_ms = 750;

	rgb_config(&blink_config, &luminance_config);
}

static void state_booted(void)
{
	luminance_config.lred = 0x00;
	luminance_config.lgreen = 0xFF;
	luminance_config.lblue = 0x00;

	blink_config.blink = false;

	rgb_config(&blink_config, &luminance_config);
}

static void state_disclosure(void)
{
	luminance_config.lred = 0x00;
	luminance_config.lgreen = 0x00;
	luminance_config.lblue = 0xFF;

	blink_config.blink = true;

	blink_config.ton_ms = 750;
	blink_config.toff_ms = 750;

	rgb_config(&blink_config, &luminance_config);
}

static void state_connected(void)
{
	luminance_config.lred = 0x00;
	luminance_config.lgreen = 0x00;
	luminance_config.lblue = 0xFF;

	blink_config.blink = true;

	blink_config.ton_ms = 125;
	blink_config.toff_ms = 125;

	rgb_config(&blink_config, &luminance_config);
}

static void state_provisioned(void)
{
	luminance_config.lred = 0x00;
	luminance_config.lgreen = 0x00;
	luminance_config.lblue = 0xFF;

	blink_config.blink = false;

	rgb_config(&blink_config, &luminance_config);
}

static void state_reset(void)
{
	ktime = ktime_set( 0, 125000000 );
	hrtimer_start( &hr_timer, ktime, HRTIMER_MODE_REL );
}


static bool enter_state(const char *state)
{
	u8  index;
	state_t previous_state = current_state;

	for(index=0; index<STATE_SIZE; index++)
	{
//		printk(KERN_NOTICE "Id: %d, strlen: %d\n", index, len);
		if(0 == strcmp(led_states[index].state_string, state))//, strlen(val)))
		{
			current_state = index;

			if(STATE_RESET == previous_state)
			{
				// disable reset_timer at state exit
				hrtimer_cancel(&hr_timer);
			}

			led_states[index].fblink_ptr();
			printk(KERN_INFO "Entering state %s\n", led_states[index].state_string);

			return true;
		}
	}

	printk(KERN_ERR "Unknown state: %s\n", state);

	return false;
}


static ssize_t fd_led_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	char val[30];

	if(copy_from_user(val, buf, (len>30?30:len)) != 0)
		return 0;

	val[strcspn(val, "\r\n")] = 0;

	if(enter_state(val))
	{
		return len;
	}

	return EINVAL;
}

static ssize_t fd_led_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	u8 i;

	char *temp_buff;
	size_t str_size = 0;
	size_t final_size = 0;
	ssize_t ret = 0;

	for(i=0; i<STATE_SIZE; i++)
	{
		str_size += strlen(led_states[i].state_string);
	}

	temp_buff = kzalloc(str_size + STATE_SIZE + 2, GFP_NOWAIT);//GFP_NOWAIT

	for(i=0; i<STATE_SIZE; i++)
	{
		if(i == current_state)
		{
			strcat(temp_buff, "[");
			strcat(temp_buff, led_states[i].state_string);
			strcat(temp_buff, "]");
		}
		else
		{
			strcat(temp_buff, led_states[i].state_string);
		}

		strcat(temp_buff, " ");
	}

	strcat(temp_buff, "\n");

	final_size = (len>(str_size+STATE_SIZE+3) ? str_size+STATE_SIZE+3 : len);
	printk(KERN_DEBUG "Size %d\n", final_size );

	ret = copy_to_user(buf, temp_buff, final_size);

	kfree(temp_buff);

	if(is_opened)
	{
		is_opened = false;
		return final_size;
	}

	return 0;
}

int fd_led_release(struct inode *in, struct file *fd)
{
	is_opened = false;
	return 0;
}

static int fd_led_open(struct inode *in, struct file *fd)
{
	is_opened = true;
	return 0;
}

static int red_led_probe(struct platform_device *pdev)
{

	struct device_node *d_node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	u32 val;
	const char *output;

	led_level_invertion = of_property_read_bool(d_node, "inverted");


	if(!of_property_read_u32(d_node, "val", &val))
	{
		printk(KERN_DEBUG "val: %x\n", val);
	}
	else
		printk(KERN_DEBUG "val: NOT FOUND\n");

	if(!of_property_read_string(d_node, "label", &output))
	{
		printk(KERN_DEBUG "label: %s\n", output);
	}
	else
		printk(KERN_DEBUG "label: NOT FOUND\n");


	get_pwm_devices(dev);

	if(!of_property_read_string(d_node, "trigger", &output))
	{
		printk(KERN_DEBUG "label: %s\n", output);
		enter_state(output);
	}
	else
		printk(KERN_DEBUG "label: NOT FOUND\n");


////////////////////////////////////////////////////////////////////////////////////


//// TAK DZIAŁA!!!!!
//
//	/* ALBO pwm_devices[...] albo ponizsze *b, *g */
//
////	struct pwm_device *b = of_pwm_get(dev->of_node, "pwm-blink");
////	struct pwm_device *g = of_pwm_get(dev->of_node, "pwm-green");
//
//	pwm_config(pwm_devices[GREEN], PWM_PERIOD_1MS, PWM_PERIOD_1MS);
//	pwm_config(pwm_devices[BLINK], PWM_PERIOD_1MS, PWM_PERIOD_1MS);
//
//	pwm_enable(pwm_devices[GREEN]);
//	pwm_enable(pwm_devices[BLINK]);

////////////////////////////////////////////////////////////////////////////////////

	return 0;
}

static int red_led_remove(struct platform_device *pdev)
{
//	gpio_free(1);

	u8 index;

	disable_pwms();

	for( index=0; index<SIZE; index++)
	{
		pwm_free(pwm_devices[index]);
	}

	printk( KERN_NOTICE "Platform device remove\n" );
    return 0;
}


static const struct of_device_id led_of_match[] = {
	{.compatible = DRIVER_NAME,},
	{},
};

MODULE_DEVICE_TABLE(of, led_of_match);

static struct platform_driver red_led_driver = {
    .probe      = red_led_probe,
    .remove     = red_led_remove,
    .driver     = {
                    .name = DRIVER_NAME,
					.owner = THIS_MODULE,
					.of_match_table = of_match_ptr(led_of_match),
                },
};


static struct file_operations led_fops =
{
  .owner = THIS_MODULE,
  .open = fd_led_open,
  .release = fd_led_release,
  .write = fd_led_write,
  .read = fd_led_read
};


/*===============================================================================================*/
static int red_led_driver_init(void)
{
    int result = 0;
    printk( KERN_NOTICE "S-Labs PWM Led driver: Initialization started\n" );

	// try to allocate major number for device starting from 0, requested device count 0 and name slabs-pwm-led (see /proc/devices
	if(0 > alloc_chrdev_region(&dev_nr, 0, 1, "slabs-pwm-led"))
	{
		return -1;
	}

	if ((device_class = class_create(THIS_MODULE, "slabs-pwm-led")) == NULL) //"chardrv"
	{
		unregister_chrdev_region(dev_nr, 1);
		return -1;
	}

	if (device_create(device_class, NULL, dev_nr, NULL, "led") == NULL)
	{
		class_destroy(device_class);
		unregister_chrdev_region(dev_nr, 1);
		return -1;
	}

	cdev_init(&c_dev, &led_fops);

	if (cdev_add(&c_dev, dev_nr, 1) == -1)
	{
		device_destroy(device_class, dev_nr);
		class_destroy(device_class);
		unregister_chrdev_region(dev_nr, 1);
		return -1;
	}

    result = platform_driver_probe(&red_led_driver, red_led_probe);

	hrtimer_init( &hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
	hr_timer.function = &reset_timer_callback;

	INIT_WORK(&reset_workq, pwm_workqueue_handler);

    return result;
}
/*-----------------------------------------------------------------------------------------------*/
static void red_led_driver_exit(void)
{
	cdev_del(&c_dev);
	device_destroy(device_class, dev_nr);
	class_unregister(device_class);
	class_destroy(device_class);
	unregister_chrdev_region(dev_nr, 1);

    printk( KERN_NOTICE "S-Labs PWM Led driver: Exiting\n" );
    platform_driver_unregister(&red_led_driver);     

    hrtimer_cancel(&hr_timer);
    flush_scheduled_work();

}
/*===============================================================================================*/

module_init(red_led_driver_init);
module_exit(red_led_driver_exit);
