/**
    Copyright 2018  Adriano Cofrancesco

    RadioArduinoClient is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    RadioArduinoClient is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
//include libraries
#include <map>
#include <Wire.h>
#include <TEA5767Radio.h>
#include <LiquidCrystal.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

//define constants
#define WIFI_ATTEMPTS 100
#define MQTT_ATTEMPTS 10
#define DELAY_MQTT 1000
#define MSG_LEN 150
#define TOPIC "RadioArduino/"

WiFiClient espClient;

PubSubClient mqtt_client(espClient);

//WiFi credentials
byte mac[6];
String ssid = "***";
String password = "***";

//MQTT server name
String mqtt_server = "***"; //Name or IP of Server
String nodeName;

//MQTT message max length
char msg[MSG_LEN];

//setup LCD pins
LiquidCrystal lcd(0, 2, 14, 12, 13, 15);

typedef struct station {
    String stationName;
    float freq;
} Station;

Station stations[9];

TEA5767Radio radio = TEA5767Radio();

//value read from Analog Pin
int val = 0;
//Analog pin number
int analogPin = 0;
//interger frequency value
int frequencyInt = 0;
//float frequency value
float frequency = 0.0;
float previousFrequency = 0.0;
float defaultPotentiometerFrequency = 0.0;
//first check of potentiometer value
bool firstFreqCheck = true;
//flag used to stay unsubscripted from topic
bool mqttStayUnsub = false;

//default group name
String group = "None";
String clientId = "ESP8266Client-";

//last MQTT reconnect time 
long lastReconnectAttempt = 0;
//MQTT attempts counter
int mqttAttempts = 0;

float defaultStationFrequency;

//MQTT command map used in switch
std::map<String, int> commandMap;

void setup()
{
    //used by TEA5767Radio library
    Wire.begin();
    Serial.begin(115200);

    lcd.begin(16,2);

    //setup radio stations
    initStations(stations);

    //setup MQTT command
    initCommandMap();

    //set default radio station
    setDefaultFrequency(stations[3].freq);

    Serial.println("Booting...");

    //WiFi connection attempt
    if (wifi_setup() == true) {

        WiFi.macAddress(mac);
        nodeName=TOPIC;

        //setting clientId with MAC address
        for (int i=0; i<6; i++)
            clientId += String(mac[i],HEX);

        //MQTT Broker connection attempt
        mqtt_client.setServer(mqtt_server.c_str(), 1883);

        //set MQTT client callback
        mqtt_client.setCallback(callback);
      
        Serial.println("Booted!");
    }
}

void loop()
{
    //reading frequency value from Analog Pin
    for(int i=0;i<20;i++) {
       val = val + analogRead(analogPin); 
       delay(1);
    }

    //map value read from potentiometer from 87 MHz to 107 MHz
    val = val/20;
    frequencyInt = map(val, 2, 1014, 8700, 10700);
    frequency = frequencyInt/100.0f;

    //setup default potentiometer frequency value to current frequency if is the first time that controls it
    if (firstFreqCheck == true && defaultPotentiometerFrequency > 0.0) {
        firstFreqCheck = false;
        defaultPotentiometerFrequency = frequency;
    }

    //check if frequency is controlled by MQTT messages or potentiometer values
    if (defaultPotentiometerFrequency == 0.0 || defaultFrequencyCheck(frequency)) {

        //if frequency is controlled by potentiometer set it up to 0.0
        defaultPotentiometerFrequency = 0.0;

        //setup frequency read from potentiometer
        if(frequency - previousFrequency >= 0.1f || previousFrequency - frequency >= 0.1f) {
            
            //set radio frequency
            radio.setFrequency(frequency);
            previousFrequency = frequency;
        }
  
        checkStationFrequency(frequency);
    }

    //check for serial input
    String fromSerial;
    while(Serial.available()){
        fromSerial = Serial.readString();
        Serial.println("Serial Input: " + fromSerial);
        checkSerialInput(fromSerial);
    }

    //check for connection to MQTT Broker
    if (!mqtt_client.connected() && mqttStayUnsub == false && mqttAttempts < MQTT_ATTEMPTS) {
        long now = millis();
        //check if reconnection attempt time is about 5 seconds
        if (now - lastReconnectAttempt > 5000) {
            lastReconnectAttempt = now;
            mqtt_reconnect();
        }
    }
    mqtt_client.loop();
}

/**
 * Function used to controls inputs from Serial monitor
 */
