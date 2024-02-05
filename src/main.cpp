#include "Arduino.h"
#include "api/Common.h"
#include "pins_arduino.h"
#include <Stepper.h>

const int ledPin = LED_BUILTIN;

const int step_per_rev = 200;
const int motor_a_1_pin = 2;
const int motor_a_2_pin = 3;
const int motor_b_1_pin = 4;
const int motor_b_2_pin = 5;

const int n_shake = 3;

Stepper stepper(step_per_rev, motor_a_1_pin, motor_a_2_pin, motor_b_1_pin,
                motor_b_2_pin);

void shake();
void dispense();

void setup() {
  pinMode(ledPin, OUTPUT);
  stepper.setSpeed(60);
}

void loop() {
  dispense();
  delay(1000);
}

void dispense() {
  digitalWrite(ledPin, HIGH);
  for (int i = 0; i <= n_shake; i++) {
    shake();
  }

  digitalWrite(ledPin, LOW);
  stepper.step(-step_per_rev);
  delay(500);
}

void shake() {
  stepper.step(+step_per_rev / 6);
  delay(100);
  stepper.step(-step_per_rev / 6);
  delay(100);
}
