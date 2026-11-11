/* HW-954 red LED GPIO scanner. BATTERY-ONLY SPARE BOARD, NO MOTOR/USB. */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <stddef.h>

struct candidate { unsigned char port; unsigned char pin; };
/* Deliberately exclude power gate P0.13, status P0.15, UART 17/20,
 * SWD, USB, NFC and low-frequency crystal pins. No external wiring! */
static const struct candidate pins[] = {
    {0, 6}, {0, 8}, {0, 11}, {0, 12}, {0, 14}, {0, 16},
    {0, 19}, {0, 21}, {0, 22}, {0, 23}, {0, 24}, {0, 25},
    {0, 26}, {0, 27}, {0, 28}, {0, 29}, {0, 30}, {0, 31},
    {1, 0}, {1, 1}, {1, 2}, {1, 4}, {1, 6}, {1, 7}
};

int main(void)
{
    const struct device *g0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    const struct device *g1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
    if (!device_is_ready(g0) || !device_is_ready(g1)) {
        for (;;) { k_sleep(K_FOREVER); }
    }
    (void)gpio_pin_configure(g0, 13, GPIO_OUTPUT_LOW);
    (void)gpio_pin_configure(g0, 15, GPIO_OUTPUT_HIGH);
    /* Five-second lead-in: start stopwatch when battery is connected. */
    k_sleep(K_SECONDS(5));
    for (;;) {
        for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
            const struct device *port = pins[i].port ? g1 : g0;
            unsigned int pin = pins[i].pin;
            /* Each pin gets 8 s: HIGH 3s, LOW 3s, input 2s. */
            (void)gpio_pin_configure(port, pin, GPIO_OUTPUT_HIGH);
            k_sleep(K_SECONDS(3));
            (void)gpio_pin_configure(port, pin, GPIO_OUTPUT_LOW);
            k_sleep(K_SECONDS(3));
            (void)gpio_pin_configure(port, pin, GPIO_INPUT);
            k_sleep(K_SECONDS(2));
        }
        /* 10-second gap between scans. */
        k_sleep(K_SECONDS(10));
    }
}