void checkSerialInput(String serialIn)
{
    float newFreq = 0.0;
    String commandArg;
    String prevGroup;
    int firstSlashOccurrence = serialIn.indexOf('/');

    //check if the command is in the correct format
    if (firstSlashOccurrence > 0) {

        //get the command from input
        String command = serialIn.substring(0, firstSlashOccurrence);
        command.trim();

        //check the command type
        switch(commandMap[command])
        {
            //group/newGroupName - change the current group to newGroupName
            case 1:
                    //get the name of the new group
                    commandArg = serialIn.substring(firstSlashOccurrence+1);
                    commandArg.trim();
                    
                    if (commandArg != "") {

                        prevGroup = group;
                        group = commandArg;

                        if (!mqtt_client.connected())
                            mqtt_reconnect();
                        
                        if (mqtt_client.connected()) {

                            //unsubscribe from the previous topic
                            String prevTopic = nodeName + prevGroup;
                            mqtt_client.unsubscribe(prevTopic.c_str());
                            
                            String firstMsg = "Client " + clientId + " connected!";
                            String currentTopic = nodeName + group;

                            //publish message to new topic
                            mqtt_client.publish(currentTopic.c_str(), firstMsg.c_str());
                            //subscribe to new topic
                            mqtt_client.subscribe(currentTopic.c_str());

                            Serial.println("Group switched to: " + group);
                        
                        } else { Serial.println("Error: MQTT client not connected"); }
                    }
                break;
            //groupInfo/ - print the current group
            case 3:
                    Serial.println("Current Group: " + group);
                break;
            //frequencyInfo/ - print the current frequency
            case 4:
                    commandArg = String(frequency);
                    //if the potentiometer doesn't change the frequency, print the default frequency
                    if (previousFrequency == 0.0)
                        commandArg = String(defaultStationFrequency);

                    Serial.println("Current Frequency: " + String(commandArg));
                break;
            //frequencyLocal/frequencyValue - change local frequency to frequencyValue
            case 9:
                    //get the frequency value
                    commandArg = serialIn.substring(firstSlashOccurrence+1);
                    commandArg.trim();

                    if (commandArg != "") {
                        newFreq = commandArg.toFloat();
                        //chek if the new frequency is in the range from 87MHz to 107MHz
                        if (newFreq >= 87.0 && newFreq <= 107.0) {
                            setDefaultFrequency(newFreq);
                            Serial.println("Local Frequency updated: " + String(newFreq));
                        }
                    }
                break;
            //stayUnsubscribed/ - stay unsubscribed from MQTT topic
            case 10:
                    commandArg = nodeName + group;
                    if (mqtt_client.unsubscribe(commandArg.c_str())) {

                        mqttStayUnsub = true;
                        Serial.println("MQTT unsubscription completed");
                    } else
                        Serial.println("MQTT unsubscription failed! Check connection");
                break;
            //enableSubscription/ - enable MQTT topic subscription
            case 11:
                    mqtt_client.disconnect();
                    mqtt_reconnect();

                    if (mqtt_client.connected()) {
                    
                        mqttStayUnsub = false;
                        Serial.println("MQTT subscription enabled");
                        
                    } else { Serial.println("Error: MQTT client not connected"); }
                break;
            //mqttProbe/ - retry MQTT Broker connection manually
            case 12:
                    if (!mqtt_client.connected())
                        mqtt_reconnect();
                    else
                        Serial.println("MQTT client already connected");
                break;
            //default case if the command is not recognised
            default:
                Serial.println("Unknown Serial command");
                break;
        }

    }
}

/**
 * MQTT client callback function, used to receive messages
 */
void callback(char* topic, byte* payload, unsigned int length)
{
    String inMsg = "";
  
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    Serial.println();
    for (int i = 0; i < length; i++) {
        inMsg += (char)payload[i];
    }
    Serial.println(inMsg);

    if (mqttStayUnsub == false)
        checkMqttInput(inMsg);
    else
        Serial.println("Enable subscription to execute MQTT command");
}

/**
 * Function used to check the command received from MQTT
 */
