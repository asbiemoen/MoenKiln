#include <SPI.h>
#include <Adafruit_MAX31855.h>

#define PIN_CS    8
#define PIN_RELAY 4

Adafruit_MAX31855 thermocouple(PIN_CS);

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);

  Serial.println("Moen Kiln – test");

  if (!thermocouple.begin()) {
    Serial.println("FEIL: MAX31855 ikke funnet – sjekk koblinger");
    while (1);
  }

  Serial.println("MAX31855 OK");
}

void loop() {
  double temp = thermocouple.readCelsius();

  if (isnan(temp)) {
    Serial.println("FEIL: Ingen temperaturavlesning");
  } else {
    Serial.print("Temperatur: ");
    Serial.print(temp, 1);
    Serial.println(" °C");
  }

  // Relay test: klikk av og på
  digitalWrite(PIN_RELAY, HIGH);
  Serial.println("Relay: PÅ");
  delay(1000);

  digitalWrite(PIN_RELAY, LOW);
  Serial.println("Relay: AV");
  delay(1000);
}
