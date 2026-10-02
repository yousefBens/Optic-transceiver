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

uint32_t half_bit_ticks;
uint32_t next_alarm;

uint8_t bit_index = 7;
uint8_t half = 0;

enum tx_state {
    PREAMBLE,
    DATA
};

enum tx_state state = PREAMBLE;
uint8_t count_preamble = 0;


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

        if (half == 0){

            gpio_pin_set_dt(&data, data_tosend >> bit_index & 0x01);
            half = 1;

        }else if (half == 1){

            gpio_pin_set_dt(&data, !(data_tosend >> bit_index & 0x01));
            half = 0;

            if (bit_index == 0){

                state = PREAMBLE;

            }else{

                bit_index--;
            }
        }
    }

    next_alarm += half_bit_ticks;
    alarm_cfg.ticks = next_alarm;

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


    half_bit_ticks = counter_us_to_ticks(timer, HALF_BIT_US);

    printf("Frequency = %u Hz\n", counter_get_frequency(timer));
    printf("500 us = %u ticks\n", half_bit_ticks);


    alarm_cfg.flags = COUNTER_ALARM_CFG_ABSOLUTE;
    alarm_cfg.callback = timer_callback;
    alarm_cfg.user_data = NULL;


    counter_start(timer);


    counter_get_value(timer, &next_alarm);

    next_alarm += half_bit_ticks;

    alarm_cfg.ticks = next_alarm;


    counter_set_channel_alarm(timer, 0, &alarm_cfg);


    while (1) {

        k_sleep(K_FOREVER);
    }


    return 0;
}