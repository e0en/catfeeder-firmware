#include "Arduino.h"
#include "api/Common.h"
#include "pins_arduino.h"
#include <Stepper.h>

const int ledPin = 13;
const int stepsPerRevolution = 200;
Stepper motor(stepsPerRevolution, 8, 9, 10, 11);

void setup() { pinMode(ledPin, OUTPUT); }

void loop() {
  motor.step(stepsPerRevolution);
  digitalWrite(ledPin, HIGH);
  delay(500);
  digitalWrite(ledPin, LOW);
  delay(500);
}
