/* Battery-only HW-954 sleep diagnostic. NO Zigbee, UART or motor control. */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
int main(void)
{
    const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (device_is_ready(gpio0)) {
        /* HW-954 external VCC gate off, per board documentation. */
        (void)gpio_pin_configure(gpio0, 13, GPIO_OUTPUT_LOW);
        /* P0.15 LED: active-low, keep high/off. */
        (void)gpio_pin_configure(gpio0, 15, GPIO_OUTPUT_HIGH);
    }
    for (;;) {
        k_sleep(K_FOREVER);
    }
}
