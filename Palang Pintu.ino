#include <Servo.h> // servo library
Servo s1;
const int trigPin = 10;
const int echoPin = 9;
long duration;
int distanceCm, distanceInch;
void setup()
{

    Serial.begin(9600);
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);

    s1.attach(3); // Servo Motor
}
void loop()
{
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    duration = pulseIn(echoPin, HIGH);
    distanceCm = duration * 0.034 / 2;
    distanceInch = duration * 0.0133 / 2;
    Serial.println("Distance: ");
    Serial.println(distanceCm);
    delay(50);

    if (distanceCm < 30)
    {

        s1.write(90);
        delay(1000);
    }
    else
    {
        s1.write(0);
        delay(10);
    }
}