/**
 * @file fsc_driver.c
 * @brief
 * @author Finchos software team
 * @version 1.01

 * Copyright (C) 2008-2016 CHENGDU FINCHOS CO.,LTD
 *
 * All rights are reserved
 *
 * Proprieary and confidential.
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 * Any use is subject to an appropriate license granted by Finchos co.,ltd.
 * @date 2016-12-26
 */

#include <linux/device.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
/** 堆栈函数的头 */
#include <linux/slab.h>
/** 输入事件函数的头 */
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <linux/hct_board_config.h>
extern int hct_finger_set_power(int cmd);
extern int hct_finger_set_reset(int cmd);
extern int hct_finger_set_18v_power(int cmd);

#define FSC_KERNEL_LOG(fmt, ...) printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)

static const struct of_device_id fp_of_match[] = {
	{.compatible = "mediatek,hct_finger",},
	{},
};

typedef struct fsc_data_s {
    int gpio_flag;
    /** 设备对象的指针 */
    struct device *device;
    /** 中断号 */
    int irq_num;
    /** 中断引脚编号 */
    int gpio_irq;
    /** 复位引脚编号 */
    int gpio_rst;
    bool irq_flag;
    /** 输入设备 */
    struct input_dev *input_dev;
    /** 中断服务程序的工作队列 */
    struct work_struct work;
} fsc_data_t;

static fsc_data_t sData;

static ssize_t fsc_irq_get(struct device *obj,
                           struct device_attribute *attr,
                           char *buf)
{
    int gpio_status;
    ssize_t len;
    gpio_status = 0;
    len = sprintf(buf, "%d\n", gpio_status);
    //FSC_KERNEL_LOG("fsc: %s gpio_irq=%d value=%d return=%zu\n", __func__, sData.gpio_irq, gpio_status, len);
    return len;
}

static ssize_t fsc_irq_set(struct device *obj,
                           struct device_attribute *attr,
                           const char *buf,
                           size_t count)
{
    if (0 == strncmp(buf, "enable", strlen("enable"))) {
        FSC_KERNEL_LOG("fsc: %s input= %s\n", __func__, buf);
        sData.irq_flag = true;
        enable_irq(sData.irq_num);
    }
    if (0 == strncmp(buf, "disable", strlen("disable"))) {
		FSC_KERNEL_LOG("fsc: %s input= %s\n", __func__, buf);
		disable_irq(sData.irq_num);
    }

    return count;
}

static ssize_t fsc_rst_get(struct device *obj,
                           struct device_attribute *attr,
                           char *buf)
{
    int gpio_status;
    ssize_t len;
    gpio_status = 0;
    len = sprintf(buf, "%d\n", gpio_status);
    //FSC_KERNEL_LOG("fsc: %s gpio_rst=%d value=%d return=%zu\n", __func__, sData.gpio_rst, gpio_status, len);
    return len;
}

static ssize_t fsc_rst_set(struct device *obj,
                           struct device_attribute *attr,
                           const char *buf,
                           size_t count)
{
    FSC_KERNEL_LOG("fsc: %s input= %s\n", __func__, buf);
    if (0 == strncmp(buf, "true", strlen("true"))) {
		hct_finger_set_reset(1);
    }
    else if (0 == strncmp(buf, "false", strlen("false"))) {
		hct_finger_set_reset(0);
    }
    else if (0 == strncmp(buf, "reset", strlen("reset"))) {
		hct_finger_set_power(1);
		hct_finger_set_18v_power(1);
        mdelay(10);
		hct_finger_set_reset(0);
        mdelay(30);
		hct_finger_set_reset(1);
        mdelay(20);
    } else
        FSC_KERNEL_LOG("fsc: %s input error\n", __func__);

    return count;
}

