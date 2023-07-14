#include "myled.h"
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#define CNAME "myled"
int major;
char kbuf[128] = { 0 };
gpio_t *gpioe, *gpiof;
unsigned int* rcc;
struct class* cls;
struct device* dev;
int myled_open(struct inode* inode, struct file* file)
{
    printk("%s:%s:%d\n", __FILE__, __func__, __LINE__);
    // 1.地址映射
    rcc = ioremap(RCC_MP_AHB4ENSETR, 4);
    if (rcc == NULL) {
        printk("ioremap rcc error\n");
        return -ENOMEM;
    }
    gpioe = ioremap(GPIOE, sizeof(gpio_t));
    if (gpioe == NULL) {
        printk("ioremap moder error\n");
        return -ENOMEM;
    }
    gpiof = ioremap(GPIOF, sizeof(gpio_t));
    if (gpiof == NULL) {
        printk("ioremap odr error\n");
        return -ENOMEM;
    }
    // 2.led初始化 gpioe10 gpiof10  gpioe8
    *rcc |= (1 << 4) | (1 << 5); // gpioe/gpiof时钟使能
    // LED1 init
    gpioe->MODER &= ~(3 << 20);
    gpioe->MODER |= (1 << 20);
    gpioe->ODR &= ~(1 << 10);
    // LED2 init
    gpiof->MODER &= ~(3 << 20);
    gpiof->MODER |= (1 << 20);
    gpiof->ODR &= ~(1 << 10);
    // LED3 init
    gpioe->MODER &= ~(3 << 16);
    gpioe->MODER |= (1 << 16);
    gpioe->ODR &= ~(1 << 8);
    return 0;
}
ssize_t myled_read(struct file* file,
    char __user* ubuf, size_t size, loff_t* offs)
{
    int ret;
    printk("%s:%s:%d\n", __FILE__, __func__, __LINE__);
    if (size > sizeof(kbuf))
        size = sizeof(kbuf);
    ret = copy_to_user(ubuf, kbuf, size);
    if (ret) {
        printk("copy_to_user error\n");
        return -EIO;
    }
    return size;
}
ssize_t myled_write(struct file* file,
    const char __user* ubuf, size_t size, loff_t* offs)
{
    int ret;
    printk("%s:%s:%d\n", __FILE__, __func__, __LINE__);
    if (size > sizeof(kbuf))
        size = sizeof(kbuf);
    ret = copy_from_user(kbuf, ubuf, size);
    if (ret) {
        printk("copy_from_user error\n");
        return -EIO;
    }

    return size;
}
void led_op(int which, int status)
{
    switch (which) {
    case LED1:
        status ? (gpioe->ODR |= (1 << 10)) : (gpioe->ODR &= ~(1 << 10));
        break;
    case LED2:
        status ? (gpiof->ODR |= (1 << 10)) : (gpiof->ODR &= ~(1 << 10));
        break;
    case LED3:
        status ? (gpioe->ODR |= (1 << 8)) : (gpioe->ODR &= ~(1 << 8));
        break;
    }
}
long myled_ioctl(struct file* file,
    unsigned int cmd, unsigned long arg)
{
    int which, ret;
    switch (cmd) {
    case LED_ON:
        ret = copy_from_user(&which, (void*)arg, GET_CMD_SIZE(LED_ON));
        if (ret) {
            printk("copy_from_user error\n");
            return -EIO;
        }
        led_op(which, ON);
        break;
    case LED_OFF:
        ret = copy_from_user(&which, (void*)arg, GET_CMD_SIZE(LED_OFF));
        if (ret) {
            printk("copy_from_user error\n");
            return -EIO;
        }
        led_op(which, OFF);
        break;
    }

    return 0;
}
int myled_close(struct inode* inode, struct file* file)
{
    printk("%s:%s:%d\n", __FILE__, __func__, __LINE__);
    iounmap(rcc);
    iounmap(gpioe);
    iounmap(gpiof);
    return 0;
}
const struct file_operations fops = {
    .open = myled_open,
    .read = myled_read,
    .write = myled_write,
    .unlocked_ioctl = myled_ioctl,
    .release = myled_close,
};
static int __init myled_init(void)
{
    // 1.注册字符设备驱动 register_chrdev
    major = register_chrdev(0, CNAME, &fops);
    if (major < 0) {
        printk("register_chrdev error\n");
        return major;
    }
    printk("register char device driver success,major = %d\n", major);

    // 2.自动创建设备节点
    cls = class_create(THIS_MODULE, "hello");
    if (IS_ERR(cls)) {
        printk("class_create error\n");
        return PTR_ERR(cls);
    }
    dev = device_create(cls, NULL, MKDEV(major, 0), NULL, "myled");
    if (IS_ERR(dev)) {
        printk("device_create error\n");
        return PTR_ERR(dev);
    }
    return 0;
}
static void __exit myled_exit(void)
{
    // 3.销毁设备节点
    device_destroy(cls, MKDEV(major, 0));
    class_destroy(cls);

    // 4.注销字符设备驱动  unregister_chrdev
    unregister_chrdev(major, CNAME);
}
module_init(myled_init);
module_exit(myled_exit);
MODULE_LICENSE("GPL");