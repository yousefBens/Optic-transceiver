#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_dma.h>
#include <stm32_ll_gpio.h>
#include <stm32_ll_rcc.h>
#include <stm32_ll_tim.h>

#define HALF_BIT_US 3U
#define GAP_HALFS   4U
#define DATA_NODE   DT_PATH(zephyr_user)
#define TIMER_NODE  DT_NODELABEL(t_bit)
#define DATA_MASK   BIT(DT_GPIO_PIN(DATA_NODE, data_gpios))
#define DATA_PORT   ((GPIO_TypeDef *)DT_REG_ADDR(DT_GPIO_CTLR(DATA_NODE, data_gpios)))
#define TX_TIMER    ((TIM_TypeDef *)DT_REG_ADDR(DT_PARENT(TIMER_NODE)))

static const struct gpio_dt_spec data = GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);
static const struct device *timer = DEVICE_DT_GET(TIMER_NODE);
static const uint8_t frame[] = {
    0x55, 0xD3,
    0xFF, 0x16, 0xC1, 0xFF, 0x16, 0xC1,
    0xFF, 0x16, 0xC1, 0xFF, 0x16, 0xC1,
};
static uint32_t waveform[GAP_HALFS + sizeof(frame) * 16U];

int main(void)
{
    BUILD_ASSERT(DT_GPIO_FLAGS(DATA_NODE, data_gpios) == GPIO_ACTIVE_HIGH);
    BUILD_ASSERT(DT_REG_ADDR(DT_PARENT(TIMER_NODE)) == TIM2_BASE);
    if (!gpio_is_ready_dt(&data) || !device_is_ready(timer)) {
        printk("TX: GPIO ou timer indisponible\n");
        return -ENODEV;
    }
    /* The overlay enables MSI auto-calibration against the external LSE crystal. */
    if (!LL_RCC_LSE_IsReady() || !LL_RCC_MSI_IsReady() ||
        (READ_BIT(RCC->CR, RCC_CR_MSIPLLEN) == 0U)) {
        printk("TX: horloge MSI/LSE non prete\n");
        return -EIO;
    }
    int ret = gpio_pin_configure_dt(&data, GPIO_OUTPUT_HIGH);
    if (ret < 0) {
        goto error;
    }
    LL_GPIO_SetPinSpeed(DATA_PORT, DATA_MASK, LL_GPIO_SPEED_FREQ_HIGH);

    unsigned int index = 0;
    for (unsigned int i = 0; i < GAP_HALFS; i++) {
        waveform[index++] = DATA_MASK;
    }
    for (unsigned int i = 0; i < sizeof(frame); i++) {
        for (int bit = 7; bit >= 0; bit--) {
            bool high = (frame[i] >> bit) & 1U;
            /* BSRR: low 16 bits set a pin, high 16 bits reset it. */
            waveform[index++] = high ? DATA_MASK : DATA_MASK << 16;
            waveform[index++] = high ? DATA_MASK << 16 : DATA_MASK;
        }
    }

    uint32_t ticks = counter_us_to_ticks(timer, HALF_BIT_US);
    if (ticks == 0U) {
        return -EINVAL;
    }
    /* STM32 counts 0..ARR inclusive; no timer callback or timer IRQ is needed. */
    const struct counter_top_cfg cfg = {.ticks = ticks - 1U};
    ret = counter_set_top_value(timer, &cfg);
    if (ret < 0) {
        goto error;
    }
    LL_TIM_GenerateEvent_UPDATE(TX_TIMER);
    LL_TIM_ClearFlag_UPDATE(TX_TIMER);

    /* RM0351: TIM2_UP -> DMA1 channel 2, request selection 4. */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_2);
    LL_DMA_ClearFlag_GI2(DMA1);
    LL_DMA_SetPeriphRequest(DMA1, LL_DMA_CHANNEL_2, LL_DMA_REQUEST_4);
    LL_DMA_ConfigTransfer(DMA1, LL_DMA_CHANNEL_2,
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH | LL_DMA_MODE_CIRCULAR |
        LL_DMA_PERIPH_NOINCREMENT | LL_DMA_MEMORY_INCREMENT |
        LL_DMA_PDATAALIGN_WORD | LL_DMA_MDATAALIGN_WORD | LL_DMA_PRIORITY_VERYHIGH);
    LL_DMA_SetMemoryAddress(DMA1, LL_DMA_CHANNEL_2, (uint32_t)waveform);
    LL_DMA_SetPeriphAddress(DMA1, LL_DMA_CHANNEL_2, (uint32_t)&DATA_PORT->BSRR);
    LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_2, ARRAY_SIZE(waveform));

    printk("TX STM32: MSI calibre par quartz LSE, TIM2 + DMA circulaire\n"
           "PA9, demi-bit %u us, payload %u octets, emission continue\n",
           HALF_BIT_US, (unsigned int)sizeof(frame) - 2U);
    LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_2);
    LL_TIM_EnableDMAReq_UPDATE(TX_TIMER);
    ret = counter_start(timer);
    if (ret == 0) {
        return 0;
    }
    LL_TIM_DisableDMAReq_UPDATE(TX_TIMER);
    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_2);
error:
    printk("TX: erreur initialisation %d\n", ret);
    return ret;
}
