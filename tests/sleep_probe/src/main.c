/* HW-954 P0.24 red LED polarity test. SPARE BOARD ONLY. */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>

int main(void)
{
    const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (device_is_ready(gpio0)) {
        (void)gpio_pin_configure(gpio0, 13, GPIO_OUTPUT_LOW);
        (void)gpio_pin_configure(gpio0, 15, GPIO_OUTPUT_HIGH);
        (void)gpio_pin_configure(gpio0, 24, GPIO_OUTPUT_HIGH);
        for (;;) {
            /* 20 seconds HIGH, then 20 seconds LOW, repeating. */
            (void)gpio_pin_set(gpio0, 24, 1);
            k_sleep(K_SECONDS(20));
            (void)gpio_pin_set(gpio0, 24, 0);
            k_sleep(K_SECONDS(20));
        }
    }
    for (;;) { k_sleep(K_FOREVER); }
}
