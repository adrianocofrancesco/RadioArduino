# RadioArduino

RadioArduino is a project that aims to emulate an FM Radio. Specially that radio can be controlled manually, using a potentiometer to change frequency, or with special messages through WiFi using MQTT protocol. Based on the scheme publish/subscribe with MQTT RadioArduino can operate as Subscriber, in this mode it can only receive messages and answer to the Publisher automatically when it receives specific requests, or operate as Publisher, in this mode it has all the features of Subscriber, furthermore it can change the frequency to all subscribers, check the frequency on which they are tuned and require their ID. Every Subscriber will belong to a group, initially by default, which can be change indipendently so as to receive messages from a different Publisher.

## Hardware

- ESP8266 NodeMCU
- LCD 16x2
- 2x potentiometers 10K
- TEA5767 FM Radio module

## Links:

- TEA5767 - https://www.instructables.com/id/Make-an-Arduino-FM-Radio-using-TEA5767/
- MQTT - http://mqtt.org/
- PubSubClient (MQTT) - https://pubsubclient.knolleary.net/
- ESP8266WiFi - https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi
