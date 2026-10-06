#include <stdio.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>



#define HALF_BIT_US        40U
#define PREAMBLE_LENGTH    8U
#define SYNC_LENGTH        1U

static const uint8_t data_tosend = 0x16;



#define DATA_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec data =
    GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);




#define TIMER_NODE DT_NODELABEL(timer1)

static const struct device *timer =
    DEVICE_DT_GET(TIMER_NODE);

static struct counter_alarm_cfg alarm_cfg;

static uint32_t half_bit_ticks;

static uint32_t next_alarm;




enum tx_state {
    PREAMBLE,
    SYNC,
    DATA
};


static enum tx_state state = PREAMBLE;


static uint8_t count_preamble = 0;

static uint8_t count_sync = 0;

static uint8_t bit_index = 7;

static uint8_t half = 0;




static void timer_callback(const struct device *dev,
                           uint8_t chan_id,
                           uint32_t ticks,
                           void *user_data)
{
    uint8_t current_bit;

    int ret;



    if (state == PREAMBLE) {

        gpio_pin_set_dt(
            &data,
            count_preamble & 0x01U
        );


        count_preamble++;


        if (count_preamble >= PREAMBLE_LENGTH) {

            count_preamble = 0;

            count_sync = 0;

            state = SYNC;
        }
    }



    else if (state == SYNC) {

        gpio_pin_set_dt(
            &data,
            1
        );


        count_sync++;


        if (count_sync >= SYNC_LENGTH) {

            count_sync = 0;

            half = 0;

            bit_index = 7;

            state = DATA;
        }
    }



    else if (state == DATA) {

        current_bit =
            (data_tosend >> bit_index) & 0x01U;


        if (half == 0U) {

            gpio_pin_set_dt(
                &data,
                current_bit
            );


            half = 1U;
        }


        else {

            gpio_pin_set_dt(
                &data,
                !current_bit
            );


            half = 0U;


            if (bit_index == 0U) {

                state = PREAMBLE;

                count_preamble = 0;
            }


            else {

                bit_index--;
            }
        }
    }



    next_alarm += half_bit_ticks;


    alarm_cfg.ticks =
        next_alarm;



    ret = counter_set_channel_alarm(
        dev,
        chan_id,
        &alarm_cfg
    );


    (void)ret;
}




int main(void)
{
    int ret;



    printf("\n");

    printf("OPV4COM Manchester TX\n");

    printf("\n");



    if (!gpio_is_ready_dt(&data)) {

        printf(
            "ERROR : GPIO not ready\n"
        );

        return -1;
    }



    ret = gpio_pin_configure_dt(
        &data,
        GPIO_OUTPUT_INACTIVE
    );



    if (ret < 0) {

        printf(
            "ERROR : GPIO configuration : %d\n",
            ret
        );

        return -1;
    }



    if (!device_is_ready(timer)) {

        printf(
            "ERROR : TIMER1 not ready\n"
        );

        return -1;
    }



    half_bit_ticks =
        counter_us_to_ticks(
            timer,
            HALF_BIT_US
        );



    printf(
        "Timer frequency = %u Hz\n",
        counter_get_frequency(timer)
    );



    printf(
        "Half bit = %u us = %u ticks\n",
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

        printf(
            "ERROR : TIMER1 start : %d\n",
            ret
        );

        return -1;
    }



    ret = counter_get_value(
        timer,
        &next_alarm
    );



    if (ret < 0) {

        printf(
            "ERROR : TIMER1 read : %d\n",
            ret
        );

        return -1;
    }



    next_alarm +=
        half_bit_ticks;


    alarm_cfg.ticks =
        next_alarm;



    ret = counter_set_channel_alarm(
        timer,
        0,
        &alarm_cfg
    );



    if (ret < 0) {

        printf(
            "ERROR : TIMER1 alarm : %d\n",
            ret
        );

        return -1;
    }



    printf(
        "Transmitter running\n"
    );


    printf(
        "Data = 0x%02X\n",
        data_tosend
    );



    k_sleep(
        K_FOREVER
    );


    return 0;
}