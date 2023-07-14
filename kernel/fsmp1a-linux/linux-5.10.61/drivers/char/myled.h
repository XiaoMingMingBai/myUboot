#ifndef __MYLED_H__
#define __MYLED_H__
typedef struct
{
    volatile unsigned int MODER;
    volatile unsigned int OTYPER;
    volatile unsigned int OSPEEDR;
    volatile unsigned int PUPDR;
    volatile unsigned int IDR;
    volatile unsigned int ODR;
} gpio_t;

#define GPIOE (0x50006000)
#define GPIOF (0x50007000)
#define RCC_MP_AHB4ENSETR 0x50000a28


#define LED_ON _IOW('K', 1, int)
#define LED_OFF _IOW('K', 0, int)
#define GET_CMD_SIZE(cmd)  ((cmd>>16)&0x3fff)
enum leds {
    LED1 = 1,
    LED2,
    LED3
};
enum leds_status {
    OFF,
    ON
};
#endif