/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

// Author: @yting27, @hcyyc2


#include "mbed.h"
#include "rtos/ThisThread.h"
#include <cstdio>
#include "TextLCD.h"
#include "Keypad.h"
#include <cstdlib>
#include <cstring>
#include <stdio.h>
#include <string>
#include <vector>
#include <regex>
#include <nlohmann/json.hpp>
using json = nlohmann::json;


// Define constants for Wi-Fi
#define TX PA_9  // UART TX (connected to RX of ESP8266)
#define RX PA_10 // UART RX (connected to TX of ESP8266)
#define SSID "MY home"
#define WIFI_PASSWORD "SECRET"
#define MQTT_HOST "192.168.1.111" // Your computer IP address (use ipconfig)

// Define constants for order details
#define TABLE_NUM "1"


// Define variables for wifi connection
Thread wifiConnectThread;
BufferedSerial esp(TX, RX); // UART TX,RX
bool isWifiConnected = false;
bool mqtt_is_connected = false;
string my_ip = "";

// Define variables for LCD display
Thread displayThread;
TextLCD myLCD(PC_4, PC_5, PC_6, PC_7, PC_8, PC_9);
int displayID;

// Define variables for keypad
Thread inputThread;
Keypad key(PC_1, PC_0, PC_13, PC_12, PC_2, PC_3, PC_10, PC_11); //Column 1, Column 2, Column 3, Column 4, Row 4, Row 3, Row 2 , Row 1
char const *key_table = "?*7410852#963DCBA";

// Define pins for order status LEDs and buttons
DigitalOut led_queuing(PB_1, 0);
DigitalOut led_preparing(PB_2, 0);
DigitalOut led_ready(PB_3, 0);
DigitalOut led_served(PB_5, 0);
DigitalIn confirmButton(PB_8);
DigitalIn noButton(PB_9);

// Define variables for food ordering
string food_string;
char food_id[10];
char food_qty[10];
bool flag_food = false;
bool flag_qty = false;
Mutex mut;

// --------- Function declaration -------------
// Handle wifi connections
void setupWifi();
void setupMQTT();
void handle_mqtt_subs(const char* topic, const char* message);
void readWifiMessage();

// Handle food ordering
void introduce();
void getfood();
void getQty();
void cont();
void thanks();
void addNewOrder(json order);
void displayInfo(int *displayID);
void getInput();
void foodstring_strip(string input); // Prepare order string to be sent to server

// Utility function
vector<string> splitByDelimiter(string given_str, string delim);
int char_arr_size(const char *str);



int main() {
    esp.set_baud(115200); // Set baud rate to 115200

    printf("Connecting to wifi...\n");
    wifiConnectThread.start(readWifiMessage); // Listen to incoming message

    setupWifi(); // Connect to Wi-Fi
    setupMQTT(); // Setup MQTT

    // Start displayThread
    displayID = 0;
    displayThread.start(callback(displayInfo, &displayID));

    // Start inputThread
    inputThread.start(getInput);
    
    while (1) {
    }
}


/*
** Send a new order to web server
*/
void addNewOrder(json order) {
    char publish_sub[256]; // Increase the size if not enough!!!
    string orderStr = order.dump() + "\r\n";
    int lenOfData = char_arr_size(orderStr.c_str());
    
    // Prepare to send raw data
    sprintf(publish_sub, "AT+MQTTPUBRAW=0,\"fromDevice_order\",%i,0,0\r\n", lenOfData);
    esp.write(publish_sub, char_arr_size(publish_sub));
    ThisThread::sleep_for(1s);

    esp.write(orderStr.c_str(), lenOfData); // Send data to the subscriber (web server)
    ThisThread::sleep_for(2s);

    printf("Sent new order JSON to 'fromDevice_order' topic.\n");

    led_queuing = 1; // Set LED status to 1 (queuing)
}


