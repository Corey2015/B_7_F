#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/acpi.h>
#include <linux/miscdevice.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>

//#include <mach/mt_spi.h>
#include <../../../../../../kernel-3.18/drivers/spi/mediatek/mt6735/mt_spi.h>
#define TRANSFER_MODE_DMA 

struct spidev_data {
    dev_t devt;
    spinlock_t spi_lock;
    /** SPI设备的结构体 */
    struct spi_device *spi;

    /* TX/RX buffers are NULL unless this device is open (users > 0) */
    struct mutex buf_lock;
    unsigned users;
    u8 *tx_buffer;
    u8 *rx_buffer;
};

static struct mt_chip_conf spi_conf =
{
    .setuptime    = 1,
    .holdtime     = 1,
    .high_time    = 6,
    .low_time     = 6,
    .cs_idletime  = 2,
    .ulthgh_thrsh = 0,
    .cpol         = 1,
    .cpha         = 1,
    .rx_mlsb      = SPI_MSB, 
    .tx_mlsb      = SPI_MSB,
    .tx_endian    = 0,
    .rx_endian    = 0,
#ifdef TRANSFER_MODE_DMA
    .com_mod      = DMA_TRANSFER,
#else
    .com_mod      = FIFO_TRANSFER,
#endif
    .pause        = 0,
    .finish_intr  = 1,
    .deassert     = 0,
    .ulthigh      = 0,
    .tckdly       = 0,
};

#define BUFSIZ (12*1024)
DECLARE_WAIT_QUEUE_HEAD(wait_spi);
static volatile int event_spi;

/*-------------------------------------------------------------------------*/
static inline ssize_t
spidev_sync_write(struct spidev_data *dev, size_t len)
{
    int status;
    struct spi_message m;
    struct spi_transfer trans = {
            .tx_buf     = dev->tx_buffer,
            .rx_buf     = dev->rx_buffer,
            .len        = len,
            .bits_per_word = 8,
    };

	if((len > 1024) && (len%1024 != 0))
		trans.len = (len/1024+1)*1024;

    spi_message_init(&m);
    spi_message_add_tail(&trans, &m);
    status = spi_sync(dev->spi, &m);
    event_spi = 1;
    return status;
}

/*-------------------------------------------------------------------------*/

/* Read-only message with current device setup */
static ssize_t
spidev_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct spidev_data	*spidev;
	long missing;

	/* chipselect only toggles at start or end of operation */
	if (count > BUFSIZ)
		return -EMSGSIZE;
    spidev = filp->private_data;

	wait_event_interruptible(wait_spi, (event_spi == 1));

	missing = copy_to_user(buf, spidev->rx_buffer, count);
	if (missing == 0)
        return count;
	else
        return -EFAULT;
}

/* Write-only message with current device setup */
static ssize_t
spidev_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct spidev_data	*spidev;
	ssize_t			status = 0;
	unsigned long		missing;

	if (count > BUFSIZ)
		return -EMSGSIZE;

	spidev = filp->private_data;

	mutex_lock(&spidev->buf_lock);
	missing = copy_from_user(spidev->tx_buffer, buf, count);
	if (missing == 0){
        event_spi = 0;
		status = spidev_sync_write(spidev, count);
	}
	else
		status = -EFAULT;
	mutex_unlock(&spidev->buf_lock);

    if (status == 0)
        return count;
    else
        return status;
}

static struct spidev_data *private_spi;

static int spidev_open(struct inode *inode, struct file *filp)
{
    int status = -ENXIO;

    if (!private_spi->tx_buffer) {
        private_spi->tx_buffer = kmalloc(BUFSIZ, GFP_KERNEL);
        if (!private_spi->tx_buffer) {
            dev_dbg(&private_spi->spi->dev, "open/ENOMEM\n");
            status = -ENOMEM;
            return status;
        }
    }

    if (!private_spi->rx_buffer) {
        private_spi->rx_buffer = kmalloc(BUFSIZ, GFP_KERNEL);
        if (!private_spi->rx_buffer) {
            dev_dbg(&private_spi->spi->dev, "open/ENOMEM\n");
            status = -ENOMEM;
            goto err_alloc_rx_buf;
        }
    }

    private_spi->users++;
    filp->private_data = private_spi;
    nonseekable_open(inode, filp);

    return 0;

    err_alloc_rx_buf:
    kfree(private_spi->tx_buffer);
    private_spi->tx_buffer = NULL;
    return status;
}

