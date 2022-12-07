/*
 * Copyright (c) 2016, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*****************************************************************************

   Application Name     -   MQTT Client
   Application Overview -   The device is running a MQTT client which is
                           connected to the online broker. Three LEDs on the
                           device can be controlled from a web client by
                           publishing msg on appropriate topics. Similarly,
                           message can be published on pre-configured topics
                           by pressing the switch buttons on the device.

   Application Details  - Refer to 'MQTT Client' README.html

*****************************************************************************/
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <mqueue.h>
#include <stdio.h>

#include <ti/drivers/net/wifi/simplelink.h>
#include <ti/drivers/net/wifi/slnetifwifi.h>

#include <ti/drivers/SPI.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/Timer.h>

#include <ti/net/mqtt/mqttclient.h>

#include "network_if.h"
#include "uart_term.h"
#include "mqtt_if.h"
#include "debug_if.h"

#include "ti_drivers_config.h"

extern int32_t ti_net_SlNet_initConfig();

#define APPLICATION_NAME         "MQTT client"
#define APPLICATION_VERSION      "2.0.0"

#define SL_TASKSTACKSIZE            2048
#define SPAWN_TASK_PRIORITY         9

// un-comment this if you want to connect to an MQTT broker securely
//#define MQTT_SECURE_CLIENT

#define MQTT_MODULE_TASK_PRIORITY   2
#define MQTT_MODULE_TASK_STACK_SIZE 2048

#define MQTT_WILL_TOPIC             "cc32xx_will_topic"
#define MQTT_WILL_MSG               "will_msg_works"
#define MQTT_WILL_QOS               MQTT_QOS_2
#define MQTT_WILL_RETAIN            false

//#define MQTT_CLIENT_PASSWORD        "HybridLighting123"
#define MQTT_CLIENT_PASSWORD        NULL
//#define MQTT_CLIENT_USERNAME        "Jordy"
#define MQTT_CLIENT_USERNAME        NULL
#define MQTT_CLIENT_KEEPALIVE       0
#define MQTT_CLIENT_CLEAN_CONNECT   true
#define MQTT_CLIENT_MQTT_V3_1       true
#define MQTT_CLIENT_BLOCKING_SEND   true

#ifndef MQTT_SECURE_CLIENT
#define MQTT_CONNECTION_FLAGS           MQTTCLIENT_NETCONN_URL
#define MQTT_CONNECTION_ADDRESS         "mqtt.eclipseprojects.io"
#define MQTT_CONNECTION_PORT_NUMBER     1883
#else
#define MQTT_CONNECTION_FLAGS           MQTTCLIENT_NETCONN_IP4 | MQTTCLIENT_NETCONN_SEC
//#define MQTT_CONNECTION_FLAGS           MQTTCLIENT_NETCONN_URL | MQTTCLIENT_NETCONN_SEC
#define MQTT_CONNECTION_ADDRESS         "192.168.178.67"
//#define MQTT_CONNECTION_ADDRESS         "adf249e7a9ef46d2924fa18aab4b7589.s2.eu.hivemq.cloud"
#define MQTT_CONNECTION_PORT_NUMBER     8883
#endif

#define POWER_SOURCE_BATTERY    0
#define POWER_SOURCE_MAIN       1

char batteryPercentageString[32];
int powerSource;
int batteryPercentage;

mqd_t appQueue;
int connected;
int deinit;
Timer_Handle timer0;
Timer_Handle timer1;
int longPress = 0;

/* Client ID                                                                 */
/* If ClientId isn't set, the MAC address of the device will be copied into  */
/* the ClientID parameter.                                                   */
char ClientId[13] = {'\0'};
//char ClientId[13] = {'J','o','r','d','y','\0'};

enum{
    APP_MQTT_PUBLISH,
    APP_MQTT_CON_TOGGLE,
    APP_MQTT_DEINIT,
    APP_BTN_HANDLER
};

struct msgQueue
{
    int   event;
    char* payload;
};

MQTT_IF_InitParams_t mqttInitParams =
{
     MQTT_MODULE_TASK_STACK_SIZE,   // stack size for mqtt module - default is 2048
     MQTT_MODULE_TASK_PRIORITY      // thread priority for MQTT   - default is 2
};

MQTTClient_Will mqttWillParams =
{
     MQTT_WILL_TOPIC,    // will topic
     MQTT_WILL_MSG,      // will message
     MQTT_WILL_QOS,      // will QoS
     MQTT_WILL_RETAIN    // retain flag
};