static ssize_t fsc_input(struct device *obj,
                         struct device_attribute *attr,
                         const char *buf,
                         size_t count)
{
    FSC_KERNEL_LOG("fsc: %s input= %s\n", __func__, buf);

    if (0 == strncmp(buf, "true", strlen("true"))) {
			input_report_key(sData.input_dev, KEY_POWER, 1);
			input_report_key(sData.input_dev, KEY_POWER, 0);
			input_sync(sData.input_dev);
    }

    return count;
}

/** Sysfs attributes cannot be world-writable. */
static struct device_attribute gpio_irq_attribute = {
        .attr = {
                .name = "fsc_irq",
                .mode = S_IRUSR | S_IWUSR,
        },
        .show  = fsc_irq_get,
        .store = fsc_irq_set,
};
static struct device_attribute gpio_rst_attribute = {
        .attr = {
                .name = "fsc_rst",
                .mode = S_IRUSR | S_IWUSR,
        },
        .show  = fsc_rst_get,
        .store = fsc_rst_set,
};
static struct device_attribute input_attribute = {
        .attr = {
                .name = "fsc_input",
                .mode = S_IWUSR,
        },
        .store = fsc_input,
};
//static struct device_attribute gpio_pwr_attribute	= __ATTR(fp_pwr,  S_IRUSR|S_IWUSR, fsc_rst_get, fsc_rst_set);

/*
 * Create a group of attributes so that we can create and destroy them all
 * at once.
 */
static struct attribute *attributes[] = {
        &gpio_irq_attribute.attr,
        &gpio_rst_attribute.attr,
        &input_attribute.attr,
        NULL,    /* need to NULL terminate the list of attributes */
};

/*
 * An unnamed attribute group will put all of the attributes directly in
 * the device directory.  If we specify a name, a subdirectory will be
 * created for the attributes with the directory being the name of the
 * attribute group.
 */
static struct attribute_group attr_group = {
        .attrs = attributes,
};

static irqreturn_t irq_handler(int irq, void *dev_id)
{
    FSC_KERNEL_LOG("fsc: %s\n", __func__);
	//schedule_work(&sData.work);
	sysfs_notify(&sData.device->kobj, NULL, gpio_irq_attribute.attr.name);
    return IRQ_HANDLED;
}

static void work_handler(struct work_struct *data)
{
    if(sData.irq_flag == true)
    {
        sData.irq_flag = false;
        /** 通过sysfs文件通知 */
        sysfs_notify(&sData.device->kobj, NULL, gpio_irq_attribute.attr.name);
        FSC_KERNEL_LOG("fsc: sysfs_notify!!!\n");
    }
}

static int input_create(struct fsc_data_s *o)
{
	int err;
	o->input_dev = input_allocate_device();
	if (!o->input_dev) {
		printk(KERN_ERR "Unable to allocate the input device !!\n");
		return -ENOMEM;
	}
	o->input_dev->name = "fsc_input";
	set_bit(EV_KEY,    o->input_dev->evbit); //支持的动作为按键
	set_bit(KEY_POWER, o->input_dev->keybit);//支持的按键类型

	err = input_register_device(o->input_dev);
	if(err)
	{
		printk(KERN_ERR "failed to register input device/n");  
		input_free_device(o->input_dev);
		return -ENOMEM;
	}
	return 0;
}
/**
 * GPIO的初始化及中断申请
 * @param sData
 * @return
 */