/*
** Setup connection to Wi-Fi
*/
void setupWifi() {
    // esp.write("AT+RST\r\n", char_arr_size("AT+RST\r\n"));
    // ThisThread::sleep_for(1s);

    // Set ESP8266 to station mode
    esp.write("AT+CWMODE=1\r\n", char_arr_size("AT+CWMODE=1\r\n"));
    ThisThread::sleep_for(2s);

    // Connect to your AP
    char connectToAP[50];
    sprintf(connectToAP, "AT+CWJAP=\"%s\",\"%s\"\r\n", SSID, WIFI_PASSWORD);
    esp.write(connectToAP, char_arr_size(connectToAP));
    ThisThread::sleep_for(8s);

    while (!isWifiConnected) {
        // Wait until wifi is connected!!!
        ThisThread::sleep_for(1s);
    }
    printf("Wifi connected!\n");

    // Return the IP address
    esp.write("AT+CIPSTA?\r\n", char_arr_size("AT+CIPSTA?\r\n")); 
    ThisThread::sleep_for(2s);
}

/*
** Setup connection to the MQTT
*/
void setupMQTT() {
    // Clear any MQTT connection
    esp.write("AT+MQTTCLEAN=0\r\n", char_arr_size("AT+MQTTCLEAN=0\r\n")); 
    ThisThread::sleep_for(8s);

    // Configure MQTT
    const char* mqtt_connect_str = "AT+MQTTUSERCFG=0,1,\"mqtt_client2\",\"\",\"\",0,0,\"\"\r\n";
    esp.write(mqtt_connect_str, char_arr_size(mqtt_connect_str)); 
    ThisThread::sleep_for(2s);

    // Start connection to MQTT host (IMPORTANT: Please ensure the MQTT broker is running!!!)
    char mqtt_str[50];
    sprintf(mqtt_str, "AT+MQTTCONN=0,\"%s\",1883,1\r\n", MQTT_HOST);
    esp.write(mqtt_str, char_arr_size(mqtt_str)); 
    ThisThread::sleep_for(2s);

    // Check MQTT connection
    esp.write("AT+MQTTCONN?\r\n", char_arr_size("AT+MQTTCONN?\r\n")); 
    ThisThread::sleep_for(500ms);
    printf("Waiting for MQTT connection to be established...\n");
    while(!mqtt_is_connected) {
        ThisThread::sleep_for(1s);
    }
    printf("MQTT host is connected!\n");

    // Subscribe to topic: toDevice_order
    esp.write("AT+MQTTSUB=0,\"toDevice_order\",1\r\n", char_arr_size("AT+MQTTSUB=0,\"toDevice_order\",1\r\n")); 
    ThisThread::sleep_for(2s);
    printf("Subscribed to 'toDevice_order' topic.\n");
}

/*
** Handle MQTT messages received
*/
void handle_mqtt_subs(const char* topic, const char* rawMessage) {
    // REPLACE garbage characters received (removed control characters)
    string message = regex_replace(rawMessage, regex("[[:cntrl:]]"), "");

    if (strcmp(topic, "toDevice_order") == 0) {
        printf("### %s\n\n", message.c_str());
        
        // Parse the order status message received
        smatch matches;
        if (regex_search(message, matches, regex("\"action\":\"(.*?)\",\"table_num\":(.*?),\"order_status\":(.*?)\\}"))) {
            string action = matches[1].str();
            string tableNum = matches[2].str();
            string order_status = matches[3].str();

            if (tableNum == TABLE_NUM) {
                printf("action: %s\n", action.c_str());
                printf("table_num: %s\n", tableNum.c_str());
                printf("order_status: %s\n", order_status.c_str());

                // Output to LEDs.
                // Turn off all LEDs.
                led_queuing = 0;
                led_preparing = 0;
                led_ready = 0;
                led_served = 0;

                // Turn on LED based on status
                if (order_status == "1") {
                    led_queuing = 1;
                } else if (order_status == "2") {
                    led_preparing = 1;
                } else if (order_status == "3") {
                    led_ready = 1;
                } else if (order_status == "4") {
                    led_served = 1;
                }

            } else {
                printf("Table number (%s) doesn't match with this device (%s)\n", tableNum.c_str(), TABLE_NUM);
            }
            
        }
    }
}

