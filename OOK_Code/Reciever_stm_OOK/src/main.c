#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/irq.h>
#include <stm32_ll_exti.h>
#include <stm32_ll_gpio.h>
#include <stm32_ll_tim.h>
#define BIT_US       3U
#define GAP_BITS     16U
#define HEADER_SIZE  5U
#define SEQ_SIZE     8U
#define PAYLOAD_SIZE 12U
#define FRAME_SIZE   (HEADER_SIZE + SEQ_SIZE + PAYLOAD_SIZE)
#define FRAME_SLOTS  (GAP_BITS + FRAME_SIZE * 8U)

/* OOK direct: 1 = HIGH, 0 = LOW, MSB first. */
static const uint8_t header[] = {0x55, 0x55, 0x55, 0x55, 0xD3};


#define GAP_MIN_BITS 12U
#define ACQUISITION_MS 60000U
#define DATA_NODE      DT_PATH(zephyr_user)
#define TIMER_NODE     DT_NODELABEL(t_bit)
#define DATA_MASK      BIT(DT_GPIO_PIN(DATA_NODE, data_gpios))
#define DATA_PORT      ((GPIO_TypeDef *)DT_REG_ADDR(DT_GPIO_CTLR(DATA_NODE, data_gpios)))
#define TIMESTAMP      ((TIM_TypeDef *)DT_REG_ADDR(DT_PARENT(TIMER_NODE)))

static const struct gpio_dt_spec data = GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);
static const struct device *timer = DEVICE_DT_GET(TIMER_NODE);
static volatile struct {
    uint32_t complete, gaps, timing, preamble, sfd;
    uint32_t lost, duplicates, discontinuities, sequence_errors, unique;
} stats;
static bool receiving, edge_initialized, sequence_initialized;
static uint16_t last_sequence;
static uint32_t received_sequence;
static uint8_t previous_level, current_byte, bit_count, byte_index;
static uint32_t bit_ticks, last_ticks;
/* Last decoded payload; overwritten by the next frame. No expected data pattern. */
static volatile uint8_t received_payload[PAYLOAD_SIZE];
#define EDGE_BUFFER_SIZE 1024U
static struct {
    uint32_t ticks;
    uint8_t level;
} edges[EDGE_BUFFER_SIZE];
static volatile uint32_t edge_head, edge_tail, edge_overflows;

static void reset_decoder(void)
{
    receiving = false;
    current_byte = 0;
    bit_count = 0;
    byte_index = 0;
    received_sequence = 0;
}

static void process_bit(uint8_t level)
{
    current_byte = (current_byte << 1) | level;
    if (++bit_count != 8U) {
        return;
    }
    uint8_t value = current_byte;
    current_byte = 0;
    bit_count = 0;
    if (byte_index < HEADER_SIZE && value != header[byte_index]) {
        if (byte_index < HEADER_SIZE - 1U) {
            stats.preamble++;
        } else {
            stats.sfd++;
        }
        reset_decoder();
        return;
    }
    if (byte_index >= HEADER_SIZE && byte_index < HEADER_SIZE + SEQ_SIZE) {
        if ((value & 0xE1U) != 0xA0U) {
            stats.sequence_errors++;
            reset_decoder();
            return;
        }
        received_sequence = (received_sequence << 4) | ((value >> 1) & 0x0FU);
    }
    if (byte_index >= HEADER_SIZE + SEQ_SIZE) {
        received_payload[byte_index - HEADER_SIZE - SEQ_SIZE] = value;
    }
    if (++byte_index == FRAME_SIZE) {
        uint16_t sequence = received_sequence >> 16;
        if ((uint16_t)received_sequence != (uint16_t)~sequence) {
            stats.sequence_errors++;
            reset_decoder();
            return;
        }
        if (!sequence_initialized) {
            sequence_initialized = true;
            last_sequence = sequence;
            stats.unique++;
        } else {
            uint16_t delta = sequence - last_sequence;
            if (delta == 0U) {
                stats.duplicates++;
            } else if (delta < 0x8000U) {
                stats.lost += delta - 1U;
                stats.unique++;
                last_sequence = sequence;
            } else {
                /* Restart, out-of-order frame or an ambiguous long interruption. */
                stats.discontinuities++;
                last_sequence = sequence;
            }
        }
        stats.complete++;
        reset_decoder();
    }
}

