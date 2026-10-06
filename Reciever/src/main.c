#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>



#define HALF_BIT_US             40U
#define DATA_BITS               8U


#define PREAMBLE_EDGES          6U


#define PREAMBLE_MIN_US         30U
#define PREAMBLE_MAX_US         50U




#define DATA_NODE DT_PATH(zephyr_user)
static const struct gpio_dt_spec data_arrive = GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);
static struct gpio_callback data_cb;



#define TIMER_NODE DT_NODELABEL(t_bit)
static const struct device *timer = DEVICE_DT_GET(TIMER_NODE);
static struct counter_alarm_cfg alarm_cfg;

static uint32_t half_bit_ticks;

enum rx_state {
    WAIT_PREAMBLE,
    RECEIVE_DATA
};

static volatile enum rx_state state = WAIT_PREAMBLE;


static uint32_t previous_edge_ticks = 0;
static bool first_edge = true;
static uint8_t preamble_count = 0;




static uint8_t half_index = 0;


static uint8_t first_half = 0;


static uint8_t received_byte = 0;


static uint8_t bit_count = 0;



static volatile bool frame_ready = false;
static volatile bool frame_error = false;

static uint8_t completed_byte = 0;



static void reset_receiver(void)
{
    state = WAIT_PREAMBLE;

    first_edge = true;
    preamble_count = 0;

    half_index = 0;

    received_byte = 0;
    bit_count = 0;
}



static bool decode_manchester(uint8_t first,
                              uint8_t second,
                              uint8_t *bit)
{

    if ((first == 0U) && (second == 1U)) {

        *bit = 0U;

        return true;
    }

    if ((first == 1U) && (second == 0U)) {

        *bit = 1U;

        return true;
    }

    return false;
}




static void timer_callback(const struct device *dev,
                           uint8_t chan_id,
                           uint32_t ticks,
                           void *user_data)
{
    uint8_t current_level;
    uint8_t bit;

    int ret;


    if (state != RECEIVE_DATA) {
        return;
    }



    current_level = (uint8_t)gpio_pin_get_dt(&data_arrive);


    if (half_index == 0U) {
        first_half = current_level;
        half_index = 1U;
    } else {

        uint8_t second_half = current_level;

        half_index = 0U;


        if (!decode_manchester(first_half,
                               second_half,
                               &bit)) {

            frame_error = true;

            reset_receiver();

            return;
        }


        received_byte <<= 1;

        received_byte |= bit;

        bit_count++;

        if (bit_count >= DATA_BITS) {

            completed_byte = received_byte;

            frame_ready = true;

            reset_receiver();

            return;
        }
    }


    alarm_cfg.ticks = ticks + half_bit_ticks;


    ret = counter_set_channel_alarm(
        dev,
        chan_id,
        &alarm_cfg
    );

    if (ret < 0) {

        frame_error = true;

        reset_receiver();
    }
}




static void start_sampling(void)
{
    uint32_t now;


    received_byte = 0;

    bit_count = 0;

    half_index = 0;



    counter_get_value(timer, &now);



    alarm_cfg.ticks = now + half_bit_ticks;


    counter_set_channel_alarm(
        timer,
        0,
        &alarm_cfg
    );
}




static void data_callback(const struct device *dev,
                          struct gpio_callback *cb,
                          uint32_t pins)
{
    uint32_t current_ticks;
    uint32_t delta_ticks;

    uint64_t delta_us;


    /*
     * Une fois synchronisé,
     * le timer s'occupe de la DATA.
     */

    if (state != WAIT_PREAMBLE) {
        return;
    }


    /* ========================================================
     * TIMESTAMP FRONT
     * ======================================================== */

    if (counter_get_value(timer, &current_ticks) != 0) {
        return;
    }


    /* ========================================================
     * PREMIER FRONT
     * ======================================================== */

    if (first_edge) {

        previous_edge_ticks = current_ticks;
        first_edge = false;

        return;
    }


    delta_ticks = current_ticks - previous_edge_ticks;
 
    previous_edge_ticks = current_ticks;


    delta_us = counter_ticks_to_us(timer, delta_ticks);



    if ((delta_us >= PREAMBLE_MIN_US) && (delta_us <= PREAMBLE_MAX_US)) {

        preamble_count++;




        if (preamble_count >= PREAMBLE_EDGES) {

            preamble_count = 0;

            state = RECEIVE_DATA;

            gpio_pin_interrupt_configure_dt(&data_arrive, GPIO_INT_DISABLE);

            start_sampling();
        }
    }

    else {
        preamble_count = 0;
    }
}



int main(void)
{
    int ret;


    printk("\n");
    printk("MANCHESTER RECEIVER\n");
    printk("\n");

    if (!gpio_is_ready_dt(&data_arrive)) {
        printk("ERROR : GPIO not ready\n");
        return -1;
    }


    ret = gpio_pin_configure_dt(
        &data_arrive,
        GPIO_INPUT | GPIO_PULL_DOWN
    );

    if (ret < 0) {
        printk("ERROR : GPIO config : %d\n", ret);
        return -1;
    }


    gpio_init_callback(
        &data_cb,
        data_callback,
        BIT(data_arrive.pin)
    );


    ret = gpio_add_callback(
        data_arrive.port,
        &data_cb
    );

    if (ret < 0) {
        printk("ERROR : GPIO callback : %d\n", ret);
        return -1;
    }



    if (!device_is_ready(timer)) {
        printk("ERROR : Timer not ready\n");
        return -1;
    }


    printk("Timer frequency : %u Hz\n", counter_get_frequency(timer));


    half_bit_ticks = counter_us_to_ticks(timer, HALF_BIT_US);


    printk("Half bit       : %u us\n", HALF_BIT_US);
    printk("Half bit ticks : %u\n", half_bit_ticks);


    alarm_cfg.flags = COUNTER_ALARM_CFG_ABSOLUTE;
    alarm_cfg.callback = timer_callback;
    alarm_cfg.user_data = NULL;


    ret = counter_start(timer);

    if (ret < 0) {
        printk("ERROR : Timer start : %d\n", ret);
        return -1;
    }


    ret = gpio_pin_interrupt_configure_dt(&data_arrive, GPIO_INT_EDGE_BOTH);

    if (ret < 0) {
        printk("ERROR : GPIO IRQ : %d\n", ret);
        return -1;
    }


    printk("\nReceiver ready\n");
    printk("Waiting for frame...\n\n");

    while (1) {

        if (frame_ready) {

            frame_ready = false;

            printk("\n------------------------\n");

            printk("Frame received\n");

            printk("Original data : ");


            for (int i = 7; i >= 0; i--) {

                printk(
                    "%u",
                    (completed_byte >> i) & 0x01
                );
            }


            printk("\n");
            printk("HEX           : 0x%02X\n", completed_byte);
            printk("Decimal       : %u\n", completed_byte);
            printk("------------------------\n\n");


            first_edge = true;
            gpio_pin_interrupt_configure_dt(&data_arrive, GPIO_INT_EDGE_BOTH);
        }

        if (frame_error) {

            frame_error = false;

            printk( "Manchester error - frame rejected\n");

            first_edge = true;

            gpio_pin_interrupt_configure_dt(
                &data_arrive,
                GPIO_INT_EDGE_BOTH
            );
        }

        k_sleep(K_MSEC(1));
    }


    return 0;
}