/*
** Handle and process wifi message received
*/
void readWifiMessage() {
    string unreadBuffer = "";
    vector<string> splittedStr;

    while (1) {
        while (esp.readable()) {
            char currBuff[64]; // Buffer to store current data
            esp.read(currBuff, 64);

            unreadBuffer += currBuff;
            printf("%s", currBuff);
        }

        // Check returned messages
        splittedStr = splitByDelimiter(unreadBuffer, "\r\n");
        if (splittedStr.capacity() == 1) { // If there is nothing to split, put the message to unreaBuffer
            unreadBuffer = splittedStr.front();

        } else if (splittedStr.capacity() > 1) {
            unreadBuffer = splittedStr.back();
            splittedStr.pop_back();

            // Handle message returned
            for (auto i = splittedStr.begin(); i != splittedStr.end(); ++i) {
                smatch matches;

                if (regex_search(*i, regex("WIFI CONNECTED"))) {
                    isWifiConnected = true; // Wifi is connected
                } else if (regex_search(*i, regex("WIFI DISCONNECT"))) { // If wifi disconnected
                    // isWifiConnected = false;
                } else if (regex_search(*i, matches, regex("\\+CIPSTA:ip:\"(.*)\""))) { // Get IP address
                    my_ip = matches[1].str();

                    printf("Device's IP address: %s\n", my_ip.c_str());

                } else if (regex_search(*i, matches, regex("\\+MQTTCONNECTED:0,1,\"(.*)\",\"1883\",\"\",1"))) { // Connected to MQTT broker
                    string matched_mqtt = matches[1].str();
                    if (matched_mqtt.compare("0.0.0.0") != 0) {
                        mqtt_is_connected = true;
                    } else {
                        printf("Sorry the device is not connected to MQTT: %s\n", matched_mqtt.c_str());
                    }
                    
                } else if (regex_search(*i, matches, regex("\\+MQTTSUBRECV:0,\"toDevice_order\",(.*?),(.*)"))) { // If any message received from MQTT broker
                    handle_mqtt_subs("toDevice_order", matches[2].str().c_str());
                    ThisThread::sleep_for(1s);
                }
            }
                
        } else {
            unreadBuffer = "";
        }

        ThisThread::sleep_for(1s);
    }
}

/*
** LCD: Introduction
*/
void introduce(){
    myLCD.locate(4, 0);
    myLCD.printf("WELCOME");

    //Serial log
    printf("WELCOME\n");
}

/*
** LCD: Fetch food order
*/
void getfood(){
    myLCD.locate(0, 0);
    myLCD.printf("Food ID: %s", food_id);

    //Serial Log
    printf("Food ID: %s\n", food_id);
}

/*
** LCD: Fetch food quantity
*/
void getQty(){
    myLCD.locate(0, 0);
    myLCD.printf("Qty: %s", food_qty);

    //Serial Log
    printf("Qty: %s\n", food_qty);
}

/*
** LCD: Continue to fetch or not
*/
void cont(){
    myLCD.locate(0, 0);
    myLCD.printf("Add more ?");

    //Serial Log
    printf("Add more ?\n");
}


/*
** LCD: Thanks
*/
void thanks(){
    myLCD.locate(4, 0);
    myLCD.printf("THANKS!");

    //Serial log
    printf("THANKS!\n");
}

/*
** LED Display - Thread
*/
void displayInfo(int *displayID){
    //Always display the information
    while (1) {
        switch (*displayID) {
            case 0:
                myLCD.cls();
                introduce();
                ThisThread::sleep_for(3s);
                break;
            case 1:
                myLCD.cls();
                getfood();
                ThisThread::sleep_for(1s);
                break;
            case 2:
                myLCD.cls();
                getQty();
                ThisThread::sleep_for(1s);
                break;
            case 3:
                myLCD.cls();
                cont();
                ThisThread::sleep_for(1s);
                break;
            case 4:
                myLCD.cls();
                thanks();
                ThisThread::sleep_for(1s);
                break;
        }
    }
}

/**
** Process food order entered by customers into json format and send to the web server. 
**/
void foodstring_strip(string input)
{
    json order_data;
    order_data["action"] = "add_order";
    order_data["items"] = {};
    order_data["table_num"] = TABLE_NUM;

    for(int i = 0; i < input.length();){
        string food = input.substr(i, 3);
        i += 3;
        int qty = stoi(input.substr(i, 1));
        i++;

        printf("Processed food id is: %s\n", food.c_str());
        printf("Processed qty is: %d\n", qty);

        order_data["items"] += {
            {"id", food.c_str()},
            {"qty", qty}
        };
    }

    addNewOrder(order_data);
}

