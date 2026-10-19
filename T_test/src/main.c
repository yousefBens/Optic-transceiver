#include <zephyr/kernel.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <nrfx_pwm.h>

#define HALF_BIT_US 3U
#define GAP_HALFS   4U
#define DATA_PIN    DT_GPIO_PIN(DT_PATH(zephyr_user), data_gpios)

static const uint8_t frame[] = {0x55, 0xD3, 0xFF, 0x16, 0xC1, 0xFF, 0x16, 0xC1, 0xFF, 0x16, 0xC1, 0xFF, 0x16, 0xC1};
static uint16_t waveform[GAP_HALFS + sizeof(frame) * 16U];
static const nrfx_pwm_t pwm = NRFX_PWM_INSTANCE(0);

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
    while (clock_control_get_status(clock, CLOCK_CONTROL_NRF_SUBSYS_HF) !=
           CLOCK_CONTROL_STATUS_ON) {
        if (k_uptime_get() >= deadline) {
            printk("TX: quartz non demarre\n");
            return -ETIMEDOUT;
        }
        k_sleep(K_MSEC(1));
    }

    unsigned int index = 0;
    /* Bit 15 selects rising polarity: compare=0 is LOW, compare=TOP is HIGH. */
    for (unsigned int i = 0; i < GAP_HALFS; i++) {
        waveform[index++] = 0x8000U | HALF_BIT_US;
    }
    for (unsigned int i = 0; i < sizeof(frame); i++) {
        for (int bit = 7; bit >= 0; bit--) {
            bool high = (frame[i] >> bit) & 1U;
            waveform[index++] = 0x8000U | (high ? HALF_BIT_US : 0U);
            waveform[index++] = 0x8000U | (high ? 0U : HALF_BIT_US);
        }
    }

    nrfx_pwm_config_t cfg = NRFX_PWM_DEFAULT_CONFIG(
        DATA_PIN, NRF_PWM_PIN_NOT_CONNECTED,
        NRF_PWM_PIN_NOT_CONNECTED, NRF_PWM_PIN_NOT_CONNECTED);
    cfg.base_clock = NRF_PWM_CLK_1MHz;
    cfg.top_value = HALF_BIT_US;
    cfg.load_mode = NRF_PWM_LOAD_COMMON;
    ret = nrfx_pwm_init(&pwm, &cfg, NULL, NULL);
    if (ret != NRFX_SUCCESS) {
        printk("TX: erreur PWM %d\n", ret);
        return -EIO;
    }
    static const nrf_pwm_sequence_t sequence = {
        .values.p_common = waveform,
        .length = ARRAY_SIZE(waveform),
    };
    nrfx_pwm_simple_playback(&pwm, &sequence, 1, NRFX_PWM_FLAG_LOOP);
    return 0;
}
