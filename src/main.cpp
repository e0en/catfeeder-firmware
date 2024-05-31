#include <stdio.h>

#include "secrets.h"
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

char ssid[] = SECRET_SSID;
char password[] = SECRET_PASSWORD;

const int step_per_rev = 200;
const int motor_a_1_pin = 2;
const int motor_a_2_pin = 3;
const int motor_b_1_pin = 4;
const int motor_b_2_pin = 5;

const gpio_num_t GPIO_PIN = GPIO_NUM_8;
const TickType_t BLINK_PERIOD = 1000;

extern "C" void app_main(void) {
  // todo: add wifi connection
  // https://github.com/espressif/esp-idf/blob/master/examples/wifi/getting_started/station/main/station_example_main.c
  //
  // todo: add http server
  // https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_server
  //
  // todo: add stepper control
  // https://github.com/espressif/esp-idf/tree/master/examples/peripherals/gpio/generic_gpio
  // https://github.com/espressif/esp-idf/tree/master/examples/peripherals/rmt/stepper_motor

  bool is_led_on = true;
  gpio_reset_pin(GPIO_PIN);
  gpio_set_direction(GPIO_PIN, GPIO_MODE_OUTPUT);
  while (1) {
    gpio_set_level(GPIO_PIN, is_led_on);
    vTaskDelay(BLINK_PERIOD / portTICK_PERIOD_MS);
    is_led_on = !is_led_on;
  }
}
