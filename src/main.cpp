#include "Arduino.h"
#include "api/Common.h"
#include "pins_arduino.h"
#include "secrets.h"
#include "utility/wl_definitions.h"
#include <Stepper.h>
#include <WiFiNINA.h>

char ssid[] = SECRET_SSID;
char password[] = SECRET_PASSWORD;

const int ledPin = LED_BUILTIN;

const int step_per_rev = 200;
const int motor_a_1_pin = 2;
const int motor_a_2_pin = 3;
const int motor_b_1_pin = 4;
const int motor_b_2_pin = 5;

Stepper stepper(step_per_rev, motor_a_1_pin, motor_a_2_pin, motor_b_1_pin,
                motor_b_2_pin);

int status = WL_IDLE_STATUS;
WiFiServer server(80);

void connect();
void consume_request(WiFiClient &);
void send_response(WiFiClient &);
void dispense(const int);
void fast_blink();

void setup() {
  long started_at = millis();
  pinMode(ledPin, OUTPUT);
  stepper.setSpeed(60);
  connect();
  server.begin();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    WiFi.end();
    connect();
    server.begin();
    delay(1000);
    if (WiFi.status() != WL_CONNECTED) {
      fast_blink();
      return;
    }
  }
  if (server.status() == CLOSED) {
    server.begin();
    delay(1000);
  }
  WiFiClient client = server.available();
  if (client) {
    digitalWrite(ledPin, HIGH);
    consume_request(client);
    send_response(client);
    dispense(4);
    client.stop();
    digitalWrite(ledPin, LOW);
  }
}

void connect() {
  WiFi.begin(ssid, password);
  for (int i = 0; i < 10; i++) {
    digitalWrite(ledPin, HIGH);
    delay(500);
    digitalWrite(ledPin, LOW);
    delay(500);
    if (WiFi.status() == WL_CONNECTED) {
      break;
    }
  }
}

void consume_request(WiFiClient &client) {
  bool is_blank_line = true;
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n' && is_blank_line) {
        break;
      }
      if (c == '\n') {
        is_blank_line = true;
      } else if (c != '\r') {
        is_blank_line = false;
      }
    }
  }
}

void send_response(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Length: 0");
  client.println("");
}

void dispense(const int rotation) {
  digitalWrite(ledPin, HIGH);
  stepper.step(step_per_rev * rotation);
  digitalWrite(ledPin, LOW);
}

void fast_blink() {
  for (int i = 0; i < 10; i++) {
    digitalWrite(ledPin, HIGH);
    delay(100);
    digitalWrite(ledPin, LOW);
    delay(100);
  }
}