MQTT_IF_ClientParams_t mqttClientParams =
{
     ClientId,                  // client ID
     MQTT_CLIENT_USERNAME,      // user name
     MQTT_CLIENT_PASSWORD,      // password
     MQTT_CLIENT_KEEPALIVE,     // keep-alive time
     MQTT_CLIENT_CLEAN_CONNECT, // clean connect flag
     MQTT_CLIENT_MQTT_V3_1,     // true = 3.1, false = 3.1.1
     MQTT_CLIENT_BLOCKING_SEND, // blocking send flag
     &mqttWillParams            // will parameters
};

#ifndef MQTT_SECURE_CLIENT
MQTTClient_ConnParams mqttConnParams =
{
     MQTT_CONNECTION_FLAGS,         // connection flags
     MQTT_CONNECTION_ADDRESS,       // server address
     MQTT_CONNECTION_PORT_NUMBER,   // port number of MQTT server
     0,                             // method for secure socket
     0,                             // cipher for secure socket
     0,                             // number of files for secure connection
     NULL                           // secure files
};
#else
/*
 * In order to connect to an MQTT broker securely, the MQTTCLIENT_NETCONN_SEC flag,
 * method for secure socket, cipher, secure files, number of secure files must be set
 * and the certificates must be programmed to the file system.
 *
 * The first parameter is a bit mask which configures the server address type and security mode.
 * Server address type: IPv4, IPv6 and URL must be declared with the corresponding flag.
 * All flags can be found in mqttclient.h.
 *
 * The flag MQTTCLIENT_NETCONN_SEC enables the security (TLS) which includes domain name
 * verification and certificate catalog verification. Those verifications can be skipped by
 * adding to the bit mask: MQTTCLIENT_NETCONN_SKIP_DOMAIN_NAME_VERIFICATION and
 * MQTTCLIENT_NETCONN_SKIP_CERTIFICATE_CATALOG_VERIFICATION.
 *
 * Note: The domain name verification requires URL Server address type otherwise, this
 * verification will be disabled.
 *
 * Secure clients require time configuration in order to verify the server certificate validity (date)
 */

/* Day of month (DD format) range 1-31                                       */
#define DAY                      1
/* Month (MM format) in the range of 1-12                                    */
#define MONTH                    12
/* Year (YYYY format)                                                        */
#define YEAR                     2022
/* Hours in the range of 0-23                                                */
#define HOUR                     12
/* Minutes in the range of 0-59                                              */
#define MINUTES                  00
/* Seconds in the range of 0-59                                              */
#define SEC                      00

char *MQTTClient_secureFiles[1] = {"ca-cert.pem"};
//char *MQTTClient_secureFiles[1] = {"auth-hivemq-cloud.pem"};

MQTTClient_ConnParams mqttConnParams =
{
    MQTT_CONNECTION_FLAGS,                  // connection flags
    MQTT_CONNECTION_ADDRESS,                // server address
    MQTT_CONNECTION_PORT_NUMBER,            // port number of MQTT server
    SLNETSOCK_SEC_METHOD_SSLv3_TLSV1_2,     // method for secure socket
    SLNETSOCK_SEC_CIPHER_FULL_LIST,         // cipher for secure socket
    1,                                      // number of files for secure connection
    MQTTClient_secureFiles                  // secure files
};

void setTime(){

    SlDateTime_t dateTime = {0};
    dateTime.tm_day = (uint32_t)DAY;
    dateTime.tm_mon = (uint32_t)MONTH;
    dateTime.tm_year = (uint32_t)YEAR;
    dateTime.tm_hour = (uint32_t)HOUR;
    dateTime.tm_min = (uint32_t)MINUTES;
    dateTime.tm_sec = (uint32_t)SEC;
    sl_DeviceSet(SL_DEVICE_GENERAL, SL_DEVICE_GENERAL_DATE_TIME,
                 sizeof(SlDateTime_t), (uint8_t *)(&dateTime));
}
#endif

