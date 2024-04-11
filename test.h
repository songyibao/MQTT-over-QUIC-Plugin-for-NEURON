//
// Created by root on 4/9/24.
//

#ifndef NEURON_TEST_H
#define NEURON_TEST_H
// 设备信息配置结构体
typedef struct {
    char *deviceNum;
    char *userId;
    char *productId;
    float firmwareVersion;
    float latitude;
    float longitude;
} DeviceInfo;

// MQTT配置结构体
typedef struct {
    char *host;
    int port;
    char *userName;
    char *password;
    char secret[17];
    char *authCode;
    char *ntpServer;
} MqttConfig;

// 主题信息结构体
typedef struct {
    char *sInfoTopic;
    char *sOtaTopic;
    char *sNtpTopic;
    char *sPropertyTopic;
    char *sFunctionTopic;
    char *sPropertyOnline;
    char *sFunctionOnline;
    char *sMonitorTopic;
    char *pInfoTopic;
    char *pNtpTopic;
    char *pPropertyTopic;
    char *pFunctionTopic;
    char *pMonitorTopic;
    char *pEventTopic;
} Topics;
#endif // NEURON_TEST_H
