#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/workqueue.h>

#define DEVICE_NAME         "ekranas"
#define LCD_LINE_LEN        16
#define LCD_I2C_ADDR        0x3F
#define MAX_TEXT_LEN        256
#define LCD_BACKLIGHT       0x08
#define LCD_ENABLE_BIT      0x04
#define LCD_COMMAND_MODE    0
#define LCD_DATA_MODE       1

static struct i2c_client *lcd_client;
static struct class *lcd_class;
static struct cdev lcd_cdev;
static dev_t lcd_dev;

static char full_buf[MAX_TEXT_LEN];
static size_t full_len;

static DEFINE_MUTEX(buf_lock);
static struct delayed_work lcd_delayed_work;
static bool showing_first_half = true;

// LCD functions
static int lcd_send_nibble(uint8_t nibble, int mode)
{
    uint8_t rs = (mode == LCD_DATA_MODE) ? 0x01 : 0x00;
    uint8_t byte = nibble | rs | LCD_BACKLIGHT;
    uint8_t pulse = byte | LCD_ENABLE_BIT;

    uint8_t sequence[] = { byte, pulse, byte };
    for (int i = 0; i < ARRAY_SIZE(sequence); i++) {
        int ret = i2c_smbus_write_byte(lcd_client, sequence[i]);
        if (ret < 0)
            return ret;
        usleep_range(500, 600);
    }
    return 0;
}

static int lcd_send_byte(uint8_t data, int mode)
{
    int ret = lcd_send_nibble(data & 0xF0, mode);
    if (ret < 0)
        return ret;
    return lcd_send_nibble((data << 4) & 0xF0, mode);
}

static int lcd_cmd(uint8_t cmd)
{
    int ret = lcd_send_byte(cmd, LCD_COMMAND_MODE);
    if (ret < 0)
        return ret;
    if (cmd < 4)
        usleep_range(2000, 2500);
    return 0;
}

static void lcd_init_display(void)
{
    msleep(50);
    lcd_send_byte(0x33, LCD_COMMAND_MODE);
    lcd_send_byte(0x32, LCD_COMMAND_MODE);
    lcd_cmd(0x28);
    lcd_cmd(0x0C);
    lcd_cmd(0x06);
    lcd_cmd(0x01);
    msleep(2);
}

static void lcd_print_line(const char *src, size_t offset, uint8_t line_addr)
{
    lcd_cmd(line_addr);

    for (int i = 0; i < LCD_LINE_LEN; i++) {
        size_t idx = offset + i;

        char c = ' ';
        if (idx < full_len && src[idx] >= 32 && src[idx] <= 126)
            c = src[idx];

        lcd_send_byte(c, LCD_DATA_MODE);
    }
}


static void lcd_display_segment(const char *src, size_t offset)
{
    lcd_print_line(src, offset, 0x80);
    lcd_print_line(src, offset + LCD_LINE_LEN, 0xC0);
}

// Workqueue function to show 1st or 2nd part of the text if text length > 32
static void lcd_toggle_display(struct work_struct *work)
{
    mutex_lock(&buf_lock);

    if (full_len <= LCD_LINE_LEN * 2) {
        mutex_unlock(&buf_lock);
        return;
    }

    showing_first_half = !showing_first_half;
    size_t offset = showing_first_half ? 0 : LCD_LINE_LEN * 2;

    lcd_display_segment(full_buf, offset);
    schedule_delayed_work(&lcd_delayed_work, msecs_to_jiffies(8000));

    mutex_unlock(&buf_lock);
}

// Write function => /dev/ekranas
static ssize_t lcd_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
    char kbuf[128];

    if (len > sizeof(kbuf) - 1)
        len = sizeof(kbuf) - 1;

    if (copy_from_user(kbuf, buf, len))
        return -EFAULT;

    kbuf[len] = '\0';
    if (len > 0 && kbuf[len - 1] == '\n')
        kbuf[len - 1] = '\0';

    mutex_lock(&buf_lock);

    memset(full_buf, 0, sizeof(full_buf));
    full_len = 0;

    for (size_t i = 0; i < len && full_len < MAX_TEXT_LEN - 1; i++) {
        if (kbuf[i] >= 32 && kbuf[i] <= 126)
            full_buf[full_len++] = kbuf[i];
    }

    lcd_init_display();
    showing_first_half = true;
    lcd_display_segment(full_buf, 0);

    cancel_delayed_work_sync(&lcd_delayed_work);
    if (full_len > LCD_LINE_LEN * 2)
        schedule_delayed_work(&lcd_delayed_work, msecs_to_jiffies(8000));

    mutex_unlock(&buf_lock);
    return len;
}