void checkMqttInput(String mqttIn)
{
    //frequency value used in MQTT messages
    float mqttFreq = 0.0;
    String commandArg;
    int firstSlashOccurrence = mqttIn.indexOf('/');

    //check if the MQTT command is in the correct format
    if (firstSlashOccurrence > 0) {

        //get the command from input
        String command = mqttIn.substring(0, firstSlashOccurrence);
        command.trim();

        //check the command type
        switch(commandMap[command])
        {
            //frequency/frequencyValue - change the frequency in according to topic publisher
            case 2:
                    //get frequency value
                    commandArg = mqttIn.substring(firstSlashOccurrence+1);
                    commandArg.trim();
                    if (commandArg != "") {
                      
                        mqttFreq = commandArg.toFloat();
                        //chek if the new frequency is in the range from 87MHz to 107MHz
                        if (mqttFreq >= 87.0 && mqttFreq <= 107.0) {
                            
                            //setup the default frequency value variables with the new frequency to avoid that potentiometer current value change the frequency
                            setDefaultFrequency(mqttFreq);

                            Serial.println("Frequency changed to: " + String(mqttFreq));
                        }
                    }
                break;
            //frequencyFollowers/ - receive MQTT message from the topic publisher and send it back the current frequency
            case 5:
                    commandArg = String(frequency);
                    if (previousFrequency == 0.0)
                          //if the potentiometer doesn't change the frequency, send the default frequency
                          commandArg = String(defaultStationFrequency);

                    if (mqtt_send("frequencyAck/" + commandArg))
                        Serial.println("Frequency ACK sended");
                break;
            //followersId/ - send MQTT message to topic publisher with the clientId
            case 7:
                    if (mqtt_send("followerIdAck/" + clientId))
                        Serial.println("Follower ACK sended");
                break;
            //default case if the command is not recognised
            default:
                Serial.println("Unknown MQTT command");
                break;
        }

    }
}

/**
 * Function used to check if the potentiometer has changed the frequency
 */
boolean defaultFrequencyCheck(float freq)
{
    if (defaultPotentiometerFrequency >= freq) {

        if (defaultPotentiometerFrequency - freq > 1.0f)
            return true;
        return false;
    
    } else {

        if (freq - defaultPotentiometerFrequency > 1.0f)
            return true;
        return false;
    }

    return false;
}

/**
 * Initialize the radio stations and their frequency
 */
void initStations(Station s[])
{
  s[0].stationName = "RADIO CAPITAL";
  s[0].freq = 90.1;
  s[1].stationName = "RADIO M2O";
  s[1].freq = 93.1;
  s[2].stationName = "RADIO MARCONI";
  s[2].freq = 94.8;
  s[3].stationName = "DISCO RADIO";
  s[3].freq = 96.5;
  s[4].stationName = "RADIO kISS KISS";
  s[4].freq = 97.8;
  s[5].stationName = "RADIO DEEJAY";
  s[5].freq = 99.7;
  s[6].stationName = "R 101";
  s[6].freq = 100.9;
  s[7].stationName = "RTL";
  s[7].freq = 102.5;
  s[8].stationName = "VIRGIN RADIO";
  s[8].freq = 104.5;
}

/**
 * Setup command map used in switch
 */
void initCommandMap()
{
    commandMap["group"] = 1;
    commandMap["frequency"] = 2;
    commandMap["groupInfo"] = 3;
    commandMap["frequencyInfo"] = 4;
    commandMap["frequencyFollowers"] = 5;
    commandMap["frequencyAck"] = 6;
    commandMap["followersId"] = 7;
    commandMap["followerIdAck"] = 8;
    commandMap["frequencyLocal"] = 9;
    commandMap["stayUnsubscribed"] = 10;
    commandMap["enableSubscription"] = 11;
    commandMap["mqttProbe"] = 12;
}

/**
 * Function used to print on display the radio frequency and the station name if known
 */
