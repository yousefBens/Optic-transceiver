#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>
#include <stm32_ll_tim.h>

#ifndef RX_EDGE_IRQ
#define RX_EDGE_IRQ 0
#endif

#ifndef RX_FAST_GPIO
#define RX_FAST_GPIO 0
#endif
#if RX_FAST_GPIO
#include <stm32_ll_gpio.h>
#endif

#ifndef HALF_BIT_US
#define HALF_BIT_US 48U
#endif
#ifndef SAMPLE_US
#define SAMPLE_US 12U
#endif
#ifndef ACQUISITION_MS
#define ACQUISITION_MS 60000U
#endif

#ifndef DIAG_SAMPLE_COUNT
#define DIAG_SAMPLE_COUNT 0U
#endif

#define PREAMBLE                0x55U
#define SFD                     0xD3U

#define INTER_FRAME_HALFS       4U

#define PAYLOAD_SIZE            3U

#define SAMPLES_PER_HALF        (HALF_BIT_US / SAMPLE_US)

#define GAP_MIN_SAMPLES         \
    ((3U * HALF_BIT_US + SAMPLE_US - 1U) / SAMPLE_US)

#define FRAME_HALFS             \
    (INTER_FRAME_HALFS + 16U + 16U + (PAYLOAD_SIZE * 16U))

#define DATA_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec data =
    GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);

#define TIMER_NODE DT_NODELABEL(t_bit)

static const struct device *timer =
    DEVICE_DT_GET(TIMER_NODE);

static struct counter_top_cfg top_cfg;

static uint32_t sample_ticks;

static const uint8_t expected_payload[PAYLOAD_SIZE] = {
    0xFF,
    0x16,
    0xC1
};

enum rx_state {
    RX_SEARCH_GAP,
    RX_PREAMBLE,
    RX_SFD,
    RX_PAYLOAD
};

static enum rx_state state = RX_SEARCH_GAP;

static uint16_t high_count_gap = 0;

static uint8_t sample_in_half = 0;
static uint8_t high_count = 0;
static uint8_t center_level = 0;

static uint8_t first_half = 0;
static uint8_t half_index = 0;

static uint8_t current_byte = 0;
static uint8_t bit_count = 0;

static uint8_t payload_index = 0;

static uint8_t payload[PAYLOAD_SIZE];

#if DIAG_SAMPLE_COUNT > 0
static uint8_t diagnostic_samples[DIAG_SAMPLE_COUNT];
#endif
static volatile uint32_t sample_count = 0;
static volatile uint32_t gpio_errors = 0;
static volatile uint32_t payload_bit_errors = 0;

static volatile uint32_t gap_count = 0;

static volatile uint32_t frame_count = 0;
static volatile uint32_t frame_ok = 0;
static volatile uint32_t frame_bad = 0;

static volatile uint32_t manchester_errors = 0;
static volatile uint32_t preamble_errors = 0;
static volatile uint32_t sfd_errors = 0;

static void reset_decoder(void)
{
    state = RX_SEARCH_GAP;

    high_count_gap = 0;

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

            bool valid = true;

            frame_count++;

            for (uint8_t i = 0; i < PAYLOAD_SIZE; i++) {

                if (payload[i] != expected_payload[i]) {
                    valid = false;
                    uint8_t diff = payload[i] ^ expected_payload[i];
                    while (diff != 0U) {
                        payload_bit_errors += diff & 1U;
                        diff >>= 1;
                    }
                }
            }

            if (valid) {
                frame_ok++;
            } else {
                frame_bad++;
            }

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

    current_byte =
        (uint8_t)((current_byte << 1) | bit);

    bit_count++;

    if (bit_count >= 8U) {

        uint8_t value = current_byte;

        current_byte = 0;
        bit_count = 0;

        process_byte(value);
    }
}

#ifndef RX_EDGE_DECODER
#define RX_EDGE_DECODER 0
#endif

#if RX_EDGE_DECODER
static uint8_t run_level;
static uint16_t run_samples;
static uint8_t run_emitted;

