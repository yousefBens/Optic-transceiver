#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>


#define DATA_NODE DT_PATH(zephyr_user)
#define HALF_BIT_US 500

static const struct gpio_dt_spec data = GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);

const uint8_t data_tosend = 0x16;


int main(void)
{
	printf("------ Prog Start ------\n");

    if (!gpio_is_ready_dt(&data)) {
        return -1;
    }

    gpio_pin_configure_dt(&data, GPIO_OUTPUT_INACTIVE);

    while (1) {

        for (int i = 0; i < 3; i++){
                gpio_pin_set_dt(&data, 0);
                k_usleep(2*HALF_BIT_US);
        }

        for (int i = 0; i < 8; i++){

            gpio_pin_set_dt(&data, data_tosend >> i & 0x01);
            k_usleep(HALF_BIT_US);

            gpio_pin_set_dt(&data, !(data_tosend >> i & 0x01));
            k_usleep(HALF_BIT_US);

        }
    }

	return 0;
}
