#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>

const uint8_t data_tosend = 0x16;

#define DATA_NODE DT_PATH(zephyr_user)
static const struct gpio_dt_spec data =
    GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);

#define TIMER_NODE DT_NODELABEL(timer1)

#define HALF_BIT_US 40U
#define PREAMBLE_LENGTH 8U

static const struct device *timer =
    DEVICE_DT_GET(TIMER_NODE);

static struct counter_alarm_cfg alarm_cfg;

static uint32_t half_bit_ticks;
static uint32_t next_alarm;

static uint8_t bit_index = 7U;
static uint8_t half = 0U;
static uint8_t count_preamble = 0U;

enum tx_state {
    PREAMBLE,
    DATA
};

static enum tx_state state = PREAMBLE;

static void timer_callback(const struct device *dev,
                           uint8_t chan_id,
                           uint32_t ticks,
                           void *user_data)
{
    uint8_t current_bit;

    if (state == PREAMBLE) {

        gpio_pin_set_dt(&data, count_preamble & 0x01U);

        count_preamble++;

        if (count_preamble >= PREAMBLE_LENGTH) {

            count_preamble = 0U;
            half = 0U;
            bit_index = 7U;

            state = DATA;
        }

    } else {

        current_bit =
            (data_tosend >> bit_index) & 0x01U;

        if (half == 0U) {

            gpio_pin_set_dt(&data, current_bit);

            half = 1U;

        } else {

            gpio_pin_set_dt(&data, !current_bit);

            half = 0U;

            if (bit_index == 0U) {

                count_preamble = 0U;
                state = PREAMBLE;

            } else {

                bit_index--;
            }
        }
    }

    next_alarm += half_bit_ticks;

    alarm_cfg.ticks = next_alarm;

    counter_set_channel_alarm(
        dev,
        chan_id,
        &alarm_cfg
    );
}

int main(void)
{
    int ret;

    printf("------ Prog Start ------\n");

    if (!gpio_is_ready_dt(&data)) {
        printf("GPIO non disponible\n");
        return -1;
    }

    ret = gpio_pin_configure_dt(
        &data,
        GPIO_OUTPUT_INACTIVE
    );

    if (ret < 0) {
        printf("Erreur GPIO : %d\n", ret);
        return -1;
    }

    if (!device_is_ready(timer)) {
        printf("Timer non disponible\n");
        return -1;
    }

    half_bit_ticks =
        counter_us_to_ticks(
            timer,
            HALF_BIT_US
        );

    printf(
        "Frequency = %u Hz\n",
        counter_get_frequency(timer)
    );

    printf(
        "%u us = %u ticks\n",
        HALF_BIT_US,
        half_bit_ticks
    );

    alarm_cfg.flags =
        COUNTER_ALARM_CFG_ABSOLUTE;

    alarm_cfg.callback =
        timer_callback;

    alarm_cfg.user_data =
        NULL;

    ret = counter_start(timer);

    if (ret < 0) {
        printf("Erreur start timer : %d\n", ret);
        return -1;
    }

    ret = counter_get_value(
        timer,
        &next_alarm
    );

    if (ret < 0) {
        printf("Erreur lecture timer : %d\n", ret);
        return -1;
    }

    next_alarm += half_bit_ticks;

    alarm_cfg.ticks = next_alarm;

    ret = counter_set_channel_alarm(
        timer,
        0,
        &alarm_cfg
    );

    if (ret < 0) {
        printf("Erreur alarm : %d\n", ret);
        return -1;
    }

    printf("Transmission de 0x%02X\n", data_tosend);

    k_sleep(K_FOREVER);

    return 0;
}