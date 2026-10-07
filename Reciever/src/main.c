#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>


#define SAMPLE_US               10U
#define SAMPLES_PER_HALF        4U
#define GAP_MIN_SAMPLES         12U

#define PREAMBLE                0xAA
#define SFD                     0xD3

#define PAYLOAD_SIZE            3U

#define RING_SIZE               1024U
#define RING_MASK               (RING_SIZE - 1U)


#define DATA_NODE DT_PATH(zephyr_user)
static const struct gpio_dt_spec data = GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);


#define TIMER_NODE DT_NODELABEL(t_bit)
static const struct device *timer = DEVICE_DT_GET(TIMER_NODE);


static struct counter_top_cfg top_cfg;
static uint32_t sample_ticks;


static volatile uint8_t sample_buffer[RING_SIZE];

static volatile uint16_t write_index = 0;
static volatile uint16_t read_index = 0;

static volatile uint32_t overflow_count = 0;


enum rx_state {
    RX_SEARCH_GAP,
    RX_PREAMBLE,
    RX_SFD,
    RX_PAYLOAD
};


static enum rx_state state = RX_SEARCH_GAP;


static uint16_t low_count = 0;

static uint8_t sample_in_half = 0;
static uint8_t high_count = 0;

static uint8_t first_half = 0;
static uint8_t half_index = 0;

static uint8_t current_byte = 0;
static uint8_t bit_count = 0;

static uint8_t payload_index = 0;


static uint8_t payload[PAYLOAD_SIZE];


static uint32_t frame_count = 0;
static uint32_t manchester_errors = 0;
static uint32_t preamble_errors = 0;
static uint32_t sfd_errors = 0;


static void timer_callback(const struct device *dev,
                           void *user_data)
{
    int level;
    uint16_t next;

    level = gpio_pin_get_dt(&data);

    if (level < 0) {
        return;
    }

    next = (write_index + 1U) & RING_MASK;

    if (next == read_index) {
        overflow_count++;
        return;
    }

    sample_buffer[write_index] = (uint8_t)level;
    write_index = next;
}


static void reset_decoder(void)
{
    state = RX_SEARCH_GAP;

    low_count = 0;

    sample_in_half = 0;
    high_count = 0;

    first_half = 0;
    half_index = 0;

    current_byte = 0;
    bit_count = 0;

    payload_index = 0;
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


static void process_byte(uint8_t value)
{
    if (state == RX_PREAMBLE) {

        if (value != PREAMBLE) {
            preamble_errors++;
            reset_decoder();
            return;
        }

        state = RX_SFD;

        return;
    }


    if (state == RX_SFD) {

        if (value != SFD) {
            sfd_errors++;
            reset_decoder();
            return;
        }

        state = RX_PAYLOAD;
        payload_index = 0;

        return;
    }


    if (state == RX_PAYLOAD) {

        payload[payload_index] = value;
        payload_index++;

        if (payload_index >= PAYLOAD_SIZE) {

            frame_count++;

            printk("Frame %u : ", frame_count);

            for (uint8_t i = 0; i < PAYLOAD_SIZE; i++) {
                printk("%02X ", payload[i]);
            }

            printk("\n");

            reset_decoder();
        }
    }
}


static void process_half(uint8_t level)
{
    uint8_t bit;

    if (half_index == 0U) {
        first_half = level;
        half_index = 1U;
        return;
    }

    half_index = 0U;

    if (!decode_manchester(first_half, level, &bit)) {
        manchester_errors++;
        reset_decoder();
        return;
    }

    current_byte = (current_byte << 1) | bit;

    bit_count++;

    if (bit_count >= 8U) {

        uint8_t value = current_byte;

        current_byte = 0;
        bit_count = 0;

        process_byte(value);
    }
}


static void process_sample(uint8_t level)
{
    if (state == RX_SEARCH_GAP) {

        if (level == 0U) {

            if (low_count < 0xFFFFU) {
                low_count++;
            }

            return;
        }

        if (low_count >= GAP_MIN_SAMPLES) {

            state = RX_PREAMBLE;

            sample_in_half = 1U;
            high_count = 1U;

            first_half = 0;
            half_index = 0;

            current_byte = 0;
            bit_count = 0;

            payload_index = 0;
            low_count = 0;

            return;
        }

        low_count = 0;

        return;
    }


    if (level != 0U) {
        high_count++;
    }

    sample_in_half++;

    if (sample_in_half >= SAMPLES_PER_HALF) {

        uint8_t half_level;

        half_level = (high_count >= 2U) ? 1U : 0U;

        sample_in_half = 0;
        high_count = 0;

        process_half(half_level);
    }
}


int main(void)
{
    int ret;

    printk("\n");
    printk("OPV4COM Manchester RX\n");
    printk("\n");


    if (!gpio_is_ready_dt(&data)) {
        printk("ERROR : GPIO not ready\n");
        return -1;
    }


    ret = gpio_pin_configure_dt(&data, GPIO_INPUT | GPIO_PULL_DOWN);

    if (ret < 0) {
        printk("ERROR : GPIO configuration : %d\n", ret);
        return -1;
    }


    if (!device_is_ready(timer)) {
        printk("ERROR : Timer not ready\n");
        return -1;
    }


    sample_ticks = counter_us_to_ticks(timer, SAMPLE_US);


    printk("Timer frequency = %u Hz\n", counter_get_frequency(timer));
    printk("Sample period = %u us\n", SAMPLE_US);
    printk("Sample ticks = %u\n", sample_ticks);
    printk("Samples / half bit = %u\n", SAMPLES_PER_HALF);
    printk("Payload size = %u bytes\n", PAYLOAD_SIZE);


    top_cfg.ticks = sample_ticks;
    top_cfg.callback = timer_callback;
    top_cfg.user_data = NULL;
    top_cfg.flags = 0;


    ret = counter_set_top_value(timer, &top_cfg);

    if (ret < 0) {
        printk("ERROR : Timer top : %d\n", ret);
        return -1;
    }


    ret = counter_start(timer);

    if (ret < 0) {
        printk("ERROR : Timer start : %d\n", ret);
        return -1;
    }


    printk("\n");
    printk("Receiver running\n");
    printk("Waiting for frame...\n");
    printk("\n");


    while (1) {

        while (read_index != write_index) {

            uint8_t sample;

            sample = sample_buffer[read_index];
            read_index = (read_index + 1U) & RING_MASK;

            process_sample(sample);
        }

        k_yield();
    }


    return 0;
}