static void process_edge(uint8_t level, uint32_t duration)
{
    uint8_t previous = previous_level;
    previous_level = level;
    if (level == previous) {
        if (receiving) {
            stats.timing++;
            reset_decoder();
        }
        return;
    }
    uint32_t bits = (duration + bit_ticks / 2U) / bit_ticks;
    bool gap = previous != 0U && bits >= GAP_MIN_BITS;
    if (receiving) {
        unsigned int remaining = (FRAME_SIZE - byte_index) * 8U - bit_count;
        unsigned int count = bits;
        /* A final HIGH data run merges with the following HIGH gap. */
        if (gap && bits == remaining + GAP_BITS) {
            count = remaining;
        } else if (gap || bits == 0U || bits > remaining) {
            stats.timing++;
            reset_decoder();
            count = 0;
        }
        for (unsigned int i = 0; i < count && receiving; i++) {
            process_bit(previous);
        }
    }
    if (!receiving && gap && level == 0U) {
        stats.gaps++;
        receiving = true; /* First LOW bit of the 0x55 preamble begins here. */
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
    uint8_t level = LL_GPIO_IsInputPinSet(DATA_PORT, DATA_MASK);
    uint32_t head = edge_head;
    if (head - edge_tail == EDGE_BUFFER_SIZE) {
        edge_overflows++;
        return;
    }
    edges[head & (EDGE_BUFFER_SIZE - 1U)].ticks = now;
    edges[head & (EDGE_BUFFER_SIZE - 1U)].level = level;
    __DMB();
    edge_head = head + 1U;
}

int main(void)
{
    BUILD_ASSERT(DT_GPIO_PIN(DATA_NODE, data_gpios) == 9);
    BUILD_ASSERT(DT_GPIO_FLAGS(DATA_NODE, data_gpios) == GPIO_ACTIVE_HIGH);
    if (!gpio_is_ready_dt(&data) || !device_is_ready(timer)) {
        printk("RX OOK: GPIO ou timer indisponible\n");
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
    bit_ticks = counter_us_to_ticks(timer, BIT_US);
    ret = gpio_pin_interrupt_configure_dt(&data, GPIO_INT_EDGE_BOTH);
    if (ret < 0) {
        goto error;
    }
    irq_disable(EXTI9_5_IRQn);
    irq_connect_dynamic(EXTI9_5_IRQn, 0, edge_irq, NULL, IRQ_ZERO_LATENCY);
    printk("RX OOK pret: PA9, bit %u us, payload %u octets\n"
           "Debut reception pendant %u ms...\n",
           BIT_US, (unsigned int)PAYLOAD_SIZE, ACQUISITION_MS);
    ret = counter_start(timer);
    if (ret < 0) {
        goto error;
    }
    /* Discard setup edges; the first complete gap establishes synchronization. */
    LL_EXTI_ClearFlag_0_31(DATA_MASK);
    last_ticks = LL_TIM_GetCounter(TIMESTAMP);
    previous_level = LL_GPIO_IsInputPinSet(DATA_PORT, DATA_MASK);
    edge_initialized = false;
    uint32_t start = k_uptime_get_32();
    irq_enable(EXTI9_5_IRQn);
    uint32_t seen_overflows = 0;
    do {
        /* Decode outside the ISR; never print while acquiring. */
        for (unsigned int batch = 0; batch < 128U; batch++) {
            if (seen_overflows != edge_overflows) {
                seen_overflows = edge_overflows;
                edge_tail = edge_head;
                reset_decoder();
                edge_initialized = false;
            }
            uint32_t tail = edge_tail;
            if (tail == edge_head) {
                break;
            }
            __DMB();
            uint32_t now = edges[tail & (EDGE_BUFFER_SIZE - 1U)].ticks;
            uint8_t level = edges[tail & (EDGE_BUFFER_SIZE - 1U)].level;
            __DMB();
            edge_tail = tail + 1U;
            uint32_t duration = now - last_ticks;
            last_ticks = now;
            if (!edge_initialized) {
                previous_level = level;
                edge_initialized = true;
            } else {
                process_edge(level, duration);
            }
        }
    } while (k_uptime_get_32() - start < ACQUISITION_MS);
    irq_disable(EXTI9_5_IRQn);
    uint32_t elapsed = k_uptime_get_32() - start;
    gpio_pin_interrupt_configure_dt(&data, GPIO_INT_DISABLE);
    counter_stop(timer);

    uint32_t goodput = (uint64_t)stats.complete * PAYLOAD_SIZE * 8000U / elapsed;
    uint32_t expected = (uint64_t)elapsed * 1000U / (FRAME_SLOTS * BIT_US);
    printk("\nReception OOK terminee\n"
           "Duree                : %u ms\n"
           "Bit                  : %u us\n"
           "Debit logique        : %u bit/s\n"
           "Trames attendues est.: %u\n"
           "GAP detectes         : %u\n"
           "Trames completes     : %u\n"
           "Erreurs duree/fronts : %u\n"
           "Erreurs preambule    : %u\n"
           "Erreurs SFD          : %u\n"
           "Buffer debordements : %u\n"
           "Sequences invalides  : %u\n"
           "Trames uniques       : %u\n"
           "Trames manquantes    : %u\n"
           "Doublons             : %u\n"
           "Discontinuites seq.  : %u\n"
           "Debit utile          : %u bit/s\n",
           elapsed, BIT_US, 1000000U / BIT_US, expected,
           stats.gaps, stats.complete,
           stats.timing, stats.preamble, stats.sfd,
           edge_overflows, stats.sequence_errors, stats.unique, stats.lost, stats.duplicates,
           stats.discontinuities, goodput);
    return 0;
error:
    printk("RX OOK: erreur initialisation %d\n", ret);
    return ret;
}