static void process_sample(uint8_t level)
{
    if (state == RX_SEARCH_GAP) {
        if (level != 0U) {
            if (high_count_gap < UINT16_MAX) {
                high_count_gap++;
            }
        } else if (high_count_gap >= GAP_MIN_SAMPLES) {
            gap_count++;
            state = RX_PREAMBLE;
            high_count_gap = 0U;
            run_level = 0U;
            run_samples = 1U;
            run_emitted = 0U;
        } else {
            high_count_gap = 0U;
        }
        return;
    }

    if (level != run_level) {
        /* Every edge re-establishes timing; drift cannot accumulate over a frame. */
        unsigned int halves = (run_samples + SAMPLES_PER_HALF / 2U) /
                              SAMPLES_PER_HALF;
        if (halves < 1U || halves > 2U || halves < run_emitted) {
            manchester_errors++;
            reset_decoder();
        } else {
            for (unsigned int i = run_emitted; i < halves; i++) {
                process_half(run_level);
                if (state == RX_SEARCH_GAP) {
                    break;
                }
            }
        }
        run_level = level;
        run_samples = 1U;
        run_emitted = 0U;
    } else {
        run_samples++;
    }

    if (state == RX_SEARCH_GAP) {
        high_count_gap = level != 0U ? 1U : 0U;
        return;
    }

    /* Complete a byte's last half even when HIGH continues into the frame gap. */
    if (run_emitted == 0U && run_samples >= SAMPLES_PER_HALF &&
        bit_count == 7U && half_index == 1U && run_level != first_half) {
        run_emitted = 1U;
        process_half(run_level);
        if (state == RX_SEARCH_GAP) {
            high_count_gap = level != 0U ? 1U : 0U;
            return;
        }
    }

    if (run_samples > (5U * SAMPLES_PER_HALF) / 2U) {
        manchester_errors++;
        reset_decoder();
        high_count_gap = level != 0U ? 1U : 0U;
    }
}
#else
static void process_sample(uint8_t level)
{
    if (state == RX_SEARCH_GAP) {

        if (level == 1U) {

            if (high_count_gap < UINT16_MAX) {
                high_count_gap++;
            }

            return;
        }

        if (high_count_gap >= GAP_MIN_SAMPLES) {

            gap_count++;

            state = RX_PREAMBLE;

            center_level = 0U;
            sample_in_half = 1U;
            high_count = 0U;

            first_half = 0U;
            half_index = 0U;

            current_byte = 0U;
            bit_count = 0U;

            payload_index = 0U;
            high_count_gap = 0U;

            return;
        }

        high_count_gap = 0U;
        return;
    }

    if (sample_in_half == ((SAMPLES_PER_HALF - 1U) / 2U)) {
        center_level = level;
    }

    if (level != 0U) {
        high_count++;
    }

    sample_in_half++;

    if (sample_in_half >= SAMPLES_PER_HALF) {

        uint8_t half_level;

        /* A tied vote must not systematically favor HIGH (short LOW pulses). */
        if (2U * high_count == SAMPLES_PER_HALF) {
            half_level = center_level;
        } else {
            half_level = (2U * high_count > SAMPLES_PER_HALF) ? 1U : 0U;
        }

        sample_in_half = 0U;
        high_count = 0U;

        process_half(half_level);
    }
}

#endif

#if RX_EDGE_IRQ
static uint8_t previous_edge_level;
static uint32_t rx_half_ticks;
static volatile uint32_t edges_acquired;

static void process_edge(uint8_t level, uint32_t duration)
{
    uint8_t previous = previous_edge_level;
    previous_edge_level = level;
    edges_acquired++;
    if (level == previous) {
        if (state == RX_SEARCH_GAP) {
            return;
        }
        manchester_errors++;
        reset_decoder();
        return;
    }

    if (state != RX_SEARCH_GAP) {
        unsigned int halves = (duration + rx_half_ticks / 2U) / rx_half_ticks;
        /* A final HIGH half may merge with the idle HIGH interval. */
        if (halves > 2U && previous != 0U && state == RX_PAYLOAD &&
            payload_index == PAYLOAD_SIZE - 1U && bit_count == 7U &&
            half_index == 1U && first_half == 0U) {
            process_half(1U);
        } else if (halves < 1U || halves > 2U) {
            manchester_errors++;
            reset_decoder();
        } else {
            for (unsigned int i = 0; i < halves; i++) {
                process_half(previous);
                if (state == RX_SEARCH_GAP) {
                    break;
                }
            }
        }
    }

    if (state == RX_SEARCH_GAP && previous != 0U && level == 0U &&
        duration >= 3U * rx_half_ticks) {
        gap_count++;
        state = RX_PREAMBLE;
    }
}
#endif

static void timer_callback(const struct device *dev,
                           void *user_data)
{
#if RX_FAST_GPIO
    /* This board uses an active-high local GPIO; read IDR without driver dispatch. */
    int level = LL_GPIO_IsInputPinSet(
        (GPIO_TypeDef *)DT_REG_ADDR(DT_GPIO_CTLR(DATA_NODE, data_gpios)),
        BIT(DT_GPIO_PIN(DATA_NODE, data_gpios)));
#else
    int level = gpio_pin_get_dt(&data);
#endif

    if (level < 0) {
        gpio_errors++;
        return;
    }

#if DIAG_SAMPLE_COUNT > 0
    if (sample_count < DIAG_SAMPLE_COUNT) {
        diagnostic_samples[sample_count] = (uint8_t)level;
    }
#endif
    sample_count++;

    process_sample((uint8_t)level);
}

