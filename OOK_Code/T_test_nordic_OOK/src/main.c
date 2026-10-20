#include <zephyr/kernel.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <nrfx_pwm.h>
#include <zephyr/irq.h>
#include <nrfx_glue.h>

#define BIT_US       3U
#define GAP_BITS     16U
#define HEADER_SIZE  5U
#define SEQ_SIZE     8U
#define PAYLOAD_SIZE 12U
#define FRAME_SIZE   (HEADER_SIZE + SEQ_SIZE + PAYLOAD_SIZE)
#define FRAME_SLOTS  (GAP_BITS + FRAME_SIZE * 8U)

/* OOK direct: 1 = HIGH, 0 = LOW, MSB first. */
static const uint8_t header[] = {0x55, 0x55, 0x55, 0x55, 0xD3};
static const uint8_t payload[] = {
    0xFF, 0x16, 0xC1, 0xFF, 0x16, 0xC1,
    0xFF, 0x16, 0xC1, 0xFF, 0x16, 0xC1,
};

#define DATA_PIN DT_GPIO_PIN(DT_PATH(zephyr_user), data_gpios)

static uint16_t waveform[2][FRAME_SLOTS];
static uint16_t next_sequence;
static const nrfx_pwm_t pwm = NRFX_PWM_INSTANCE(0);

/* Refill only the buffer that DMA has finished reading. */
static void fill_waveform(unsigned int buffer)
{
    uint16_t sequence = next_sequence++;
    uint32_t protected_sequence = ((uint32_t)sequence << 16) | (uint16_t)~sequence;
    unsigned int index = 0;
    for (unsigned int i = 0; i < GAP_BITS; i++) {
        waveform[buffer][index++] = 0x8000U | BIT_US;
    }
    for (unsigned int i = 0; i < FRAME_SIZE; i++) {
        uint8_t value;
        if (i < HEADER_SIZE) {
            value = header[i];
        } else if (i < HEADER_SIZE + SEQ_SIZE) {
            /* Each nibble is sent as 101xxxx0: never a false HIGH gap. */
            unsigned int shift = (HEADER_SIZE + SEQ_SIZE - 1U - i) * 4U;
            value = 0xA0U | (((protected_sequence >> shift) & 0x0FU) << 1);
        } else {
            value = payload[i - HEADER_SIZE - SEQ_SIZE];
        }
        for (int bit = 7; bit >= 0; bit--) {
            waveform[buffer][index++] = ((value >> bit) & 1U) ? (0x8000U | BIT_US) : (0x8000U);
        }
    }
}

static void pwm_event(nrfx_pwm_evt_type_t event, void *context)
{
    ARG_UNUSED(context);
    if (event == NRFX_PWM_EVT_END_SEQ0) {
        fill_waveform(0);
    } else if (event == NRFX_PWM_EVT_END_SEQ1) {
        fill_waveform(1);
    }
}

int main(void)
{
    const struct device *clock = DEVICE_DT_GET(DT_NODELABEL(clock));
    int ret;

    if (!device_is_ready(clock)) {
        printk("TX: horloge indisponible\n");
        return -ENODEV;
    }
    ret = clock_control_on(clock, CLOCK_CONTROL_NRF_SUBSYS_HF);
    if (ret < 0) {
        printk("TX: erreur horloge %d\n", ret);
        return ret;
    }
    int64_t deadline = k_uptime_get() + 100;
    while (clock_control_get_status(clock, CLOCK_CONTROL_NRF_SUBSYS_HF) !=  CLOCK_CONTROL_STATUS_ON) {
        if (k_uptime_get() >= deadline) {
            printk("TX: quartz non demarre\n");
            return -ETIMEDOUT;
        }
        k_sleep(K_MSEC(1));
    }

    fill_waveform(0);
    fill_waveform(1);

    nrfx_pwm_config_t cfg = NRFX_PWM_DEFAULT_CONFIG(
        DATA_PIN, NRF_PWM_PIN_NOT_CONNECTED,
        NRF_PWM_PIN_NOT_CONNECTED, NRF_PWM_PIN_NOT_CONNECTED);
    cfg.base_clock = NRF_PWM_CLK_1MHz;
    cfg.top_value = BIT_US;
    cfg.load_mode = NRF_PWM_LOAD_COMMON;
    ret = nrfx_pwm_init(&pwm, &cfg, pwm_event, NULL);
    if (ret != NRFX_SUCCESS) {
        printk("TX: erreur PWM %d\n", ret);
        return -EIO;
    }
    IRQ_CONNECT(DT_IRQN(DT_NODELABEL(pwm0)), 1, nrfx_isr, nrfx_pwm_0_irq_handler, 0);
    irq_enable(DT_IRQN(DT_NODELABEL(pwm0)));
    static const nrf_pwm_sequence_t sequences[] = {
        {.values.p_common = waveform[0], .length = FRAME_SLOTS},
        {.values.p_common = waveform[1], .length = FRAME_SLOTS},
    };
    printk("TX OOK Nordic: P0.04, bit %u us, payload %u octets\n",
           BIT_US, (unsigned int)PAYLOAD_SIZE);
    nrfx_pwm_complex_playback(&pwm, &sequences[0], &sequences[1], 1,
        NRFX_PWM_FLAG_LOOP | NRFX_PWM_FLAG_SIGNAL_END_SEQ0 | NRFX_PWM_FLAG_SIGNAL_END_SEQ1);
    return 0;
}
