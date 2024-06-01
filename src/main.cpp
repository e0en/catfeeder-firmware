#include <stdio.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "secrets.h"

const int step_per_rev = 200;
const int motor_a_1_pin = 2;
const int motor_a_2_pin = 3;
const int motor_b_1_pin = 4;
const int motor_b_2_pin = 5;

const gpio_num_t GPIO_PIN = GPIO_NUM_8;
const TickType_t BLINK_PERIOD = 1000;

static int wifi_retry_count = 0;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

void blink_once(TickType_t duration_ms);
void setup_wifi();
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data);

extern "C" void app_main(void) {
  // todo: add http server
  // https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_server
  //
  // todo: add stepper control
  // https://github.com/espressif/esp-idf/tree/master/examples/peripherals/gpio/generic_gpio
  // https://github.com/espressif/esp-idf/tree/master/examples/peripherals/rmt/stepper_motor
  gpio_reset_pin(GPIO_PIN);
  gpio_set_direction(GPIO_PIN, GPIO_MODE_OUTPUT);

  // initialize non-volatile storage encryped r/w
  ESP_ERROR_CHECK(nvs_flash_init());
  setup_wifi();

  while (true) {
    blink_once(1000);
  }
}

void blink_once(TickType_t duration_ms) {
  gpio_set_level(GPIO_PIN, true);
  vTaskDelay(duration_ms / portTICK_PERIOD_MS);
  gpio_set_level(GPIO_PIN, false);
  vTaskDelay(duration_ms / portTICK_PERIOD_MS);
}

void setup_wifi(void) {
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init()); // init underlying tcp/ip stack
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta(); // setup wifi station object

  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&config));

  esp_event_handler_instance_t instance_wifi;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_wifi));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = SECRET_SSID,
              .password = SECRET_PASSWORD,
              .threshold =
                  {
                      .authmode = WIFI_AUTH_OPEN,
                  },
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  ESP_ERROR_CHECK(esp_wifi_start());

  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);
  if (bits & WIFI_CONNECTED_BIT) {
    for (int i = 0; i < 10; i++) {
      blink_once(100);
    }
  }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT) {
    if (event_id == WIFI_EVENT_STA_START) {
      esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
      if (wifi_retry_count < 5) {
        esp_wifi_connect();
        wifi_retry_count++;
      } else {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
      }
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    wifi_retry_count = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}