// Read function => cat /dev/ekranas
static ssize_t lcd_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
    char kbuf[MAX_TEXT_LEN + 2];

    if (*ppos > 0)
        return 0;

    mutex_lock(&buf_lock);
    size_t to_copy = min(len - 1, full_len);
    for (size_t i = 0; i < to_copy; i++)
        kbuf[i] = (full_buf[i] >= 32 && full_buf[i] <= 126) ? full_buf[i] : ' ';
    kbuf[to_copy] = '\n';
    kbuf[to_copy + 1] = '\0';
    mutex_unlock(&buf_lock);

    if (copy_to_user(buf, kbuf, to_copy + 2))
        return -EFAULT;

    *ppos += to_copy + 1;
    return to_copy + 1;
}

static int lcd_open(struct inode *inode, struct file *file) { return 0; }
static int lcd_release(struct inode *inode, struct file *file) { return 0; }

static const struct file_operations lcd_fops = {
    .owner   = THIS_MODULE,
    .open    = lcd_open,
    .release = lcd_release,
    .write   = lcd_write,
    .read    = lcd_read,
};

// Probe function to find the device
static int lcd_probe(struct i2c_client *client)
{
    int ret;

    dev_info(&client->dev, "LCD driver probing...\n");

    if (client->addr != LCD_I2C_ADDR)
        return -EINVAL;

    lcd_client = client;
    lcd_init_display();

    ret = alloc_chrdev_region(&lcd_dev, 0, 1, DEVICE_NAME);
    if (ret)
        return ret;
    dev_info(&client->dev, "Character device region allocated\n");

    cdev_init(&lcd_cdev, &lcd_fops);
    ret = cdev_add(&lcd_cdev, lcd_dev, 1);
    if (ret)
        goto unregister_chrdev;
    dev_info(&client->dev, "Character device added\n");

    lcd_class = class_create(DEVICE_NAME);
    if (IS_ERR(lcd_class)) {
        ret = PTR_ERR(lcd_class);
        goto del_cdev;
    }

    if (IS_ERR(device_create(lcd_class, NULL, lcd_dev, NULL, DEVICE_NAME))) {
        ret = -ENOMEM;
        goto destroy_class;
    }

    INIT_DELAYED_WORK(&lcd_delayed_work, lcd_toggle_display);

    dev_info(&client->dev, "Device node created: /dev/%s\n", DEVICE_NAME);
    dev_info(&client->dev, "LCD driver successfully initialized\n");
    return 0;

destroy_class:
    class_destroy(lcd_class);
del_cdev:
    cdev_del(&lcd_cdev);
unregister_chrdev:
    unregister_chrdev_region(lcd_dev, 1);
    return ret;
}

// Remove function to remove the driver
static void lcd_remove(struct i2c_client *client)
{
    dev_info(&client->dev, "Removing LCD driver\n");

    cancel_delayed_work_sync(&lcd_delayed_work);
    device_destroy(lcd_class, lcd_dev);
    class_destroy(lcd_class);
    cdev_del(&lcd_cdev);
    unregister_chrdev_region(lcd_dev, 1);

    dev_info(&client->dev, "LCD driver successfully removed\n");
}

// Device match tables
static const struct i2c_device_id lcd_id[] = {
    { DEVICE_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, lcd_id);

static const struct of_device_id lcd_of_match[] = {
    { .compatible = "ekranas,lcd" },
    { }
};
MODULE_DEVICE_TABLE(of, lcd_of_match);

// I2C driver struct
static struct i2c_driver lcd_driver = {
    .driver = {
        .name           = DEVICE_NAME,
        .of_match_table = lcd_of_match,
    },
    .probe    = lcd_probe,
    .remove   = lcd_remove,
    .id_table = lcd_id,
};

module_i2c_driver(lcd_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tadas");
MODULE_DESCRIPTION("I2C LCD Driver");
