#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>

const uint8_t data_tosend = 0x16;

#define DATA_NODE DT_PATH(zephyr_user)
static const struct gpio_dt_spec data = GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);


#define TIMER_NODE DT_NODELABEL(t_bit)
#define HALF_BIT_US 500



static const struct device *timer = DEVICE_DT_GET(TIMER_NODE);
static struct counter_alarm_cfg alarm_cfg;

enum tx_state {
    PREAMBLE,
    DATA
};
static uint8_t bit_index = 7;
static uint8_t half = 0;
static uint8_t count_preamble = 0;
static enum tx_state state = PREAMBLE;

static void timer_callback(const struct device *dev,
                           uint8_t chan_id,
                           uint32_t ticks,
                           void *user_data)
{
    if (state == PREAMBLE){
        gpio_pin_set_dt(&data, 0);
        count_preamble++;
        if (count_preamble >= 6){

            count_preamble = 0;
            half = 0;
            bit_index = 7;

            state = DATA;
        }
    } else if (state == DATA){
        uint8_t bit = (data_tosend >> bit_index) & 0x01;
        if (half == 0){
            gpio_pin_set_dt(&data, bit);
            half = 1;
        }else{
            gpio_pin_set_dt(&data, !bit);
            half = 0;
            if (bit_index == 0){
                state = PREAMBLE;
            }else{
                bit_index--;
            }
        }
    }
    counter_set_channel_alarm(dev, chan_id, &alarm_cfg);
}

int main(void)
{
	printf("------ Prog Start ------\n");



    if (!gpio_is_ready_dt(&data)) {
        return -1;
    }
    gpio_pin_configure_dt(&data, GPIO_OUTPUT_INACTIVE);


    if (!device_is_ready(timer)) {
        printf("Timer non disponible\n");
        return -1;
    }

    uint32_t ticks = counter_us_to_ticks(timer, HALF_BIT_US);

    printf("Frequency = %u Hz\n", counter_get_frequency(timer));
    printf("500 us = %u ticks\n", ticks);

    alarm_cfg.flags = 0;
    alarm_cfg.ticks = ticks;
    alarm_cfg.callback = timer_callback;
    alarm_cfg.user_data = NULL;

    counter_set_channel_alarm(timer, 0, &alarm_cfg);

    counter_start(timer);

    while (1) {

        // for (int i = 0; i < 3; i++){
        //         gpio_pin_set_dt(&data, 0);
        //         k_usleep(2*HALF_BIT_US);
        // }

        // for (int i = 0; i < 8; i++){

        //     gpio_pin_set_dt(&data, data_tosend >> i & 0x01);
        //     k_usleep(HALF_BIT_US);

        //     gpio_pin_set_dt(&data, !(data_tosend >> i & 0x01));
        //     k_usleep(HALF_BIT_US);

        // }
        k_sleep(K_FOREVER);
    }

	return 0;
}
