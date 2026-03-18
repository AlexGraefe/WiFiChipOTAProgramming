// #include <stdio.h>
// #include <string.h>
// #include <stdbool.h>
// #include <zephyr/kernel.h>
// #include <zephyr/drivers/gpio.h>
// #include <zephyr/drivers/flash.h>
// #include <zephyr/device.h>
// #include <zephyr/devicetree.h>

// #define USE_TCP 0

// #define LED0_NODE DT_ALIAS(led0)
// #define LED1_NODE DT_ALIAS(led1)
// #define LED2_NODE DT_ALIAS(led2)

// static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
// static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
// static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(LED2_NODE, gpios);

// const int a = 5;
// volatile int b = 10;

// uint8_t data[256];
// uint8_t data_write[256];

int main(void)
{
    // const struct device *flash_dev = DEVICE_DT_GET(DT_ALIAS(flash0));
    // int ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    // ret |= gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    // ret |= gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
    // uint64_t flash_size;
    // if (ret < 0) {
    //     while (1) {}
    // }
    // printk("Hello World! %p, %p\n", (void *)&a, (void *)&b);

    // ret = z_impl_flash_get_size(flash_dev, &flash_size);
    // if (ret < 0) {
    //     printk("Failed to get flash size: %d\n", ret);
    // } else {
    //     printk("Flash size: %llu bytes\n", flash_size);
    // }

    // off_t address = 0x20000 + 0x2E0000;
    // ret = z_impl_flash_erase(flash_dev, address, 256);
    // if (ret < 0) {
    //     printk("Failed to erase flash: %d\n", ret);
    // } else {
    //     printk("Flash erased successfully\n");
    // }

    // for (size_t i = 0; i < sizeof(data_write); i++) {
    //     data_write[i] = i;
    // }

    // ret = z_impl_flash_write(flash_dev, address, data_write, sizeof(data_write));
    // if (ret < 0) {
    //     printk("Failed to write flash: %d\n", ret);
    // } else {
    //     printk("Flash written successfully\n");
    // }
    
    // for (size_t i = 0; i < sizeof(data); i++) {
    //     data[i] = 0;
    // }
    // ret = z_impl_flash_read(flash_dev, address, data, 256);
    // if (ret < 0) {
    //     printk("Failed to read flash: %d\n", ret);
    // } else {
    //     printk("Read from flash:\n");
    //     for (int i = 0; i < 256; i++) {
    //         printk("%02x ", data[i]);
    //     }
    //     printk("\n");
    // }

    // while (1) {
    //     gpio_pin_set_dt(&led_red, 1);
    //     k_sleep(K_MSEC(500));
    //     gpio_pin_set_dt(&led_red, 0);
    //     k_sleep(K_MSEC(500));
    // }
}