static int spidev_release(struct inode *inode, struct file *filp)
{
    struct spidev_data *spidev;
    spidev = filp->private_data;
    filp->private_data = NULL;

    /* last close? */
    spidev->users--;
    if (!spidev->users) {
        int dofree;

        kfree(spidev->tx_buffer);
        spidev->tx_buffer = NULL;

        kfree(spidev->rx_buffer);
        spidev->rx_buffer = NULL;

        spin_lock_irq(&spidev->spi_lock);
        dofree = (spidev->spi == NULL);
        spin_unlock_irq(&spidev->spi_lock);

        if (dofree)
            kfree(spidev);
    }

    return 0;
}

static const struct file_operations spidev_fops = {
        .owner =    THIS_MODULE,

        .write =    spidev_write,
        .read =     spidev_read,
        .open =     spidev_open,
        .release =  spidev_release,
};

/*-------------------------------------------------------------------------*/
static const struct of_device_id spidev_dt_ids[] = {
        {.compatible = "mediatek,hct_finger"},
        {},
};

/*-------------------------------------------------------------------------*/
static int spidev_probe(struct spi_device *spi)
{
    int status;
    struct spidev_data *spidev;

    printk("fsc: %s\n", __func__);

    if (spi->dev.of_node && !of_match_device(spidev_dt_ids, &spi->dev)) {
        dev_err(&spi->dev, "buggy DT: spidev listed directly in DT\n");
        WARN_ON(spi->dev.of_node && !of_match_device(spidev_dt_ids, &spi->dev));
    }

    /* Allocate driver data */
    spidev = kzalloc(sizeof(*spidev), GFP_KERNEL);
    if (!spidev)
        return -ENOMEM;

    /* Initialize the driver data */
    spidev->spi = spi;
    printk("fsc: %s bus_num=%d chip_select=%d\n", __func__, spi->master->bus_num, spi->chip_select);
    spin_lock_init(&spidev->spi_lock);
    mutex_init(&spidev->buf_lock);

    spi->max_speed_hz = 10 * 1e6;//10MHz
    spi->mode = 0;
    spi->bits_per_word = 8;
	spi->controller_data = (void *)&spi_conf;
    status = spi_setup(spi);

    if (status == 0) {
        spi_set_drvdata(spi, spidev);
        private_spi = spidev;
    } else
        kfree(spidev);

    return status;
}

static int spidev_remove(struct spi_device *spi)
{
    struct spidev_data *spidev = spi_get_drvdata(spi);

    printk("fsc: %s\n", __func__);
    /* make sure ops on existing fds can abort cleanly */
    spin_lock_irq(&spidev->spi_lock);
    spidev->spi = NULL;
    spin_unlock_irq(&spidev->spi_lock);

    if (spidev->users == 0)
        kfree(spidev);

    return 0;
}

static struct spi_device_id spi_id_table = {"formuex", 0};

static struct spi_driver spidev_spi_driver = {
        .driver = {
                .name  ="fsc_spi",
                //.of_match_table = spidev_dt_ids,
        },
        .probe  =    spidev_probe,
        .remove =    spidev_remove,
		.id_table = &spi_id_table,
};

/*-------------------------------------------------------------------------*/
static struct miscdevice misc = {
        .minor       = MISC_DYNAMIC_MINOR,
        .name        = "fsc_spi",
        .fops        = &spidev_fops,
};

static struct spi_board_info spi_board_devs[] __initdata = {
	[0] = {
		.modalias = "formuex",
		.bus_num = 0,
		.chip_select = 0,
		.mode = SPI_MODE_0,
	},
};

static int __init fsc_spi_init(void)
{
    int status;

	status = spi_register_board_info(spi_board_devs, ARRAY_SIZE(spi_board_devs));
    if (status < 0) {
		return status;
	}

    status = spi_register_driver(&spidev_spi_driver);
    if (status < 0) {
		return status;
    }

    misc_register(&misc);

    printk("fsc: %s status=%d\n", __func__, status);
    return status;
}
module_init(fsc_spi_init);

static void __exit fsc_spi_exit(void)
{
    printk("fsc: %s\n", __func__);
    spi_unregister_driver(&spidev_spi_driver);
    misc_deregister(&misc);
}
module_exit(fsc_spi_exit);

MODULE_AUTHOR("Finchos, www.finchos.com");
MODULE_DESCRIPTION("User mode SPI device interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:spi_fsc");
