#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/irq.h>
#include <stm32_ll_exti.h>
#include <stm32_ll_gpio.h>
#include <stm32_ll_tim.h>

#define HALF_BIT_US    3U
#define ACQUISITION_MS 60000U
#define PREAMBLE       0x55U
#define SFD            0xD3U
#define DATA_NODE      DT_PATH(zephyr_user)
#define TIMER_NODE     DT_NODELABEL(t_bit)
#define DATA_MASK      BIT(DT_GPIO_PIN(DATA_NODE, data_gpios))
#define DATA_PORT      ((GPIO_TypeDef *)DT_REG_ADDR(DT_GPIO_CTLR(DATA_NODE, data_gpios)))
#define TIMESTAMP      ((TIM_TypeDef *)DT_REG_ADDR(DT_PARENT(TIMER_NODE)))

static const struct gpio_dt_spec data = GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);
static const struct device *timer = DEVICE_DT_GET(TIMER_NODE);
static const uint8_t expected_payload[] = {0xFF, 0x16, 0xC1};

#define PAYLOAD_SIZE ARRAY_SIZE(expected_payload)
#define FRAME_HALFS  (4U + (2U + PAYLOAD_SIZE) * 16U)

/* Only the ISR writes these counters; main reads them after disabling EXTI. */
static volatile struct {
    uint32_t good, bad, gaps, manchester, preamble, sfd, bit_errors;
} stats;

static enum { SEARCH_GAP, READ_PREAMBLE, READ_SFD, READ_PAYLOAD } state;
static uint8_t first_half, half_index, current_byte, bit_count, payload_index;
static uint8_t frame_bit_errors, previous_level;
static uint32_t half_ticks, last_ticks;
static bool edge_initialized;

static void reset_decoder(void)
{
    state = SEARCH_GAP;
    half_index = 0;
    current_byte = 0;
    bit_count = 0;
    payload_index = 0;
    frame_bit_errors = 0;
}

static void process_byte(uint8_t value)
{
    if (state == READ_PREAMBLE) {
        if (value == PREAMBLE) {
            state = READ_SFD;
        } else {
            stats.preamble++;
            reset_decoder();
        }
    } else if (state == READ_SFD) {
        if (value == SFD) {
            state = READ_PAYLOAD;
        } else {
            stats.sfd++;
            reset_decoder();
        }
    } else {
        uint8_t diff = value ^ expected_payload[payload_index++];
        while (diff != 0U) {
            frame_bit_errors++;
            diff &= diff - 1U;
        }
        if (payload_index == PAYLOAD_SIZE) {
            if (frame_bit_errors == 0U) {
                stats.good++;
            } else {
                stats.bad++;
                stats.bit_errors += frame_bit_errors;
            }
            reset_decoder();
        }
    }
}

static void process_half(uint8_t level)
{
    if (half_index == 0U) {
        first_half = level;
        half_index = 1U;
        return;
    }
    half_index = 0U;
    if (first_half == level) {
        stats.manchester++;
        reset_decoder();
        return;
    }
    current_byte = (current_byte << 1) | first_half; /* 01 -> 0, 10 -> 1. */
    if (++bit_count == 8U) {
        uint8_t value = current_byte;
        current_byte = 0;
        bit_count = 0;
        process_byte(value);
    }
}

static void process_edge(uint8_t level, uint32_t duration)
{
    uint8_t previous = previous_level;
    previous_level = level;
    if (level == previous) {
        if (state != SEARCH_GAP) {
            stats.manchester++;
            reset_decoder();
        }
        return;
    }
    if (state != SEARCH_GAP) {
        unsigned int halves = (duration + half_ticks / 2U) / half_ticks;
        /* A final HIGH half can continue into the HIGH gap. */
        if (halves > 2U && previous != 0U && state == READ_PAYLOAD &&
            payload_index == PAYLOAD_SIZE - 1U && bit_count == 7U &&
            half_index == 1U && first_half == 0U) {
            process_half(1U);
        } else if (halves < 1U || halves > 2U) {
            stats.manchester++;
            reset_decoder();
        } else {
            for (unsigned int i = 0; i < halves; i++) {
                process_half(previous);
                if (state == SEARCH_GAP) {
                    break;
                }
            }
        }
    }
    if (state == SEARCH_GAP && previous != 0U && level == 0U &&
        duration >= 3U * half_ticks) {
        stats.gaps++;
        state = READ_PREAMBLE;
    }
}

