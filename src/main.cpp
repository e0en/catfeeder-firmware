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

Stepper stepper(step_per_rev, motor_a_1_pin, motor_a_2_pin, motor_b_1_pin,
                motor_b_2_pin);

void setup() {
  pinMode(ledPin, OUTPUT);
  stepper.setSpeed(60);
}

void loop() {
  digitalWrite(ledPin, HIGH);
  stepper.step(-step_per_rev);
  delay(500);

  digitalWrite(ledPin, LOW);
  stepper.step(-step_per_rev);
  delay(500);
}
