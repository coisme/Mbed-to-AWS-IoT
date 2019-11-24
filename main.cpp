/*******************************************************************************
 * Copyright (c) 2014, 2015 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *    Ian Craggs - make sure QoS2 processing works, and add device headers
 *******************************************************************************/

 /**
  This is a sample program to illustrate the use of the MQTT Client library
  on the mbed platform.  The Client class requires two classes which mediate
  access to system interfaces for networking and timing.  As long as these two
  classes provide the required public programming interfaces, it does not matter
  what facilities they use underneath. In this program, they use the mbed
  system libraries.

 */

#define MQTTCLIENT_QOS1 0
#define MQTTCLIENT_QOS2 0

#include "mbed.h"
#include "NTPClient.h"
#include "TLSSocket.h"
#include "MQTTClientMbedOs.h"
#include "MQTT_server_setting.h"
#include "mbed-trace/mbed_trace.h"
#include "mbed_events.h"
#include "mbedtls/error.h"

#define LED_ON  MBED_CONF_APP_LED_ON
#define LED_OFF MBED_CONF_APP_LED_OFF

static volatile bool isPublish = false;

/* Flag to be set when received a message from the server. */
static volatile bool isMessageArrived = false;
/* Buffer size for a receiving message. */
const int MESSAGE_BUFFER_SIZE = 256;
/* Buffer for a receiving message. */
char messageBuffer[MESSAGE_BUFFER_SIZE];

// An event queue is a very useful structure to debounce information between contexts (e.g. ISR and normal threads)
// This is great because things such as network operations are illegal in ISR, so updating a resource in a button's fall() function is not allowed
EventQueue eventQueue;
Thread thread1;

/*
 * Callback function called when a message arrived from server.
 */
void messageArrived(MQTT::MessageData& md)
{
    // Copy payload to the buffer.
    MQTT::Message &message = md.message;
    if(message.payloadlen >= MESSAGE_BUFFER_SIZE) {
        // TODO: handling error
    } else {
        memcpy(messageBuffer, message.payload, message.payloadlen);
    }
    messageBuffer[message.payloadlen] = '\0';

    isMessageArrived = true;
}

/*
 * Callback function called when the button1 is clicked.
 */
void btn1_rise_handler() {
    isPublish = true;
}


