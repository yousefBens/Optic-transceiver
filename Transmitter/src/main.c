#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>



#define HALF_BIT_US            40U

#define PREAMBLE               0xAA

#define SFD                    0xD3

#define INTER_FRAME_HALFS      4U



static const uint8_t payload[] = {
    0xFF,
    0x16,
    0xC1
};


#define PAYLOAD_SIZE           sizeof(payload)



#define DATA_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec data =
    GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);



#define TIMER_NODE DT_NODELABEL(t_bit)

static const struct device *timer =
    DEVICE_DT_GET(TIMER_NODE);



static struct counter_top_cfg top_cfg;


static uint32_t half_bit_ticks;



enum tx_state {
    TX_GAP,
    TX_PREAMBLE,
    TX_SFD,
    TX_PAYLOAD
};


static enum tx_state state =
    TX_GAP;



static uint8_t gap_index = 0;


static uint8_t current_byte = 0;

static uint8_t bit_index = 7;

static uint8_t half = 0;


static uint16_t payload_index = 0;



static void load_byte(uint8_t value)
{
    current_byte =
        value;


    bit_index =
        7;


    half =
        0;
}



static bool send_manchester_half(void)
{
    uint8_t bit;

    uint8_t level;



    bit =
        (current_byte >> bit_index) &
        0x01U;



    if (half == 0U) {


        level =
            bit;


        half =
            1U;
    }


    else {


        level =
            !bit;


        half =
            0U;
    }



    gpio_pin_set_dt(
        &data,
        level
    );



    if (half != 0U) {

        return false;
    }



    if (bit_index == 0U) {

        return true;
    }



    bit_index--;


    return false;
}



static void timer_callback(const struct device *dev,
                           void *user_data)
{

    if (state ==
        TX_GAP) {


        gpio_pin_set_dt(
            &data,
            0
        );


        gap_index++;



        if (gap_index >=
            INTER_FRAME_HALFS) {


            gap_index =
                0;


            load_byte(
                PREAMBLE
            );


            state =
                TX_PREAMBLE;
        }



        return;
    }



    if (state ==
        TX_PREAMBLE) {


        if (send_manchester_half()) {


            load_byte(
                SFD
            );


            state =
                TX_SFD;
        }



        return;
    }



    if (state ==
        TX_SFD) {


        if (send_manchester_half()) {


            payload_index =
                0;


            load_byte(
                payload[payload_index]
            );


            state =
                TX_PAYLOAD;
        }



        return;
    }



    if (state ==
        TX_PAYLOAD) {


        if (send_manchester_half()) {


            payload_index++;



            if (payload_index >=
                PAYLOAD_SIZE) {


                gpio_pin_set_dt(
                    &data,
                    0
                );


                gap_index =
                    0;


                state =
                    TX_GAP;
            }


            else {


                load_byte(
                    payload[payload_index]
                );
            }
        }
    }
}



int main(void)
{
    int ret;



    printk("\n");

    printk("OPV4COM Manchester TX - STM32\n");

    printk("\n");



    if (!gpio_is_ready_dt(
            &data)) {


        printk(
            "ERROR : GPIO not ready\n"
        );


        return -1;
    }



    ret = gpio_pin_configure_dt(
        &data,
        GPIO_OUTPUT_INACTIVE
    );



    if (ret < 0) {


        printk(
            "ERROR : GPIO configuration : %d\n",
            ret
        );


        return -1;
    }



    if (!device_is_ready(
            timer)) {


        printk(
            "ERROR : Timer not ready\n"
        );


        return -1;
    }



    half_bit_ticks =
        counter_us_to_ticks(
            timer,
            HALF_BIT_US
        );



    printk(
        "Timer frequency = %u Hz\n",
        counter_get_frequency(
            timer
        )
    );



    printk(
        "Half bit = %u us\n",
        HALF_BIT_US
    );



    printk(
        "Half bit ticks = %u\n",
        half_bit_ticks
    );



    printk(
        "Bit period = %u us\n",
        HALF_BIT_US * 2U
    );



    printk(
        "Bit rate = %u bit/s\n",
        1000000U /
        (HALF_BIT_US * 2U)
    );



    printk(
        "Preamble = 0x%02X\n",
        PREAMBLE
    );



    printk(
        "SFD = 0x%02X\n",
        SFD
    );



    printk(
        "Payload size = %u bytes\n",
        (unsigned int)PAYLOAD_SIZE
    );



    printk(
        "Payload = "
    );



    for (uint16_t i = 0;
         i < PAYLOAD_SIZE;
         i++) {


        printk(
            "%02X ",
            payload[i]
        );
    }



    printk("\n");



    top_cfg.ticks =
        half_bit_ticks;


    top_cfg.callback =
        timer_callback;


    top_cfg.user_data =
        NULL;


    top_cfg.flags =
        0;



    ret = counter_set_top_value(
        timer,
        &top_cfg
    );



    if (ret < 0) {


        printk(
            "ERROR : Timer top configuration : %d\n",
            ret
        );


        return -1;
    }



    ret = counter_start(
        timer
    );



    if (ret < 0) {


        printk(
            "ERROR : Timer start : %d\n",
            ret
        );


        return -1;
    }



    printk("\n");

    printk(
        "Transmitter running\n"
    );

    printk("\n");



    k_sleep(
        K_FOREVER
    );



    return 0;
}