//*****************************************************************************
//!
//! Set the ClientId with its own mac address
//! This routine converts the mac address which is given
//! by an integer type variable in hexadecimal base to ASCII
//! representation, and copies it into the ClientId parameter.
//!
//! \param  macAddress  -   Points to string Hex.
//!
//! \return void.
//!
//*****************************************************************************
int32_t SetClientIdNamefromMacAddress()
{
    int32_t ret = 0;
    uint8_t Client_Mac_Name[2];
    uint8_t Index;
    uint16_t macAddressLen = SL_MAC_ADDR_LEN;
    uint8_t macAddress[SL_MAC_ADDR_LEN];

    /*Get the device Mac address */
    ret = sl_NetCfgGet(SL_NETCFG_MAC_ADDRESS_GET, 0, &macAddressLen,
                       &macAddress[0]);

    /*When ClientID isn't set, use the mac address as ClientID               */
    if(ClientId[0] == '\0')
    {
        /*6 bytes is the length of the mac address                           */
        for(Index = 0; Index < SL_MAC_ADDR_LEN; Index++)
        {
            /*Each mac address byte contains two hexadecimal characters      */
            /*Copy the 4 MSB - the most significant character                */
            Client_Mac_Name[0] = (macAddress[Index] >> 4) & 0xf;
            /*Copy the 4 LSB - the least significant character               */
            Client_Mac_Name[1] = macAddress[Index] & 0xf;

            if(Client_Mac_Name[0] > 9)
            {
                /*Converts and copies from number that is greater than 9 in  */
                /*hexadecimal representation (a to f) into ascii character   */
                ClientId[2 * Index] = Client_Mac_Name[0] + 'a' - 10;
            }
            else
            {
                /*Converts and copies from number 0 - 9 in hexadecimal       */
                /*representation into ascii character                        */
                ClientId[2 * Index] = Client_Mac_Name[0] + '0';
            }
            if(Client_Mac_Name[1] > 9)
            {
                /*Converts and copies from number that is greater than 9 in  */
                /*hexadecimal representation (a to f) into ascii character   */
                ClientId[2 * Index + 1] = Client_Mac_Name[1] + 'a' - 10;
            }
            else
            {
                /*Converts and copies from number 0 - 9 in hexadecimal       */
                /*representation into ascii character                        */
                ClientId[2 * Index + 1] = Client_Mac_Name[1] + '0';
            }
        }
    }
    return(ret);
}

void timerCallback(Timer_Handle myHandle)
{
    longPress = 1;
}

// this timer callback toggles the LED once per second until the device connects to an AP
void timerLEDCallback(Timer_Handle myHandle)
{
    GPIO_toggle(CONFIG_GPIO_LED_0);
}

void timerSendDataCallback(Timer_Handle myHandle){
    int ret;
    struct msgQueue queueElement;

    queueElement.event = APP_MQTT_PUBLISH;
    ret = mq_send(appQueue, (const char*)&queueElement, sizeof(struct msgQueue), 0);
    if(ret < 0){
        LOG_ERROR("msg queue send error %d", ret);
    }
}

void pushButtonPublishHandler(uint_least8_t index)
{
    int ret;
    struct msgQueue queueElement;

    GPIO_disableInt(CONFIG_GPIO_BUTTON_0);

    queueElement.event = APP_MQTT_PUBLISH;
    ret = mq_send(appQueue, (const char*)&queueElement, sizeof(struct msgQueue), 0);
    if(ret < 0){
        LOG_ERROR("msg queue send error %d", ret);
    }

    batteryPercentage += 5;

    GPIO_clearInt(CONFIG_GPIO_BUTTON_0);
    GPIO_enableInt(CONFIG_GPIO_BUTTON_0);
}

void pushButtonConnectionHandler(uint_least8_t index)
{
    int ret;
    struct msgQueue queueElement;

    GPIO_disableInt(CONFIG_GPIO_BUTTON_1);

    ret = Timer_start(timer0);
    if(ret < 0){
        LOG_ERROR("failed to start the timer\r\n");
    }

    queueElement.event = APP_BTN_HANDLER;

    ret = mq_send(appQueue, (const char*)&queueElement, sizeof(struct msgQueue), 0);
    if(ret < 0){
        LOG_ERROR("msg queue send error %d", ret);
    }
}

int detectLongPress(){

    int buttonPressed;

    do{
        buttonPressed = GPIO_read(CONFIG_GPIO_BUTTON_1);
    }while(buttonPressed && !longPress);

    // disabling the timer in case the callback has not yet triggered to avoid updating longPress
    Timer_stop(timer0);

    if(longPress == 1){
        longPress = 0;
        return 1;
    }
    else{
        return 0;
    }
}