int main(int argc, char* argv[])
{
    mbed_trace_init();
    
    const float version = 1.0;
    bool isSubscribed = false;

    NetworkInterface* network = NULL;
    TLSSocket *socket = new TLSSocket; // Allocate on heap to avoid stack overflow.
    MQTTClient* mqttClient = NULL;

    DigitalOut led(MBED_CONF_APP_LED_PIN, LED_ON);

    printf("HelloMQTT: version is %.2f\r\n", version);
    printf("\r\n");

#ifdef MBED_MAJOR_VERSION
    printf("Mbed OS version %d.%d.%d\n\n", MBED_MAJOR_VERSION, MBED_MINOR_VERSION, MBED_PATCH_VERSION);
#endif

    printf("Opening network interface...\r\n");
    {
        network = NetworkInterface::get_default_instance();
        if (!network) {
            printf("Error! No network inteface found.\n");
            return -1;
        }

        printf("Connecting to network\n");
        nsapi_size_or_error_t ret = network->connect();
        if (ret) {
            printf("Unable to connect! returned %d\n", ret);
            return -1;
        }
    }
    printf("Network interface opened successfully.\r\n");
    printf("\r\n");

    // sync the real time clock (RTC)
    NTPClient ntp(network);
    ntp.set_server("time.google.com", 123);
    time_t now = ntp.get_timestamp();
    set_time(now);
    printf("Time is now %s", ctime(&now));

    printf("Connecting to host %s:%d ...\r\n", MQTT_SERVER_HOST_NAME, MQTT_SERVER_PORT);
    {
        nsapi_error_t ret = socket->open(network);
        if (ret != NSAPI_ERROR_OK) {
            printf("Could not open socket! Returned %d\n", ret);
            return -1;
        }
        ret = socket->set_root_ca_cert(SSL_CA_PEM);
        if (ret != NSAPI_ERROR_OK) {
            printf("Could not set ca cert! Returned %d\n", ret);
            return -1;
        }
        ret = socket->set_client_cert_key(SSL_CLIENT_CERT_PEM, SSL_CLIENT_PRIVATE_KEY_PEM);
        if (ret != NSAPI_ERROR_OK) {
            printf("Could not set keys! Returned %d\n", ret);
            return -1;
        }
        ret = socket->connect(MQTT_SERVER_HOST_NAME, MQTT_SERVER_PORT);
        if (ret != NSAPI_ERROR_OK) {
            printf("Could not connect! Returned %d\n", ret);
            return -1;
        }
    }
    printf("Connection established.\r\n");
    printf("\r\n");

    printf("MQTT client is trying to connect the server ...\r\n");
    {
        MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
        data.MQTTVersion = 3;
        data.clientID.cstring = (char *)MQTT_CLIENT_ID;
        data.username.cstring = (char *)MQTT_USERNAME;
        data.password.cstring = (char *)MQTT_PASSWORD;

        mqttClient = new MQTTClient(socket);
        int rc = mqttClient->connect(data);
        if (rc != MQTT::SUCCESS) {
            printf("ERROR: rc from MQTT connect is %d\r\n", rc);
            return -1;
        }
    }
    printf("Client connected.\r\n");
    printf("\r\n");


    printf("Client is trying to subscribe a topic \"%s\".\r\n", MQTT_TOPIC_SUB);
    {
        int rc = mqttClient->subscribe(MQTT_TOPIC_SUB, MQTT::QOS0, messageArrived);
        if (rc != MQTT::SUCCESS) {
            printf("ERROR: rc from MQTT subscribe is %d\r\n", rc);
            return -1;
        }
        isSubscribed = true;
    }
    printf("Client has subscribed a topic \"%s\".\r\n", MQTT_TOPIC_SUB);
    printf("\r\n");

    // Enable button 1
    InterruptIn btn1 = InterruptIn(MBED_CONF_APP_USER_BUTTON);
    btn1.rise(btn1_rise_handler);
    
    printf("To send a packet, push the button 1 on your board.\r\n\r\n");

    // Turn off the LED to let users know connection process done.
    led = LED_OFF;

    while(1) {
        /* Check connection */
        if(!mqttClient->isConnected()){
            break;
        }
        /* Pass control to other thread. */
        if(mqttClient->yield() != MQTT::SUCCESS) {
            break;
        }
        /* Received a control message. */
        if(isMessageArrived) {
            isMessageArrived = false;
            // Just print it out here.
            printf("\r\nMessage arrived:\r\n%s\r\n\r\n", messageBuffer);
        }
        /* Publish data */
        if(isPublish) {
            isPublish = false;
            static unsigned short id = 0;
            static unsigned int count = 0;

            count++;

            // When sending a message, LED lights blue.
            led = LED_ON;

            MQTT::Message message;
            message.retained = false;
            message.dup = false;

            const size_t buf_size = 100;
            char *buf = new char[buf_size];
            message.payload = (void*)buf;

            message.qos = MQTT::QOS0;
            message.id = id++;
            int ret = snprintf(buf, buf_size, "%d", count);
            if(ret < 0) {
                printf("ERROR: snprintf() returns %d.", ret);
                continue;
            }
            message.payloadlen = ret;
            // Publish a message.
            printf("Publishing message.\r\n");
            int rc = mqttClient->publish(MQTT_TOPIC_SUB, message);
            if(rc != MQTT::SUCCESS) {
                printf("ERROR: rc from MQTT publish is %d\r\n", rc);
            }
            printf("Message published.\r\n");
            delete[] buf;    

            led = LED_OFF;
        }
    }

    printf("The client has disconnected.\r\n");

    if(mqttClient) {
        if(isSubscribed) {
            mqttClient->unsubscribe(MQTT_TOPIC_SUB);
        }
        if(mqttClient->isConnected()) 
            mqttClient->disconnect();
        delete mqttClient;
    }
    if(socket) {
        socket->close();
    }
    if(network) {
        network->disconnect();
        // network is not created by new.
    }
}