void checkStationFrequency(float frequency)
{
    lcd.begin(16,2);
    bool found = false;
    
    for (int i=0; i<(sizeof(stations)); i++) {

        //check if frequency belongs to a known station
        if (frequency >= (stations[i].freq - 0.1f) && frequency <= (stations[i].freq + 0.1f) && found == false){

            //print station name on LCD display
            lcd.setCursor(0, 0);
            lcd.print("*" + stations[i].stationName + "*");
            found = true;
        }
    }

    //if the frequency doesn't belongs to a known station, print custom station name
    if (found == false) {
        lcd.setCursor(0, 0);
        lcd.print("RADIO ARDUINO");
    }

    //print frequency on LCD display
    lcd.setCursor(0, 1);
    lcd.print("MHz ");
    lcd.print(frequency);
}

/**
 * Function used to set default frequency
 */
void setDefaultFrequency(float defFreq)
{
    firstFreqCheck = true;
    previousFrequency = 0.0;
    defaultStationFrequency = defFreq;
    defaultPotentiometerFrequency = defFreq;
    radio.setFrequency(defFreq);
  
    checkStationFrequency(defFreq);
}

/**
 * Function used to setup WiFi connection
 */
boolean wifi_setup() 
{
    delay(100);
 
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    //set WiFi SSID and password
    WiFi.begin(ssid.c_str(), password.c_str());

    //check wifi status for some attempts
    for (int i=0; (WiFi.status() != WL_CONNECTED) && (i < WIFI_ATTEMPTS); i++) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    //print some WiFi data
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.println("WiFi Connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        Serial.print("Gateway address: ");
        Serial.println(WiFi.gatewayIP());

        return true;
        
    } else {
        //return false when WiFi connection fails
        Serial.println("WiFi connection Failed!");
        return false;
    }
}

/**
 * Function used to connect to MQTT Broker and subscribe to specified topic
 */
void mqtt_reconnect() {

    //flag used to stop reconnecting attempts
    bool mqttStopReconnect = false;
    
    mqttAttempts = 0;

    do {
        //check if WiFi is connected, if not try reconnect
        if (WiFi.status() != WL_CONNECTED)
            wifi_setup();

        //check if client is connected
        if (!mqtt_client.connected()) {
     
            Serial.println("Attempting MQTT connection...");

            //connect to MQTT Broker
            if (mqtt_client.connect(clientId.c_str())) {
              
                Serial.println("MQTT Connected");
    
                String firstMsg = "Client " + clientId + " connected!";
                String currentTopic = nodeName + group;

                //publish connection message to specified topic
                mqtt_client.publish(currentTopic.c_str(), firstMsg.c_str());

                //subscribe to specified topic
                mqtt_client.subscribe(currentTopic.c_str());

                //set last reconnection time to 0
                lastReconnectAttempt = 0;

                mqttStopReconnect = true;
     
            } else {
                //if connection fails, print client state and retry after some seconds
                Serial.print("failed, rc=");
                Serial.println(mqtt_client.state());
                Serial.println("Attempt:" + String(mqttAttempts+1) + ", skipping MQTT, retry in 5 seconds");
                delay(5000);

                //if connection attempts reaches the attempts limit, stop retrying
                mqttAttempts++;
                if (mqttAttempts >= MQTT_ATTEMPTS) {
                    mqttStopReconnect = true;
                    Serial.println("Error: MQTT attempts limit reached! Retry manually with mqqtProbe/ command");
                }
            }
        }

    } while (mqttStopReconnect == false);
}

/**
 * Function used to publish message to all topic subscribers
 */
bool mqtt_send(String msgToSend) 
{
    //check if WiFi is connected, if not try reconnect
    if (WiFi.status() != WL_CONNECTED)
        wifi_setup();
  
    if (WiFi.status() == WL_CONNECTED) {

        //check if MQTT client is connected, if not try reconnect
        if (!mqtt_client.connected())
            mqtt_reconnect();

        //check if MQTT client is connected, if not skipping message sending
        if (mqtt_client.connected()) {
          
            mqtt_client.loop();

            //setup message to publish
            snprintf (msg, MSG_LEN, "%s", msgToSend.c_str());

            String currentTopic = nodeName + group;
            //try to publish message and print the result message
            if (mqtt_client.publish(currentTopic.c_str(), msg)) {
                Serial.println("Info: MQTT message succesfully published");
                return true;
            } else { Serial.println("Error: MQTT publishing error (connection error or message too large)"); }
                
        } else { Serial.println("Error: MQTT client not connected"); }
    }
    return false;
}