void MQTT_EventCallback(int32_t event){

    struct msgQueue queueElement;

    switch(event){

        case MQTT_EVENT_CONNACK:
        {
            deinit = 0;
            connected = 1;
            LOG_INFO("MQTT_EVENT_CONNACK\r\n");
            GPIO_clearInt(CONFIG_GPIO_BUTTON_1);
            GPIO_enableInt(CONFIG_GPIO_BUTTON_1);
            break;
        }

        case MQTT_EVENT_SUBACK:
        {
            LOG_INFO("MQTT_EVENT_SUBACK\r\n");
            break;
        }

        case MQTT_EVENT_PUBACK:
        {
            LOG_INFO("MQTT_EVENT_PUBACK\r\n");
            break;
        }

        case MQTT_EVENT_UNSUBACK:
        {
            LOG_INFO("MQTT_EVENT_UNSUBACK\r\n");
            break;
        }

        case MQTT_EVENT_CLIENT_DISCONNECT:
        {
            connected = 0;
            LOG_INFO("MQTT_EVENT_CLIENT_DISCONNECT\r\n");
            if(deinit == 0){
                GPIO_clearInt(CONFIG_GPIO_BUTTON_1);
                GPIO_enableInt(CONFIG_GPIO_BUTTON_1);
            }
            break;
        }

        case MQTT_EVENT_SERVER_DISCONNECT:
        {
            connected = 0;

            LOG_INFO("MQTT_EVENT_SERVER_DISCONNECT\r\n");

            queueElement.event = APP_MQTT_CON_TOGGLE;
            int res = mq_send(appQueue, (const char*)&queueElement, sizeof(struct msgQueue), 0);
            if(res < 0){
                LOG_ERROR("msg queue send error %d", res);
            }
            break;
        }

        case MQTT_EVENT_DESTROY:
        {
            LOG_INFO("MQTT_EVENT_DESTROY\r\n");
            break;
        }
    }
}

/*
 * Subscribe topic callbacks
 * Topic and payload data is deleted after topic callbacks return.
 * User must copy the topic or payload data if it needs to be saved.
 */
void BrokerCB(char* topic, char* payload, uint8_t qos){
    LOG_INFO("TOPIC: %s PAYLOAD: %s QOS: %d\r\n", topic, payload, qos);
}

void ToggleLED1CB(char* topic, char* payload, uint8_t qos){
    GPIO_toggle(CONFIG_GPIO_LED_0);
    LOG_INFO("TOPIC: %s PAYLOAD: %s QOS: %d\r\n", topic, payload, qos);
}

void ToggleLED2CB(char* topic, char* payload, uint8_t qos){
    GPIO_toggle(CONFIG_GPIO_LED_1);
    LOG_INFO("TOPIC: %s PAYLOAD: %s QOS: %d\r\n", topic, payload, qos);
}

void ToggleLED3CB(char* topic, char* payload, uint8_t qos){
    GPIO_toggle(CONFIG_GPIO_LED_2);
    LOG_INFO("TOPIC: %s PAYLOAD: %s QOS: %d\r\n", topic, payload, qos);
}

void TogglePowerCB(char* topic, char* payload, uint8_t qos){
    if(powerSource == POWER_SOURCE_BATTERY){
        powerSource = POWER_SOURCE_MAIN;
    } else if(powerSource == POWER_SOURCE_MAIN){
        powerSource = POWER_SOURCE_BATTERY;
    }
    LOG_INFO("TOPIC: %s PAYLOAD: %s QOS: %d\r\n", topic, payload, qos);
}

void BatteryUpCB(char* topic, char* payload, uint8_t qos){
    batteryPercentage += 10;
    LOG_INFO("TOPIC: %s PAYLOAD: %s QOS: %d\r\n", topic, payload, qos);
}

void BatteryDownCB(char* topic, char* payload, uint8_t qos){
    batteryPercentage -= 10;
    LOG_INFO("TOPIC: %s PAYLOAD: %s QOS: %d\r\n", topic, payload, qos);
}

void Battery0CB(char* topic, char* payload, uint8_t qos){
    batteryPercentage = 0;
    LOG_INFO("TOPIC: %s PAYLOAD: %s QOS: %d\r\n", topic, payload, qos);
}

void Battery100CB(char* topic, char* payload, uint8_t qos){
    batteryPercentage = 100;
    LOG_INFO("TOPIC: %s PAYLOAD: %s QOS: %d\r\n", topic, payload, qos);
}


