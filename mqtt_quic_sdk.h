//
// Created by root on 4/10/24.
//

#ifndef NEURON_MQTT_QUIC_SDK_H
#define NEURON_MQTT_QUIC_SDK_H
//
// Created by root on 4/9/24.
//
// Author: wangha <wangwei at emqx dot io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

//
// This is just a simple MQTT client demonstration application.
//
// The application has three sub-commands: `conn` `pub` and `sub`.
// The `conn` sub-command connects to the server.
// The `pub` sub-command publishes a given message to the server and then
// exits. The `sub` sub-command subscribes to the given topic filter and blocks
// waiting for incoming messages.
//
// # Example:
//
// Connect to the specific server:
// ```
// $ ./quic_client conn 'mqtt-quic://127.0.0.1:14567'
// ```
//
// Subscribe to `topic` and waiting for messages:
// ```
// $ ./quic_client sub 'mqtt-tcp://127.0.0.1:14567' topic
// ```
//
// Publish 'hello' to `topic`:
// ```
// $ ./quic_client pub 'mqtt-tcp://127.0.0.1:14567' topic hello
// ```
//

#include "nng/mqtt/mqtt_client.h"
#include "nng/mqtt/mqtt_quic.h"
#include "nng/nng.h"
#include "nng/supplemental/util/platform.h"

#include <msquic/msquic.h>

#include "mqtt_quic_plugin.h"
#include <stdio.h>
#include <stdlib.h>
#define sInfoTopic 0
#define sOtaTopic 1
#define sNtpTopic 2
#define sPropertyTopic 3
#define sFunctionTopic 4
#define sPropertyOnline 5
#define sFunctionOnline 6
#define sMonitorTopic 7
#define pInfoTopic 0
#define pNtpTopic 1
#define pPropertyTopic 2
#define pFunctionTopic 3
#define pMonitorTopic 4
#define pEventTopic 5
int config_client(neu_plugin_t *plugin);
int client_init(neu_plugin_t *plugin);
int client_connect(neu_plugin_t *plugin);

int client_subscribe(neu_plugin_t *plugin);
int client_unsubscribe(neu_plugin_t *plugin);
int client_disconnect(neu_plugin_t *plugin);
int client_uninit(neu_plugin_t *plugin);
int client_publish(neu_plugin_t *plugin, const char *topic,
                   const char *data);
// 发布设备信息，status -->  1-未激活，2-禁用，3-在线，4-离线
void publishInfo(neu_plugin_t *plugin,uint8_t status);
// 发布设备监测属性 {"id":"temperature","value":25.5,"remark":1620000000}
void publishProperty(neu_plugin_t *plugin, const char *json_str);
void publish_monitor(neu_plugin_t *plugin, const char *json_str);
int free_client_config(conf_quic *conf);
#endif // NEURON_MQTT_QUIC_SDK_H