/*
** Input - Thread
*/
void getInput(){

    mut.lock();
    displayID = 1;
    mut.unlock();

    uint32_t key_num;
    int i = 0; //counter for storing food 
    int j = 0; //counter for storing qty

    while (true) {
        while ((key_num = key.read()) != 0 || confirmButton == 0 || noButton == 0){

            char input_key = *(key_table + key_num);

            //Confirmation of food item
            //To be implemented with Gesture or button
            if (displayID == 1 && confirmButton == 0) {
                //Confirm food
                if (flag_food == false && flag_qty == false) {
                    
                    flag_food = true;
                    //Serial log
                    printf("Confirmed Food ID: %s\n", food_id);
                    while(confirmButton == 0);

                    //Once confirm food, proceed to ask about the food qty
                    mut.lock();
                    displayID = 2;
                    mut.unlock();
                }

                ThisThread::sleep_for(10ms);           
            }

            //Confirmation of food qty
            //To be implemented with Gesture or button
            else if (displayID == 2 && confirmButton == 0) {
                //Confirm qty
                if (flag_food == true && flag_qty == false) {

                    flag_qty = true;
                    
                    //Serial log
                    printf("Confirmed Food Qty: %s\n", food_qty);
                    while(confirmButton == 0);
 
                    //Prompt to ask for continuation
                    mut.lock();
                    displayID = 3;
                    mut.unlock();
                }

                ThisThread::sleep_for(10ms);
            }

            //Continue to take order
            else if (displayID == 3) {

                //If press YES, continue to take  
                if (confirmButton == 0) {
                    
                    flag_food = false;
                    flag_qty = false;
                
                    //Process food_id and food_qty into food_string
                    food_string.append(food_id);
                    food_string.append(food_qty);

                    //Then clear the mem and counter 
                    memset(food_id, 0, sizeof food_id);
                    memset(food_qty, 0, sizeof food_qty);
                    i = 0;
                    j = 0;

                    while(confirmButton == 0);

                    //Switch to ask for food
                    mut.lock(); 
                    displayID = 1;
                    mut.unlock();    
                }

                //If press NO, quit to order
                if (noButton == 0) {
                    
                    //Process the final food_id and food_qty into food_string
                    food_string.append(food_id);
                    food_string.append(food_qty);

                    while(noButton == 0);

                    //Switch to thank you screen 
                    mut.lock();
                    displayID = 4;
                    mut.unlock();

                    //Serial Log
                    //Write a function to preprocess the food_string here
                    printf("The food string is %s\n", food_string.c_str());
                    foodstring_strip(food_string);
                }

                ThisThread::sleep_for(10ms);
            }

            //Deleting character
            //To be implemented with Gesture or button
            else if (noButton == 0) {
                while(noButton == 0);

                myLCD.cls();
                if(displayID == 1)
                    food_id[--i] = '\0';
                
                else if(displayID == 2)
                    food_qty[--j] = '\0';
                ThisThread::sleep_for(10ms);
            }

            //Increment the character to form food_ID
            else if (displayID == 1) {
                food_id[i++] = *(key_table + key_num);
            }
            else if (displayID == 2) {
                food_qty[j++] = *(key_table + key_num);
            }

        }

        ThisThread::sleep_for(500ms);
    }
}


/*
** Split string by certain delimiter
*/
vector<string> splitByDelimiter(string given_str, string delim="\r\n") {
    size_t pos = 0;
    string token; // define a string variable
    vector<string> splittedStr;

    // use find() function to get the position of the delimiters
    while ((pos = given_str.find(delim)) != std::string::npos) {
        token = given_str.substr(0, pos); // store the substring
        splittedStr.push_back(token);

        given_str.erase(0, pos + delim.length()); // Remove substring from the current positon to the delimitter found
    }
    
    if (given_str.length() > 0) {
        splittedStr.push_back(given_str);
    }

    return splittedStr;
}


/*
** Count c-string length
*/
int char_arr_size(const char *str) {
  size_t Size = strlen(str);
  return Size;
}

