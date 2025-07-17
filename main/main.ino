/*
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Complete project details at https://RandomNerdTutorials.com/esp-now-two-way-communication-esp32/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/
#include <Mesh.h>

//to do with motion sensor don't know what yet
#define timeSeconds 3

// set up led and motion sensor and button pins
const int redLed = 25;
const int greenLed = 26;

const int button = 33;

const int motionSensor = 27;

// Timer: Auxiliary variables
// had to do with motion sensor as well
unsigned long now = millis();
unsigned long lastTrigger = 0;
boolean startTimer = false;
boolean motion = false;

Mesh mesh;
mesh_message transmissionMessage;


// Variable to store if sending data was successful
String success;

// also motion sensor
// Checks if motion was detected, sets LED HIGH and starts a timer
void IRAM_ATTR detectsMovement() {
  digitalWrite(greenLed, HIGH);
  startTimer = true;
  lastTrigger = millis();
}

void dataRecvCallback(mesh_message message) {
  digitalWrite(greenLed, HIGH);
  delay(500);
  digitalWrite(greenLed, LOW);
}

void setup() {
  // Init Serial Monitor
  Serial.begin(115200);

  // Set LED to LOW
  pinMode(redLed, OUTPUT);
  digitalWrite(redLed, LOW);

  // Set LED to LOW
  pinMode(greenLed, OUTPUT);
  digitalWrite(greenLed, LOW);

  // PIR Motion Sensor mode INPUT_PULLUP
  pinMode(motionSensor, INPUT_PULLUP);
  
  mesh.init();
  mesh.linkDataRecvCallback(dataRecvCallback);

  // Set motionSensor pin as interrupt, assign interrupt function and set RISING mode
  attachInterrupt(digitalPinToInterrupt(motionSensor), detectsMovement, RISING);
}

void loop() {
  now = millis();
  if ((digitalRead(greenLed) == HIGH) && (motion == false)) {
    Serial.println("MOTION DETECTED!!!");
    transmissionMessage.dataType = PIR_SENSOR_MESSAGE_TYPE;
    mesh.transmit(transmissionMessage);
    motion = true;
  }

  // Turn off the LED after the number of seconds defined in the timeSeconds variable
  if (startTimer && (now - lastTrigger > (timeSeconds * 1000))) {
    Serial.println("Motion stopped...");
    digitalWrite(greenLed, LOW);
    startTimer = false;
    motion = false;
  }
}