static void edge_irq(const void *unused)
{
    ARG_UNUSED(unused);
    if (!LL_EXTI_IsActiveFlag_0_31(DATA_MASK)) {
        return;
    }
    LL_EXTI_ClearFlag_0_31(DATA_MASK);
    uint32_t now = LL_TIM_GetCounter(TIMESTAMP);
    uint32_t duration = now - last_ticks; /* Also valid across TIM2 rollover. */
    last_ticks = now;
    uint8_t level = LL_GPIO_IsInputPinSet(DATA_PORT, DATA_MASK);
    if (!edge_initialized) {
        previous_level = level;
        edge_initialized = true;
        return;
    }
    process_edge(level, duration);
}

int main(void)
{
    /* This application reserves EXTI5..9 for active-high PA9. */
    BUILD_ASSERT(DT_GPIO_PIN(DATA_NODE, data_gpios) == 9);
    BUILD_ASSERT(DT_GPIO_FLAGS(DATA_NODE, data_gpios) == GPIO_ACTIVE_HIGH);
    if (!gpio_is_ready_dt(&data) || !device_is_ready(timer)) {
        printk("RX: GPIO ou timer indisponible\n");
        return -ENODEV;
    }
    int ret = gpio_pin_configure_dt(&data, GPIO_INPUT | GPIO_PULL_UP);
    if (ret < 0) {
        goto error;
    }
    const struct counter_top_cfg cfg = {.ticks = counter_get_max_top_value(timer)};
    ret = counter_set_top_value(timer, &cfg);
    if (ret < 0) {
        goto error;
    }
    half_ticks = counter_us_to_ticks(timer, HALF_BIT_US);
    ret = gpio_pin_interrupt_configure_dt(&data, GPIO_INT_EDGE_BOTH);
    if (ret < 0) {
        goto error;
    }
    irq_disable(EXTI9_5_IRQn);
    irq_connect_dynamic(EXTI9_5_IRQn, 0, edge_irq, NULL, IRQ_ZERO_LATENCY);
    ret = counter_start(timer);
    if (ret < 0) {
        goto error;
    }
    uint32_t start = k_uptime_get_32();
    irq_enable(EXTI9_5_IRQn);
    k_sleep(K_MSEC(ACQUISITION_MS));
    irq_disable(EXTI9_5_IRQn);
    uint32_t elapsed = k_uptime_get_32() - start;
    gpio_pin_interrupt_configure_dt(&data, GPIO_INT_DISABLE);
    counter_stop(timer);

    uint32_t goodput = (uint64_t)stats.good * PAYLOAD_SIZE * 8000U / elapsed;
    uint32_t expected = (uint64_t)elapsed * 1000U / (FRAME_HALFS * HALF_BIT_US);
    printk("\nDuree                : %u ms\n"
           "Demi-bit             : %u us\n"
           "Debit logique        : %u bit/s\n"
           "Trames attendues est.: %u\n"
           "GAP detectes         : %u\n"
           "Trames completes     : %u\n"
           "Trames correctes     : %u\n"
           "Payloads incorrects  : %u\n"
           "Erreurs Manchester   : %u\n"
           "Erreurs preambule    : %u\n"
           "Erreurs SFD          : %u\n"
           "Bits payload errones : %u\n"
           "Debit utile          : %u bit/s\n",
           elapsed, HALF_BIT_US, 1000000U / (2U * HALF_BIT_US), expected,
           stats.gaps, stats.good + stats.bad, stats.good, stats.bad,
           stats.manchester, stats.preamble, stats.sfd, stats.bit_errors, goodput);
    return 0;

error:
    printk("RX: erreur initialisation %d\n", ret);
    return ret;
}
