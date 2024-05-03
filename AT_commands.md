## 1: Station mode
## Set the Wi-Fi mode of ESP32.
```
AT+CWMODE=1
```

## Query the AP to which the ESP32 Station is already connected.
```
AT+CWJAP?
+CWJAP:"My home","67:8c:d3:34:0a:98",1,-54,0,1,3,0,0
```

## Connect an ESP32 station to a targeted AP.
```
AT+CWJAP="My home", "PASSWORD"
--------------------------------
WIFI DISCONNECT
WIFI CONNECTED
WIFI GOT IP

OK
--------------------------------
or
----------------
+CWJAP:<error code>
ERROR
----------------
```

## Get IP address of ESP32 station
```
AT+CIPSTA?
-----------
+CIPSTA:ip:<"ip">
+CIPSTA:gateway:<"gateway">
+CIPSTA:netmask:<"netmask">
+CIPSTA:ip6ll:<"ipv6 addr">
+CIPSTA:ip6gl:<"ipv6 addr">

OK
---------------
+CIPSTA:ip:"192.168.1.20"
+CIPSTA:gateway:"192.168.1.254"
+CIPSTA:netmask:"255.255.255.0"

OK
-------------
```

### Set MQTT User Configuration.
## <LinkID>: currently only supports link ID 0.
## <scheme>: 2: MQTT over TLS (no certificate verify).
## <client_id>
## <username>
## <password>
```
AT+MQTTCONN?
-------------
+MQTTCONN:<LinkID>,<state>,<scheme><"host">,<port>,<"path">,<reconnect>
OK
-------------
+MQTTCONN:0,0,0,"","","",0 // Not connected

OK
-------------

AT+MQTTUSERCFG=0,1,"mqtt_client2","","",0,0,""
-----------
OK
-----------
```


## Connect to MQTT brokers.
```
AT+MQTTCONN=0,"192.168.1.8",1883,1
----
+MQTTCONNECTED:0,1,"192.168.1.7","1883","",1

OK
-----

```

## List all MQTT topics that have been already subscribed.
```
AT+MQTTSUB=0,"topic",1
OK
----
+MQTTSUBRECV:<LinkID>,<"topic">,<data_length>,data // Received data. Example: +MQTTSUBRECV:0,"test",9,hello one
----
ALREADY SUBSCRIBE // If the topic has been subscribed before, it will prompt:


AT+MQTTSUB?
-----------
+MQTTSUB:0,6,"test",1

OK
-----------
```

## Publish MQTT messages in string.
```
AT+MQTTPUB=0,"topic","test",2,0
OK
```


## Close connection to MQTT
```
AT+MQTTCLEAN=0
OK

13 (\r) & 10 (\n)
```
