#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_dma.h>
#include <stm32_ll_gpio.h>
#include <stm32_ll_rcc.h>
#include <stm32_ll_tim.h>
#include <zephyr/irq.h>

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

#define DATA_NODE  DT_PATH(zephyr_user)
#define TIMER_NODE DT_NODELABEL(t_bit)
#define DATA_MASK  BIT(DT_GPIO_PIN(DATA_NODE, data_gpios))
#define DATA_PORT  ((GPIO_TypeDef *)DT_REG_ADDR(DT_GPIO_CTLR(DATA_NODE, data_gpios)))
#define TX_TIMER   ((TIM_TypeDef *)DT_REG_ADDR(DT_PARENT(TIMER_NODE)))

static const struct gpio_dt_spec data = GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);
static const struct device *timer = DEVICE_DT_GET(TIMER_NODE);
static uint32_t waveform[2][FRAME_SLOTS];
static uint16_t next_sequence;

/* Refill only the buffer that DMA has finished reading. */
static void fill_waveform(unsigned int buffer)
{
    uint16_t sequence = next_sequence++;
    uint32_t protected_sequence = ((uint32_t)sequence << 16) | (uint16_t)~sequence;
    unsigned int index = 0;
    for (unsigned int i = 0; i < GAP_BITS; i++) {
        waveform[buffer][index++] = DATA_MASK;
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
            waveform[buffer][index++] = ((value >> bit) & 1U) ? (DATA_MASK) : (DATA_MASK << 16);
        }
    }
}

static void dma_irq(const void *unused)
{
    ARG_UNUSED(unused);
    if (LL_DMA_IsActiveFlag_HT2(DMA1)) {
        LL_DMA_ClearFlag_HT2(DMA1);
        fill_waveform(0);
    }
    if (LL_DMA_IsActiveFlag_TC2(DMA1)) {
        LL_DMA_ClearFlag_TC2(DMA1);
        fill_waveform(1);
    }
}

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

    fill_waveform(0);
    fill_waveform(1);

    uint32_t ticks = counter_us_to_ticks(timer, BIT_US);
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
    LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_2, 2U * FRAME_SLOTS);

    printk("TX OOK STM32: MSI calibre par quartz LSE, TIM2 + DMA circulaire\n"
           "PA9, bit %u us, payload %u octets, emission continue\n",
           BIT_US, (unsigned int)PAYLOAD_SIZE);
    IRQ_CONNECT(DMA1_Channel2_IRQn, 1, dma_irq, NULL, 0);
    LL_DMA_EnableIT_HT(DMA1, LL_DMA_CHANNEL_2);
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_2);
    irq_enable(DMA1_Channel2_IRQn);
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