int32_t DisplayAppBanner(char* appName, char* appVersion){

    int32_t ret = 0;
    uint8_t macAddress[SL_MAC_ADDR_LEN];
    uint16_t macAddressLen = SL_MAC_ADDR_LEN;
    uint16_t ConfigSize = 0;
    uint8_t ConfigOpt = SL_DEVICE_GENERAL_VERSION;
    SlDeviceVersion_t ver = {0};

    ConfigSize = sizeof(SlDeviceVersion_t);

    // get the device version info and MAC address
    ret = sl_DeviceGet(SL_DEVICE_GENERAL, &ConfigOpt, &ConfigSize, (uint8_t*)(&ver));
    ret |= (int32_t)sl_NetCfgGet(SL_NETCFG_MAC_ADDRESS_GET, 0, &macAddressLen, &macAddress[0]);

    UART_PRINT("\n\r\t============================================\n\r");
    UART_PRINT("\t   %s Example Ver: %s",appName, appVersion);
    UART_PRINT("\n\r\t============================================\n\r\n\r");

    UART_PRINT("\t CHIP: 0x%x\n\r",ver.ChipId);
    UART_PRINT("\t MAC:  %d.%d.%d.%d\n\r",ver.FwVersion[0],ver.FwVersion[1],
               ver.FwVersion[2],
               ver.FwVersion[3]);
    UART_PRINT("\t PHY:  %d.%d.%d.%d\n\r",ver.PhyVersion[0],ver.PhyVersion[1],
               ver.PhyVersion[2],
               ver.PhyVersion[3]);
    UART_PRINT("\t NWP:  %d.%d.%d.%d\n\r",ver.NwpVersion[0],ver.NwpVersion[1],
               ver.NwpVersion[2],
               ver.NwpVersion[3]);
    UART_PRINT("\t ROM:  %d\n\r",ver.RomVersion);
    UART_PRINT("\t HOST: %s\n\r", SL_DRIVER_VERSION);
    UART_PRINT("\t MAC address: %02x:%02x:%02x:%02x:%02x:%02x\r\n", macAddress[0],
               macAddress[1], macAddress[2], macAddress[3], macAddress[4],
               macAddress[5]);
    UART_PRINT("\n\r\t============================================\n\r");

    return(ret);
}

int WifiInit(){

    int32_t ret;
    SlWlanSecParams_t securityParams;
    pthread_t spawn_thread = (pthread_t) NULL;
    pthread_attr_t pattrs_spawn;
    struct sched_param pri_param;

    pthread_attr_init(&pattrs_spawn);
    pri_param.sched_priority = SPAWN_TASK_PRIORITY;
    ret = pthread_attr_setschedparam(&pattrs_spawn, &pri_param);
    ret |= pthread_attr_setstacksize(&pattrs_spawn, SL_TASKSTACKSIZE);
    ret |= pthread_attr_setdetachstate(&pattrs_spawn, PTHREAD_CREATE_DETACHED);
    ret = pthread_create(&spawn_thread, &pattrs_spawn, sl_Task, NULL);
    if(ret != 0){
        LOG_ERROR("could not create simplelink task\n\r");
        while(1);
    }

    Network_IF_ResetMCUStateMachine();

    Network_IF_DeInitDriver();

    ret = Network_IF_InitDriver(ROLE_STA);
    if(ret < 0){
        LOG_ERROR("Failed to start SimpleLink Device\n\r");
        while(1);
    }

    DisplayAppBanner(APPLICATION_NAME, APPLICATION_VERSION);

    SetClientIdNamefromMacAddress();

    GPIO_toggle(CONFIG_GPIO_LED_2);

    securityParams.Key = (signed char*)SECURITY_KEY;
    securityParams.KeyLen = strlen(SECURITY_KEY);
    securityParams.Type = SECURITY_TYPE;

    ret = Timer_start(timer0);
    if(ret < 0){
        LOG_ERROR("failed to start the timer\r\n");
    }

    ret = Network_IF_ConnectAP(SSID_NAME, securityParams);
    if(ret < 0){
        LOG_ERROR("Connection to an AP failed\n\r");
    }
    else{

        ret = sl_WlanProfileAdd((signed char*)SSID_NAME, strlen(SSID_NAME), 0, &securityParams, NULL, 7, 0);
        if(ret < 0){
            LOG_ERROR("failed to add profile %s\r\n", SSID_NAME);
        }
        else{
            LOG_INFO("profile added %s\r\n", SSID_NAME);
        }
    }

    Timer_stop(timer0);
    Timer_close(timer0);

    return ret;
}

