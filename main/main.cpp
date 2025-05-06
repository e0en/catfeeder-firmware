#include "secrets.h"

extern "C" {
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif_types.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include <esp_wifi_types.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <hal/gpio_types.h>
#include <nvs_flash.h>
#include <portmacro.h>
}

void my_sleep(TickType_t milliseconds);

;
const gpio_num_t LED_PIN = GPIO_NUM_8;
const gpio_num_t MOTOR_ON_PIN = GPIO_NUM_0;
const gpio_num_t DIR_PIN = GPIO_NUM_20;
const gpio_num_t STEP_PIN = GPIO_NUM_21;
const gpio_num_t BUTTON_PIN = GPIO_NUM_3;
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

void vibrate() {
  gpio_set_level(MOTOR_ON_PIN, 1);
  for (int i = 0; i < 10; i++) {
    gpio_set_level(DIR_PIN, 0);
    do_full_step();
    do_full_step();
    gpio_set_level(DIR_PIN, 1);
    do_full_step();
    do_full_step();
  }
  gpio_set_level(MOTOR_ON_PIN, 0);
}

void dispense() {
  my_sleep(10);
  vibrate();
  my_sleep(100);
  gpio_set_level(MOTOR_ON_PIN, 1);
  gpio_set_level(DIR_PIN, 0);
  for (int i = 0; i < 20; i++) {
    do_full_step();
    my_sleep(10);
  }
  my_sleep(100);
  gpio_set_level(MOTOR_ON_PIN, 0);
}

/* WiFi-related variables */
static int s_wifi_retry_count = 0;

/* FreeRTOS event group for wifi events */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

void blink_once(TickType_t duration_ms);
void log_manually();
esp_err_t setup_wifi();
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
  static httpd_handle_t server = NULL;
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init()); // init underlying tcp/ip stack
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  gpio_config_t button_conf = {
      .pin_bit_mask = (1ULL << BUTTON_PIN),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_ANYEDGE,
  };
  gpio_config(&button_conf);

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

  ESP_ERROR_CHECK(setup_wifi());

  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &connect_handler, &server));
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

  server = start_webserver();
  ESP_LOGI("server", "started");

  TickType_t last_blinked_at = 0;
  bool last_button_state = false;

  while (server) {
    ESP_LOGI("server", "alive"); // todo: replace with MQTT publish
    bool button_state = (gpio_get_level(BUTTON_PIN) != 0);
    ESP_LOGI("button", "%d -> %d", last_button_state, button_state);
    if (!last_button_state && button_state) {
      ESP_LOGI("server", "button pressed");
      dispense();
      log_manually();
    }
    last_button_state = button_state;

    if (xTaskGetTickCount() - last_blinked_at > (2000 / portTICK_PERIOD_MS)) {
      last_blinked_at = xTaskGetTickCount();
      blink_once(100);
    }
    my_sleep(100);
  }
}

void log_manually() {
  esp_http_client_config_t config = {
      .url = LOG_URI,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_err_t err = esp_http_client_perform(client);

  if (err == ESP_OK) {
    ESP_LOGI("HTTP", "Request sent successfully");
  } else {
    ESP_LOGE("HTTP", "Request failed: %s", esp_err_to_name(err));
  }
  esp_http_client_cleanup(client);
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

esp_err_t setup_wifi(void) {
  const char *TAG = "WIFI";

  esp_netif_create_default_wifi_sta(); // setup wifi station object

  s_wifi_event_group = xEventGroupCreate();
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

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASSWORD,
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
    ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", WIFI_SSID,
             WIFI_PASSWORD);
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGE(TAG, "Connection failed");
    return ESP_ERR_TIMEOUT;
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }

  esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &instance_any_id);
  esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &instance_got_ip);
  vEventGroupDelete(s_wifi_event_group);
  return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  const char *TAG = "WIFI_EVENT";
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "station started");
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_wifi_retry_count < 5) {
      ESP_LOGW(TAG, "disconnected: %d retries", s_wifi_retry_count);
      esp_wifi_connect();
      s_wifi_retry_count++;
    } else {
      ESP_LOGE(TAG, "failed after %d retries", s_wifi_retry_count);
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "connected, got IP address = " IPSTR,
             IP2STR(&event->ip_info.ip));
    s_wifi_retry_count = 0;
    ESP_LOGI(TAG, "connected!");
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
