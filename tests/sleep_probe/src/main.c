/* v42.0-6: Zephyr idle-current test; SPARE BOARD ONLY.
 * No Zigbee, no motor control, no System OFF.
 * The kernel idle thread may enter System ON low-power sleep.
 */
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
    }
    /* No active polling, logging, or periodic timer after startup. */
    k_sleep(K_FOREVER);
    return 0;
}