char* getPowerSource(){
    if(batteryPercentage <= 20){
        powerSource = POWER_SOURCE_MAIN;
    }
    if(powerSource == POWER_SOURCE_BATTERY){
        GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_ON);
        GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_OFF);
        return "Battery Power";
    } else if(powerSource == POWER_SOURCE_MAIN){
        GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF);
        GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_ON);
        return "Main Power";
    }
    return "";
}

void mainThread(void * args){

    int32_t ret;
    mq_attr attr;
    Timer_Params params;
    Timer_Params params1;
    UART_Handle uartHandle;
    struct msgQueue queueElement;
    MQTTClient_Handle mqttClientHandle;

    uartHandle = InitTerm();
    UART_control(uartHandle, UART_CMD_RXDISABLE, NULL);

    GPIO_init();
    SPI_init();
    Timer_init();

    ret = ti_net_SlNet_initConfig();
    if(0 != ret)
    {
        LOG_ERROR("Failed to initialize SlNetSock\n\r");
    }
    GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF);
    GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_OFF);
    GPIO_write(CONFIG_GPIO_LED_2, CONFIG_GPIO_LED_OFF);

    GPIO_setCallback(CONFIG_GPIO_BUTTON_0, pushButtonPublishHandler);
    GPIO_setCallback(CONFIG_GPIO_BUTTON_1, pushButtonConnectionHandler);


    // configuring the timer to toggle an LED until the AP is connected
    Timer_Params_init(&params);
    params.period = 1000000;
    params.periodUnits = Timer_PERIOD_US;
    params.timerMode = Timer_CONTINUOUS_CALLBACK;
    params.timerCallback = (Timer_CallBackFxn)timerLEDCallback;

    timer0 = Timer_open(CONFIG_TIMER_0, &params);
    if (timer0 == NULL) {
        LOG_ERROR("failed to initialize timer 0\r\n");
        while(1);
    }

    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(struct msgQueue);
    appQueue = mq_open("appQueue", O_CREAT, 0, &attr);
    if(((int)appQueue) <= 0){
        while(1);
    }

    ret = WifiInit();
    if(ret < 0){
        while(1);
    }

    GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF);
    GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_OFF);
    GPIO_write(CONFIG_GPIO_LED_2, CONFIG_GPIO_LED_OFF);

    params.period = 1500000;
    params.periodUnits = Timer_PERIOD_US;
    params.timerMode = Timer_ONESHOT_CALLBACK;
    params.timerCallback = (Timer_CallBackFxn)timerCallback;

    timer0 = Timer_open(CONFIG_TIMER_0, &params);
    if (timer0 == NULL) {
        LOG_ERROR("failed to initialize timer 0\r\n");
        while(1);
    }

    Timer_Params_init(&params1);
    params1.period = 2000000;
    params1.periodUnits = Timer_PERIOD_US;
    params1.timerMode = Timer_CONTINUOUS_CALLBACK;
    params1.timerCallback = (Timer_CallBackFxn)timerSendDataCallback;

    timer1 = Timer_open(CONFIG_TIMER_1, &params1);
    if (timer1 == NULL) {
        LOG_ERROR("failed to initialize timer 1\r\n");
        while(1);
    }

    ret = Timer_start(timer1);
    if(ret < 0){
        LOG_ERROR("failed to start the timer 1\r\n");
    }


MQTT_DEMO:

    batteryPercentage = 0;
    powerSource = POWER_SOURCE_BATTERY;
    GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_ON);
    GPIO_write(CONFIG_GPIO_LED_1, CONFIG_GPIO_LED_OFF);


    ret = MQTT_IF_Init(mqttInitParams);
    if(ret < 0){
        while(1);
    }

#ifdef MQTT_SECURE_CLIENT
    setTime();
