#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define I2C_ADDR 0x3f // lcd address => i2cdetect -y 1
#define LCD_CHR  1  
#define LCD_CMD  0 

#define LINE1 0x80
#define LINE2 0xC0

#define LCD_BACKLIGHT 0x08
#define ENABLE 0b00000100

int i2c_fd;

void lcd_toggle_enable(uint8_t data) {
    usleep(500);

    uint8_t temp = data | ENABLE;
    write(i2c_fd, &temp, 1);

    usleep(500);

    temp = data & ~ENABLE;
    write(i2c_fd, &temp, 1);

    usleep(500);
}

void lcd_write(uint8_t bits, uint8_t mode) {
    uint8_t high = mode | (bits & 0xF0) | LCD_BACKLIGHT;
    uint8_t low  = mode | ((bits << 4) & 0xF0) | LCD_BACKLIGHT;

    write(i2c_fd, &high, 1);
    lcd_toggle_enable(high);

    write(i2c_fd, &low, 1);
    lcd_toggle_enable(low);
}

void lcd_init() {
    lcd_write(0x33, LCD_CMD);
    lcd_write(0x32, LCD_CMD);
    lcd_write(0x06, LCD_CMD);
    lcd_write(0x0C, LCD_CMD);
    lcd_write(0x28, LCD_CMD);
    lcd_write(0x01, LCD_CMD);
    usleep(2000);
}

void lcd_set_cursor(int line) {
    lcd_write(line == 1 ? LINE1 : LINE2, LCD_CMD);
}

void lcd_print(const char *str) {
    for (int i = 0; i < strlen(str) && i < 16; i++) {
        lcd_write(str[i], LCD_CHR);
    }
}

int main() {
    if ((i2c_fd = open("/dev/i2c-1", O_RDWR)) < 0) {
        perror("can't open /dev/i2c-1");
        exit(1);
    }

    if (ioctl(i2c_fd, I2C_SLAVE, I2C_ADDR) < 0) {
        perror("cant connect to i2c device");
        exit(1);
    }

    lcd_init();
    lcd_set_cursor(1);
    lcd_print("Hello world!");
    lcd_set_cursor(2);
    lcd_print("1234567890123456");

    close(i2c_fd);
    return 0;
}
