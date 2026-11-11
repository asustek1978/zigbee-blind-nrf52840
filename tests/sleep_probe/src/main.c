/* v42.0-5: nRF52840 System OFF current test. SPARE BATTERY-ONLY BOARD. */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <hal/nrf_power.h>

int main(void)
{
    const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (device_is_ready(gpio0)) {
        (void)gpio_pin_configure(gpio0, 13, GPIO_OUTPUT_LOW);
        (void)gpio_pin_configure(gpio0, 15, GPIO_OUTPUT_HIGH);
    }
    /* Allow 5 seconds to distinguish startup from System OFF. */
    k_sleep(K_SECONDS(5));
    /* System OFF: wake only via reset, power cycle or configured GPIO sense. */
    nrf_power_system_off(NRF_POWER);
    for (;;) { __WFE(); }
}
