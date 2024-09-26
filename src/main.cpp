#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "nvs_flash.h"
#include "portmacro.h"
#include "secrets.h"
#include <esp_http_server.h>

void my_sleep(TickType_t milliseconds);

const gpio_num_t LED_PIN = GPIO_NUM_8;
const gpio_num_t MOTOR_ON_PIN = GPIO_NUM_0;
const gpio_num_t DIR_PIN = GPIO_NUM_20;
const gpio_num_t STEP_PIN = GPIO_NUM_21;
const int MICROSTEP = 1;

void set_step() {
  gpio_set_level(STEP_PIN, 0);
  vTaskDelay(1 / portTICK_PERIOD_MS);
  gpio_set_level(STEP_PIN, 1);
  vTaskDelay(1 / portTICK_PERIOD_MS);
}

void do_full_step() {
  for (int i = 0; i < MICROSTEP; i++) {
    set_step();
  }
}

void dispense() {
  gpio_set_level(MOTOR_ON_PIN, 1);
  my_sleep(10);
  for (int i = 0; i < 5; i++) {
    gpio_set_level(DIR_PIN, 0);
    do_full_step();
    do_full_step();
    gpio_set_level(DIR_PIN, 1);
    my_sleep(50);
    do_full_step();
  }
  my_sleep(10);
  gpio_set_level(MOTOR_ON_PIN, 0);
}

/* WiFi-related variables */
static int wifi_retry_count = 0;

/* FreeRTOS event group for wifi events */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

void blink_once(TickType_t duration_ms);
void setup_wifi();
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

/* webserver-related variables */
static httpd_handle_t start_webserver();
static esp_err_t stop_webserver(httpd_handle_t server);
static esp_err_t root_uri_handler(httpd_req_t *request);

static void connect_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data);
static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_uri_handler,
    .user_ctx = NULL,
};

extern "C" void app_main(void) {
  // todo: add stepper control
  // https://github.com/espressif/esp-idf/tree/master/examples/peripherals/gpio/generic_gpio
  // https://github.com/espressif/esp-idf/tree/master/examples/peripherals/rmt/stepper_motor
  static httpd_handle_t server = NULL;
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init()); // init underlying tcp/ip stack
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  gpio_reset_pin(LED_PIN);
  gpio_reset_pin(MOTOR_ON_PIN);
  gpio_reset_pin(DIR_PIN);
  gpio_reset_pin(STEP_PIN);

  gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(MOTOR_ON_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(DIR_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(STEP_PIN, GPIO_MODE_OUTPUT);

  gpio_set_level(LED_PIN, 0);
  gpio_set_level(MOTOR_ON_PIN, 0);
  gpio_set_level(DIR_PIN, 0);
  gpio_set_level(STEP_PIN, 0);

  setup_wifi();

  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &connect_handler, &server));
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

  server = start_webserver();
  ESP_LOGI("server", "started");

  while (server) {
    ESP_LOGI("server", "alive");
    blink_once(100);
    my_sleep(2000);
  }
}

void blink_once(TickType_t duration_ms) {
  gpio_set_level(LED_PIN, false);
  my_sleep(duration_ms);
  gpio_set_level(LED_PIN, true);
  my_sleep(duration_ms);
}

void my_sleep(TickType_t milliseconds) {
  vTaskDelay(milliseconds / portTICK_PERIOD_MS);
}

void setup_wifi(void) {
  EventGroupHandle_t wifi_event_group = xEventGroupCreate();

  esp_netif_create_default_wifi_sta(); // setup wifi station object

  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&config));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));
  xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);

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

  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                      pdFALSE, pdFALSE, portMAX_DELAY);

  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &instance_got_ip));
  vEventGroupDelete(wifi_event_group);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
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

static httpd_handle_t start_webserver() {
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(server, &root);
    return server;
  }
  return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server) {
  return httpd_stop(server);
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  httpd_handle_t *server = (httpd_handle_t *)arg;
  if (*server) {
    if (stop_webserver(*server) == ESP_OK) {
      *server = NULL;
    } else {
      ;
    }
  }
}

static void connect_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data) {
  httpd_handle_t *server = (httpd_handle_t *)arg;
  if (*server == NULL) {
    *server = start_webserver();
  }
}

static esp_err_t root_uri_handler(httpd_req_t *request) {
  dispense();
  httpd_resp_send(request, "", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}
