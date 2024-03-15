#include "Arduino.h"
#include "api/Common.h"
#include "pins_arduino.h"
#include "secrets.h"
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

void dispense(const int);

void setup() {
  Serial.begin(9600);
  pinMode(ledPin, OUTPUT);
  stepper.setSpeed(60);

  while (status != WL_CONNECTED) {
    status = WiFi.begin(ssid, password);
    for (int i = 0; i < 10; i++) {
      digitalWrite(ledPin, HIGH);
      delay(500);
      digitalWrite(ledPin, LOW);
      delay(500);
    }
  }
  IPAddress ip = WiFi.localIP();
  if (Serial) {
    Serial.print("IP Address: ");
    Serial.println(ip);
  }
  server.begin();
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    bool is_blank_line = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();

        if (c == '\n' && is_blank_line) {
          dispense(4);
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Length: 0");
          client.println("");
        }
        if (c == '\n') {
          is_blank_line = true;
        } else if (c != '\r') {
          is_blank_line = false;
        }
      }
    }
    client.stop();
  }
}

void dispense(const int rotation) {
  digitalWrite(ledPin, HIGH);
  stepper.step(step_per_rev * rotation);
  digitalWrite(ledPin, LOW);
}