#if RX_EDGE_IRQ
static struct gpio_callback edge_callback;
static uint32_t last_edge_ticks;
static bool edge_initialized;

static void gpio_edge_callback(const struct device *port, struct gpio_callback *cb,
                               gpio_port_pins_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    /* TIM2 runs freely at 80 MHz; unsigned subtraction handles its rollover. */
    uint32_t now = LL_TIM_GetCounter(
        (TIM_TypeDef *)DT_REG_ADDR(DT_PARENT(TIMER_NODE)));
    uint32_t duration = now - last_edge_ticks;
    last_edge_ticks = now;
    int level = gpio_pin_get_dt(&data);
    if (level < 0) {
        gpio_errors++;
        reset_decoder();
        return;
    }
    if (!edge_initialized) {
        /* The first IRQ may have been pending before EXTI was enabled. */
        previous_edge_level = (uint8_t)level;
        edge_initialized = true;
        return;
    }
    process_edge((uint8_t)level, duration);
}
#endif

int main(void)
{
    int ret;

    uint32_t start_ms;
    uint32_t elapsed_ms;

    uint32_t bitrate;
    uint32_t frame_duration_us;

    uint32_t expected_frames;
    uint32_t goodput_bps;

    uint32_t success_permille;
    uint32_t accepted_permille;

    if ((HALF_BIT_US % SAMPLE_US) != 0U ||
        SAMPLES_PER_HALF < 2U) {

        printk("ERROR: Invalid sampling configuration\n");
        return -1;
    }

    if (!gpio_is_ready_dt(&data)) {

        printk("ERROR: GPIO not ready\n");
        return -1;
    }

    ret = gpio_pin_configure_dt(
        &data,
        GPIO_INPUT | GPIO_PULL_UP
    );

    if (ret < 0) {

        printk("ERROR: GPIO configuration: %d\n", ret);
        return -1;
    }

    if (!device_is_ready(timer)) {

        printk("ERROR: Timer not ready\n");
        return -1;
    }

    sample_ticks =
        counter_us_to_ticks(timer, SAMPLE_US);

    if (sample_ticks == 0U) {

        printk("ERROR: Invalid sample ticks\n");
        return -1;
    }

    bitrate =
        1000000U / (2U * HALF_BIT_US);

    frame_duration_us =
        FRAME_HALFS * HALF_BIT_US;

    printk("\n");
    printk("====================================\n");
    printk("      MANCHESTER RX TEST\n");
    printk("====================================\n");

    printk("Half bit        = %u us\n", HALF_BIT_US);
#if RX_EDGE_IRQ
    printk("Acquisition mode = GPIO edges + free-running TIM2\n");
#else
    printk("Acquisition mode = periodic sampling\n");
    printk("Sample period   = %u us\n", SAMPLE_US);
#endif

#if !RX_EDGE_IRQ
    printk("Samples / half  = %u\n", SAMPLES_PER_HALF);
#endif

    printk("Bitrate         = %u bit/s\n", bitrate);

    printk("Frame duration  = %u us\n",
           frame_duration_us);

    printk("Payload size    = %u bytes\n",
           PAYLOAD_SIZE);

    printk("Acquisition     = %u ms\n",
           ACQUISITION_MS);

    printk("Timer frequency = %u Hz\n",
           counter_get_frequency(timer));

    printk("Sample ticks    = %u\n", sample_ticks);

    /* STM32 counts 0..ARR inclusive; the Zephyr driver writes ticks to ARR. */
#if RX_EDGE_IRQ
    top_cfg.ticks = counter_get_max_top_value(timer);
#else
    top_cfg.ticks = sample_ticks - 1U;
#endif
#if RX_EDGE_IRQ
    top_cfg.callback = NULL;
#else
    top_cfg.callback = timer_callback;
#endif
    top_cfg.user_data = NULL;
    top_cfg.flags = 0;

    ret = counter_set_top_value(timer, &top_cfg);

    if (ret < 0) {

        printk("ERROR: Timer configuration: %d\n", ret);
        return -1;
    }

    reset_decoder();

    printk("\nStarting acquisition...\n");

    start_ms = k_uptime_get_32();

    ret = counter_start(timer);

    if (ret < 0) {

        printk("ERROR: Timer start: %d\n", ret);
        return -1;
    }

#if RX_EDGE_IRQ
    rx_half_ticks = counter_us_to_ticks(timer, HALF_BIT_US);
    previous_edge_level = (uint8_t)gpio_pin_get_dt(&data);
    last_edge_ticks = LL_TIM_GetCounter(
        (TIM_TypeDef *)DT_REG_ADDR(DT_PARENT(TIMER_NODE)));
    gpio_init_callback(&edge_callback, gpio_edge_callback, BIT(data.pin));
    ret = gpio_add_callback(data.port, &edge_callback);
    if (ret == 0) {
        ret = gpio_pin_interrupt_configure_dt(&data, GPIO_INT_EDGE_BOTH);
    }
    if (ret < 0) {
        counter_stop(timer);
        printk("ERROR: GPIO edge acquisition: %d\n", ret);
        return -1;
    }
#endif

    k_sleep(K_MSEC(ACQUISITION_MS));

#if RX_EDGE_IRQ
    gpio_pin_interrupt_configure_dt(&data, GPIO_INT_DISABLE);
#endif

    ret = counter_stop(timer);

    elapsed_ms = k_uptime_get_32() - start_ms;

    if (ret < 0) {

        printk("ERROR: Timer stop: %d\n", ret);
        return -1;
    }

    if (elapsed_ms == 0U) {
        printk("ERROR: Invalid acquisition duration\n");
        return -1;
    }

#if DIAG_SAMPLE_COUNT > 0
    printk("SAMPLES,");
    for (uint32_t i = 0; i < MIN(sample_count, DIAG_SAMPLE_COUNT); i++) {
        printk("%u", diagnostic_samples[i]);
    }
    printk("\n");
#endif

    expected_frames =
        (uint32_t)(((uint64_t)elapsed_ms * 1000ULL) /
                   frame_duration_us);

    goodput_bps =
        (uint32_t)(((uint64_t)frame_ok *
                    PAYLOAD_SIZE * 8ULL * 1000ULL) /
                   elapsed_ms);

    success_permille = 0U;
    accepted_permille = 0U;

    if (expected_frames > 0U) {

        success_permille =
            (uint32_t)(((uint64_t)frame_ok * 1000ULL) /
                       expected_frames);

        accepted_permille =
            (uint32_t)(((uint64_t)frame_count * 1000ULL) /
                       expected_frames);
    }

    printk("\n");
    printk("====================================\n");
    printk("          FINAL RESULTS\n");
    printk("====================================\n");

    printk("Duration          : %u ms\n", elapsed_ms);
    printk("Half bit          : %u us\n", HALF_BIT_US);
#if RX_EDGE_IRQ
    printk("Edges acquired    : %u\n", edges_acquired);
#else
    printk("Sample period     : %u us\n", SAMPLE_US);
#endif
    printk("Bitrate           : %u bit/s\n", bitrate);

    printk("Expected frames   : %u\n", expected_frames);
    printk("Complete frames   : %u\n", frame_count);

    printk("Correct frames    : %u\n", frame_ok);
    printk("Incorrect frames  : %u\n", frame_bad);

    printk("GAP detected      : %u\n", gap_count);

    printk("Manchester errors : %u\n",
           manchester_errors);

    printk("Preamble errors   : %u\n",
           preamble_errors);

    printk("SFD errors        : %u\n",
           sfd_errors);

    printk("GPIO errors       : %u\n",
           gpio_errors);

    printk("Samples acquired  : %u\n",
           sample_count);

    printk("Payload bit errors: %u (complete frames only)\n", payload_bit_errors);

    printk("Goodput           : %u bit/s\n",
           goodput_bps);

    printk("Success estimate  : %u.%u %%\n",
           success_permille / 10U,
           success_permille % 10U);

    printk("Accepted estimate : %u.%u %%\n",
           accepted_permille / 10U,
           accepted_permille % 10U);

    printk("\n");
    printk("====================================\n");
    printk("             CSV RESULT\n");
    printk("====================================\n");

    printk(
        "half_bit_us,sample_us,duration_ms,"
        "bitrate_bps,expected_frames,"
        "complete_frames,correct_frames,"
        "incorrect_frames,gap_detected,"
        "manchester_errors,preamble_errors,"
        "sfd_errors,gpio_errors,"
        "samples_acquired,goodput_bps,"
        "success_permille,payload_bit_errors,rx_mode,edges_acquired\n"
    );

    printk(
        "%u,%u,%u,%u,%u,%u,%u,%u,%u,"
        "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",

        HALF_BIT_US,
        RX_EDGE_IRQ ? 0U : SAMPLE_US,
        elapsed_ms,
        bitrate,
        expected_frames,
        frame_count,
        frame_ok,
        frame_bad,
        gap_count,
        manchester_errors,
        preamble_errors,
        sfd_errors,
        gpio_errors,
        sample_count,
        goodput_bps,
        success_permille,
        payload_bit_errors,
        RX_EDGE_IRQ,
#if RX_EDGE_IRQ
        edges_acquired
#else
        0U
#endif
    );

    printk("\nAcquisition completed\n");

    return 0;
}