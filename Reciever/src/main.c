#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>


#define DATA_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec data_arrive =
    GPIO_DT_SPEC_GET(DATA_NODE, data_gpios);

static struct gpio_callback data_cb;



#define TIMER_NODE DT_NODELABEL(t_bit)

static const struct device *timer =
    DEVICE_DT_GET(TIMER_NODE);


static uint32_t previous_ticks = 0;


static bool first_edge = true;



static void data_callback(const struct device *dev,
                          struct gpio_callback *cb,
                          uint32_t pins)
{
    uint32_t current_ticks;
    uint32_t delta_ticks;
    uint64_t delta_us;
    int value;

    value = gpio_pin_get_dt(&data_arrive);


    counter_get_value(timer, &current_ticks);


    if (first_edge) {

        previous_ticks = current_ticks;
        first_edge = false;

        printk("First edge | GPIO = %d\n", value);

        return;
    }

    delta_ticks = current_ticks - previous_ticks;

    previous_ticks = current_ticks;

    delta_us = counter_ticks_to_us(timer, delta_ticks);

    printk("GPIO = %d | dt = %llu us\n",
           value,
           (unsigned long long)delta_us);
}



int main(void)
{
    int ret;

    printk("\n");
    printk("------ Receiver Start ------\n");



    if (!gpio_is_ready_dt(&data_arrive)) {

        printk("GPIO non disponible\n");
        return -1;
    }

    ret = gpio_pin_configure_dt(
        &data_arrive,
        GPIO_INPUT | GPIO_PULL_DOWN
    );

    if (ret < 0) {

        printk("Erreur configuration GPIO : %d\n", ret);
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

        printk("Erreur ajout callback GPIO : %d\n", ret);
        return -1;
    }





    if (!device_is_ready(timer)) {

        printk("Timer non disponible\n");
        return -1;
    }

    printk("Timer frequency = %u Hz\n",
           counter_get_frequency(timer));


    ret = counter_start(timer);

    if (ret < 0) {

        printk("Erreur demarrage timer : %d\n", ret);
        return -1;
    }







    ret = gpio_pin_interrupt_configure_dt(
        &data_arrive,
        GPIO_INT_EDGE_BOTH
    );

    if (ret < 0) {

        printk("Erreur configuration interruption GPIO : %d\n",
               ret);

        return -1;
    }


    printk("Receiver ready\n");


    


    k_sleep(K_FOREVER);

    return 0;
}