#endif

    /*
     * In case a persistent session is being used, subscribe is called before connect so that the module
     * is aware of the topic callbacks the user is using. This is important because if the broker is holding
     * messages for the client, after CONNACK the client may receive the messages before the module is aware
     * of the topic callbacks. The user may still call subscribe after connect but have to be aware of this.
     */
    ret = MQTT_IF_Subscribe(mqttClientHandle, "Broker/To/cc32xx", MQTT_QOS_2, BrokerCB);
    ret |= MQTT_IF_Subscribe(mqttClientHandle, "ems20/0974347/LED1", MQTT_QOS_2, ToggleLED1CB);
    ret |= MQTT_IF_Subscribe(mqttClientHandle, "ems20/0974347/LED2", MQTT_QOS_2, ToggleLED2CB);
    ret |= MQTT_IF_Subscribe(mqttClientHandle, "ems20/0974347/LED3", MQTT_QOS_2, ToggleLED3CB);
    ret |= MQTT_IF_Subscribe(mqttClientHandle, "HybridLighting/Power/Toggle", MQTT_QOS_2, TogglePowerCB);
    ret |= MQTT_IF_Subscribe(mqttClientHandle, "HybridLighting/Power/Battery/Up", MQTT_QOS_2, BatteryUpCB);
    ret |= MQTT_IF_Subscribe(mqttClientHandle, "HybridLighting/Power/Battery/Down", MQTT_QOS_2, BatteryDownCB);
    ret |= MQTT_IF_Subscribe(mqttClientHandle, "HybridLighting/Power/Battery/0", MQTT_QOS_2, Battery0CB);
    ret |= MQTT_IF_Subscribe(mqttClientHandle, "HybridLighting/Power/Battery/100", MQTT_QOS_2, Battery100CB);

    if(ret < 0){
        while(1);
    }
    else{
        LOG_INFO("Subscribed to all topics successfully\r\n");
    }

    mqttClientHandle = MQTT_IF_Connect(mqttClientParams, mqttConnParams, MQTT_EventCallback);
    if(mqttClientHandle < 0){
        while(1);
    }

    // wait for CONNACK
    while(connected == 0);

    GPIO_enableInt(CONFIG_GPIO_BUTTON_0);

    while(1){

        mq_receive(appQueue, (char*)&queueElement, sizeof(struct msgQueue), NULL);

        if(queueElement.event == APP_MQTT_PUBLISH){

            LOG_INFO("APP_MQTT_PUBLISH\r\n");

            snprintf(batteryPercentageString, sizeof batteryPercentageString, "%d\r\n", batteryPercentage);

            char* stringToSend = batteryPercentageString;

            MQTT_IF_Publish(mqttClientHandle,
                            "HybridLighting/Power/Battery",
                            stringToSend,
                            strlen(stringToSend),
                            MQTT_QOS_2);

            stringToSend = getPowerSource();

            MQTT_IF_Publish(mqttClientHandle,
                            "HybridLighting/Power",
                            stringToSend,
                            strlen(stringToSend),
                            MQTT_QOS_2);
        }
        else if(queueElement.event == APP_MQTT_CON_TOGGLE){

            LOG_TRACE("APP_MQTT_CON_TOGGLE %d\r\n", connected);


            if(connected){
                ret = MQTT_IF_Disconnect(mqttClientHandle);
                if(ret >= 0){
                    connected = 0;
                }
            }
            else{
                mqttClientHandle = MQTT_IF_Connect(mqttClientParams, mqttConnParams, MQTT_EventCallback);
                if((int)mqttClientHandle >= 0){
                    connected = 1;
                }
            }
        }
        else if(queueElement.event == APP_MQTT_DEINIT){
            break;
        }
        else if(queueElement.event == APP_BTN_HANDLER){

            struct msgQueue queueElement;

            ret = detectLongPress();
            if(ret == 0){

                LOG_TRACE("APP_BTN_HANDLER SHORT PRESS\r\n");
                queueElement.event = APP_MQTT_CON_TOGGLE;
            }
            else{

                LOG_TRACE("APP_BTN_HANDLER LONG PRESS\r\n");
                queueElement.event = APP_MQTT_DEINIT;
            }

            ret = mq_send(appQueue, (const char*)&queueElement, sizeof(struct msgQueue), 0);
            if(ret < 0){
                LOG_ERROR("msg queue send error %d", ret);
            }
        }
    }

    deinit = 1;
    if(connected){
        MQTT_IF_Disconnect(mqttClientHandle);
    }
    MQTT_IF_Deinit();

    LOG_INFO("looping the MQTT functionality of the example for demonstration purposes only\r\n");
    sleep(2);
    goto MQTT_DEMO;
}

//*****************************************************************************
//
// Close the Doxygen group.
//! @}
//
//*****************************************************************************