static int gpio_create(struct fsc_data_s *o)
{
    int err;
	struct device_node *node = NULL;
    FSC_KERNEL_LOG("fsc: %s\n", __func__);
#if 0
    err = gpio_request_one(o->gpio_irq, GPIOF_IN, "sensor interrupt");
    if (err){
        FSC_KERNEL_LOG("fsc: %s irq=%d return=%d\n", __func__, o->gpio_irq, err);
        return err;
    }
    err = gpio_request_one(o->gpio_rst, GPIOF_OUT_INIT_HIGH, "sensor reset");
    if (err){
        FSC_KERNEL_LOG("fsc: %s rst=%d return=%d\n", __func__, o->gpio_rst, err);
        return err;
    }
    o->irq_num = gpio_to_irq(o->gpio_irq);
#else
	//TODO use hct api
	node = of_find_matching_node(node, fp_of_match);
	if (node){
		o->irq_num = irq_of_parse_and_map(node, 0);
	}else{
		printk("fingerprint request_irq can not find fp eint device node!.");
	}
#endif
    FSC_KERNEL_LOG("%s gpio_to_irq =%d\n", __func__, o->irq_num);

    err = request_irq(o->irq_num,
                      irq_handler,
                      IRQF_TRIGGER_RISING,//上升沿触发
                      "sensor irq",
                      NULL);
    if (err < 0) {
        FSC_KERNEL_LOG("irq_request failed!\n");
        return err;
    }
    disable_irq(o->irq_num);//默认关闭中断
    o->irq_flag = false;
    o->gpio_flag = 1;

    INIT_WORK(&sData.work, work_handler);

    return 0;
}


static int gpio_destory(struct fsc_data_s *o)
{
    FSC_KERNEL_LOG("fsc: %s\n", __func__);
    if (o->gpio_flag == 1) {
        disable_irq(o->irq_num);
        free_irq(o->irq_num, NULL);
        //gpio_free(o->gpio_irq);
        //gpio_free(o->gpio_rst);
    }
    return 0;
}

static int fsc_driver_probe(struct platform_device *pldev)
{
    int retval = 0;
    struct device *dev = &pldev->dev;

    FSC_KERNEL_LOG("fsc: %s\n", __func__);
#if 0
	sData.gpio_irq = of_get_named_gpio(dev->of_node, "gpio_irq", 0);
    FSC_KERNEL_LOG("fsc: dts gpio_irq=%d \n", sData.gpio_irq);
	sData.gpio_rst = of_get_named_gpio(dev->of_node, "gpio_rst", 0);
    FSC_KERNEL_LOG("fsc: dts gpio_rst=%d \n", sData.gpio_rst);
#endif
    /* Create the files associated with this device */
    retval = sysfs_create_group(&pldev->dev.kobj, &attr_group);
    if (retval) {
        FSC_KERNEL_LOG("fsc: device_put %s \n", __func__);
        return retval;
    }

    sData.device = dev;
    gpio_create(&sData);
	input_create(&sData);

	//hct_finger_set_reset(1);
	//msleep(30);
	//hct_finger_set_reset(0);
	//msleep(20);

    return retval;
}

static int fsc_driver_remove(struct platform_device *pldev)
{
    int ret = 0;

	input_unregister_device(sData.input_dev);
    gpio_destory(&sData);
    sysfs_remove_group(&pldev->dev.kobj, &attr_group);
    FSC_KERNEL_LOG("fsc: %s\n", __func__);
    return ret;
}

/**
 * 匹配设备数中的信息
 */
static struct of_device_id fsc_of_match[] = {
        {.compatible = "fsc,fsc_gpio",},
        {},
};

static struct platform_driver fsc_driver = {
        .driver = {
                .name = "fsc_gpio",
                .owner = THIS_MODULE,
                .of_match_table = fsc_of_match,
        },
        .probe = fsc_driver_probe,
        .remove = fsc_driver_remove,
};

static int fsc_gpio_init(void)
{
    FSC_KERNEL_LOG("fsc: %s\n", __func__);
    if (platform_driver_register(&fsc_driver)) {
        FSC_KERNEL_LOG("fsc: platform_driver_register is failed\n ");
        return -EINVAL;
    };
    return 0;
}

static void fsc_gpio_exit(void)
{
    FSC_KERNEL_LOG("fsc: %s\n", __func__);
    platform_driver_unregister(&fsc_driver);
}

module_init(fsc_gpio_init);
module_exit(fsc_gpio_exit);
MODULE_LICENSE("GPL v2");
