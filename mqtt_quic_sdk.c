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

#include "mqtt_quic_config.h"
#include "mqtt_quic_plugin.h"
#include "mqtt_quic_sdk.h"
#include <cjson/cJSON.h>
#include <msquic.h>
#include <stdio.h>
#include <stdlib.h>

void print_property(property *prop)
{
    if (prop == NULL) {
        return;
    }

    // printf("%d \n", prop->id);

    uint8_t type    = prop->data.p_type;
    uint8_t prop_id = prop->id;
    switch (type) {
    case U8:
        printf("id: %d, value: %d (U8)\n", prop_id, prop->data.p_value.u8);
        break;
    case U16:
        printf("id: %d, value: %d (U16)\n", prop_id, prop->data.p_value.u16);
        break;
    case U32:
        printf("id: %d, value: %u (U32)\n", prop_id, prop->data.p_value.u32);
        break;
    case VARINT:
        printf("id: %d, value: %d (VARINT)\n", prop_id, prop->data.p_value.varint);
        break;
    case BINARY:
        printf("id: %d, value pointer: %p (BINARY)\n", prop_id, prop->data.p_value.binary.buf);
        break;
    case STR:
        printf("id: %d, value: %.*s (STR)\n", prop_id, prop->data.p_value.str.length,
               (const char *) prop->data.p_value.str.buf);
        break;
    case STR_PAIR:
        printf("id: %d, value: '%.*s -> %.*s' (STR_PAIR)\n", prop_id,
               prop->data.p_value.strpair.key.length, prop->data.p_value.strpair.key.buf,
               prop->data.p_value.strpair.value.length, prop->data.p_value.strpair.value.buf);
        break;

    default:
        break;
    }
}

static nng_msg *compose_connect()
{
    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);
    nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_CONNECT);
    // test property
    property *p  = mqtt_property_alloc();
    property *p1 = mqtt_property_set_value_u32(MAXIMUM_PACKET_SIZE, 120);
    property *p2 = mqtt_property_set_value_u32(SESSION_EXPIRY_INTERVAL, 120);
    property *p3 = mqtt_property_set_value_u16(RECEIVE_MAXIMUM, 120);
    property *p4 = mqtt_property_set_value_u32(MAXIMUM_PACKET_SIZE, 120);
    property *p5 = mqtt_property_set_value_u16(TOPIC_ALIAS_MAXIMUM, 120);
    mqtt_property_append(p, p1);
    mqtt_property_append(p, p2);
    mqtt_property_append(p, p3);
    mqtt_property_append(p, p4);
    mqtt_property_append(p, p5);
    nng_mqtt_msg_set_connect_property(msg, p);

    nng_mqtt_msg_set_connect_proto_version(msg, MQTT_PROTOCOL_VERSION_v5);
    nng_mqtt_msg_set_connect_keep_alive(msg, 30);
    nng_mqtt_msg_set_connect_clean_session(msg, true);

    return msg;
}
static nng_msg *compose_keep_alive()
{
    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);
    nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PINGREQ);

    nng_mqtt_msg_set_connect_proto_version(msg, MQTT_PROTOCOL_VERSION_v5);

    return msg;
}
// 修改后的函数原型，支持多个主题订阅
static nng_msg *compose_subscribe_multiple(int *qos, char **topics, int topic_count)
{
    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);
    nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_SUBSCRIBE);

    // 根据传入的主题数量动态分配订阅数组
    nng_mqtt_topic_qos *subscriptions = malloc(sizeof(nng_mqtt_topic_qos) * topic_count);

    for (int i = 0; i < topic_count; ++i) {
        subscriptions[i].qos          = qos[i];
        subscriptions[i].topic.buf    = (uint8_t *) topics[i];
        subscriptions[i].topic.length = strlen(topics[i]);
    }

    // 设置订阅主题
    nng_mqtt_msg_set_subscribe_topics(msg, subscriptions, topic_count);

    // 构造订阅属性
    property *p  = mqtt_property_alloc();
    property *p1 = mqtt_property_set_value_varint(SUBSCRIPTION_IDENTIFIER, 120);
    mqtt_property_append(p, p1);
    nng_mqtt_msg_set_subscribe_property(msg, p);

    // 清理分配的内存
    free(subscriptions);

    return msg;
}
static nng_msg *compose_unsubscribe_multiple(char **topics, int topic_count)
{
    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);
    nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_UNSUBSCRIBE);

    // 根据传入的主题数量动态分配取消订阅数组
    nng_mqtt_topic *unsubscriptions = malloc(sizeof(nng_mqtt_topic) * topic_count);

    for (int i = 0; i < topic_count; ++i) {
        unsubscriptions[i].buf    = (uint8_t *) topics[i];
        unsubscriptions[i].length = strlen(topics[i]);
    }

    // 设置取消订阅的主题
    nng_mqtt_msg_set_unsubscribe_topics(msg, unsubscriptions, topic_count);

    // 构造取消订阅属性（如果需要）
    // ... 在这里添加任何特定于取消订阅的属性 ...

    // 清理分配的内存
    free(unsubscriptions);

    return msg;
}

static nng_msg *compose_subscribe(int qos, char *topic)
{
    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);
    nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_SUBSCRIBE);

    nng_mqtt_topic_qos subscriptions[] = {
        { .qos = qos, .topic = { .buf = (uint8_t *) topic, .length = strlen(topic) } },
    };

    int count = sizeof(subscriptions) / sizeof(nng_mqtt_topic_qos);

    nng_mqtt_msg_set_subscribe_topics(msg, subscriptions, count);
    property *p  = mqtt_property_alloc();
    property *p1 = mqtt_property_set_value_varint(SUBSCRIPTION_IDENTIFIER, 120);
    mqtt_property_append(p, p1);
    nng_mqtt_msg_set_subscribe_property(msg, p);
    return msg;
}

static nng_msg *compose_publish(int qos, char *topic, char *payload)
{
    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);
    nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PUBLISH);
    property *plist = mqtt_property_alloc();
    property *p1    = mqtt_property_set_value_u8(PAYLOAD_FORMAT_INDICATOR, 1);
    mqtt_property_append(plist, p1);
    property *p2 = mqtt_property_set_value_u16(TOPIC_ALIAS, 10);
    mqtt_property_append(plist, p2);
    property *p3 = mqtt_property_set_value_u32(MESSAGE_EXPIRY_INTERVAL, 10);
    mqtt_property_append(plist, p3);
    property *p4 = mqtt_property_set_value_str(RESPONSE_TOPIC, "aaaaaa", strlen("aaaaaa"), true);
    mqtt_property_append(plist, p4);
    property *p5 = mqtt_property_set_value_binary(CORRELATION_DATA, (uint8_t *) "aaaaaa",
                                                  strlen("aaaaaa"), true);
    mqtt_property_append(plist, p5);
    property *p6 = mqtt_property_set_value_strpair(USER_PROPERTY, "aaaaaa", strlen("aaaaaa"),
                                                   "aaaaaa", strlen("aaaaaa"), true);
    mqtt_property_append(plist, p6);
    property *p7 = mqtt_property_set_value_str(CONTENT_TYPE, "aaaaaa", strlen("aaaaaa"), true);
    mqtt_property_append(plist, p7);

    nng_mqtt_msg_set_publish_property(msg, plist);

    nng_mqtt_msg_set_publish_dup(msg, 0);
    nng_mqtt_msg_set_publish_qos(msg, qos);
    nng_mqtt_msg_set_publish_retain(msg, 0);
    nng_mqtt_msg_set_publish_topic(msg, topic);
    nng_mqtt_msg_set_publish_payload(msg, (uint8_t *) payload, strlen(payload));

    return msg;
}

static int connect_cb(void *rmsg, void *arg)

{
    neu_plugin_t *plugin = arg;
    plog_debug(plugin, "[Connected]");
    return 0;
}
static void aio_connect_cb(void *arg)
{

    neu_plugin_t *plugin = (neu_plugin_t *) arg;
    int           res    = nng_aio_result(plugin->client->aio);
    plog_debug(plugin, "异步回调返回结果：%d", res);
    //    plugin->check_connect_status_waiting_flag = false;
    //    plugin->common.link_state = NEU_NODE_LINK_STATE_CONNECTED;
    //    printf("[Aio Connected][%s]...\n", (char *) arg);
}
static int disconnect_cb(void *rmsg, void *arg)
{
    neu_plugin_t *plugin = arg;
    plog_debug(plugin, "[Disconnected]");
    plugin->common.link_state = NEU_NODE_LINK_STATE_DISCONNECTED;
    //    free_client_config(plugin->client->conf);
    return 0;
}

static int msg_send_cb(void *rmsg, void *arg)
{
    neu_plugin_t *plugin = arg;
    plog_debug(plugin, "[Msg Sent]");
    return 0;
}

static int msg_recv_cb(void *rmsg, void *arg)

{
    neu_plugin_t *plugin = arg;
    plog_debug(plugin, "[Msg Arrived]");
    nng_msg *msg = rmsg;
    //    if(nng_mqtt_msg_get_packet_type(rmsg) == NNG_MQTT_PINGRESP){
    //        plugin->common.link_state = NEU_NODE_LINK_STATE_CONNECTED;
    //    }
    uint32_t topicsz, payloadsz;

    char *topic   = (char *) nng_mqtt_msg_get_publish_topic(msg, &topicsz);
    char *payload = (char *) nng_mqtt_msg_get_publish_payload(msg, &payloadsz);

    plog_debug(plugin, "topic   => %.*s , payload => %.*s", topicsz, topic, payloadsz, payload);

    property *pl = nng_mqtt_msg_get_publish_property(msg);
    if (pl != NULL) {
        mqtt_property_foreach(pl, print_property);
    }
    char **s_topics = plugin->client->topic_info->s_topics;
    plog_debug(plugin, "topic:%s", topic);
    plog_debug(plugin, "s_topics:%s", s_topics[sMonitorTopic]);
    if (strncmp(topic, s_topics[sMonitorTopic], topicsz) == 0) {
        plog_info(plugin, "收到实时监测监测指令");
        cJSON *res            = cJSON_Parse(payload);
        plugin->monitor_count = cJSON_GetObjectItemCaseSensitive(res, "count")->valueint;
        cJSON_free(res);
    }
    return 0;
}

static int sqlite_config(nng_socket *sock, uint8_t proto_ver)
{
#if defined(NNG_SUPP_SQLITE)
    int rv;
    // create sqlite option
    nng_mqtt_sqlite_option *sqlite;
    if ((rv = nng_mqtt_alloc_sqlite_opt(&sqlite)) != 0) {
        fatal("nng_mqtt_alloc_sqlite_opt", rv);
    }
    // set sqlite option
    nng_mqtt_set_sqlite_enable(sqlite, true);
    nng_mqtt_set_sqlite_flush_threshold(sqlite, 10);
    nng_mqtt_set_sqlite_max_rows(sqlite, 20);
    nng_mqtt_set_sqlite_db_dir(sqlite, "/tmp/nanomq");

    // init sqlite db
    nng_mqtt_sqlite_db_init(sqlite, "mqtt_quic_client.db", proto_ver);

    // set sqlite option pointer to socket
    return nng_socket_set_ptr(*sock, NNG_OPT_MQTT_SQLITE, sqlite);
#else
    return (0);
#endif
}

// flag 为无效参数, 内部会释放 msg
int sendmsg_func(nng_socket sock, nng_msg *msg, int flag, void *arg)
{
    neu_plugin_t *plugin = (neu_plugin_t *) arg;
    if (plugin->connected == false) {
        plog_error(plugin, "连接中断, 停止发送消息");
        stop_and_free_client(plugin);
        return -1;
    }
    int res = 0;
    res     = nng_sendmsg(sock, msg, NNG_FLAG_NONBLOCK);
    return res;
    //    for (int i = 0; i < 3; i++) {
    //        nlog_debug("尝试发送消息，第 %d 轮循环", i);
    //        res = nng_sendmsg(sock, msg, NNG_FLAG_NONBLOCK);
    //        nlog_debug("第 %d 次尝试完成", i);
    //        if (res == 0) {
    //            nlog_debug("发送消息成功，第 %d 轮循环", i);
    //            return 0;
    //        } else if (res == NNG_EAGAIN) {
    //            //
    //            例如，如果由于对等方消耗消息太慢而存在背压，或者不存在对等方，那么可能会返回NNG_EAGAIN
    //            // 如果没有NNG_FLAG_NONBLOCK标志，则nng_sendmsg将一直阻塞
    //            nlog_debug("发送消息失败，等待 1 秒后重试");
    //        }
    //        nng_msleep(1000);
    //    }
    //    return -1;
}
int free_conf_tls(conf_tls *tls)
{
    if (tls) {
        if (tls->url) {
            free(tls->url);
        }
        if (tls->cafile) {
            free(tls->cafile);
        }
        if (tls->certfile) {
            free(tls->certfile);
        }
        if (tls->keyfile) {
            free(tls->keyfile);
        }
        if (tls->ca) {
            free(tls->ca);
        }
        if (tls->cert) {
            free(tls->cert);
        }
        if (tls->key) {
            free(tls->key);
        }
        if (tls->key_password) {
            free(tls->key_password);
        }
    }
    return 0;
}
// 配置 url qos tls_conf conn_conf
int config_client(neu_plugin_t *plugin)
{

    mqtt_quic_client_t *client = plugin->client;
    client->qos                = plugin->qos;
    client->url                = (char *) malloc(strlen(plugin->url) + 1);
    strcpy(client->url, plugin->url);
    conf_quic *conf     = (conf_quic *) malloc(sizeof(conf_quic));
    conf_tls   tls_conf = {
          .enable       = false,
          .url          = NULL,
          .cafile       = NULL,
          .certfile     = NULL,
          .keyfile      = NULL,
          .ca           = NULL,
          .cert         = NULL,
          .key          = NULL,
          .key_password = NULL,
          .verify_peer  = true,
          .set_fail     = true,
    };
    conf->tls              = tls_conf;
    conf->multi_stream     = false;
    conf->qos_first        = false;
    conf->qkeepalive       = 30;
    conf->qconnect_timeout = 60;
    conf->qdiscon_timeout  = 30;
    conf->qidle_timeout    = 30;
    client->conf           = conf;
    return 0;
}

int free_client_config(conf_quic *conf)
{
    if (conf) {
        free_conf_tls(&(conf->tls));
        free(conf);
    }
    return 0;
}
int client_open(neu_plugin_t *plugin)
{

    mqtt_quic_client_t *client = plugin->client;
    int                 rv     = 0;
    if ((rv = nng_mqttv5_quic_client_open_conf(&client->sock, client->url, client->conf)) != 0) {
        rv = -1;
        printf("error in quic client init.\n");
        return rv;
    } else {
        plog_debug(plugin, "开启 MQTT 客户端连接成功");
    }
    return rv;
}
int client_connect(neu_plugin_t *plugin)
{
    mqtt_quic_client_t *client = plugin->client;
    int                 rv, q;
    nng_msg            *msg;

    // 设置回调函数
    if (0 != nng_mqtt_quic_set_connect_cb(&client->sock, connect_cb, plugin) ||
        0 != nng_mqtt_quic_set_disconnect_cb(&client->sock, disconnect_cb, plugin) ||
        0 != nng_mqtt_quic_set_msg_recv_cb(&client->sock, msg_recv_cb, plugin) ||
        0 != nng_mqtt_quic_set_msg_send_cb(&client->sock, msg_send_cb, plugin)) {
        printf("error in quic client cb set.\n");
        nng_close(client->sock);
        return 1;
    }

    // 发送MQTT连接消息
    msg = compose_connect();
    //    // 设置发送超时
    //    int timeout_ms = 2000; // 设置超时为2000毫秒
    //    if (0 != nng_socket_set_ms(client->sock, NNG_OPT_SENDTIMEO, timeout_ms)) {
    //        printf("error in setting send timeout.\n");
    //        nng_close(client->sock);
    //        return -1;
    //    }
    plog_debug(plugin, "发送MQTT连接消息");
    int send_result = sendmsg_func(client->sock, msg, NNG_FLAG_ALLOC, plugin);
    if (send_result != 0) {
        printf("error in sending message: %d\n", send_result);
        plog_debug(plugin, "发送MQTT连接消息超时");
        client_disconnect(plugin);
        return -1;
    }
    plog_debug(plugin, "发送MQTT连接消息成功");

    return 0; // 成功
}

int client_disconnect(neu_plugin_t *plugin)
{
    mqtt_quic_client_t *client = plugin->client;
    int                 res    = 0;

    // 检查客户端是否已初始化
    if (client == NULL) {
        printf("Client not initialized.\n");
        return -1;
    }

    // 可以在这里添加更多的清理步骤，例如发送断开连接的MQTT消息等。

    // 关闭QUIC客户端连接
    res = nng_close(client->sock);
    if (res != 0) {
        printf("Error closing the client: %d\n", res);
        return res;
    }

    // 在这里可以添加释放其它已分配资源的代码

    printf("Client disconnected successfully.\n");
    return res; // 成功断开连接
}
int client_subscribe(neu_plugin_t *plugin)
{
    mqtt_quic_client_t *client = plugin->client;
    int                 q      = client->qos;

    plog_debug(plugin, "client->qos:%d", q);
    if (q < 0 || q > 2) {
        printf("Qos should be in range(0~2).\n");
        return 1;
    }
    topic_info_t *topic_info = client->topic_info;
    nng_msg      *msg        = compose_subscribe_multiple(topic_info->qos, topic_info->s_topics,
                                                          topic_info->s_topic_count);

    sendmsg_func(client->sock, msg, NNG_FLAG_ALLOC, plugin);

    return 0; // 成功
}
int client_unsubscribe(neu_plugin_t *plugin)
{
    mqtt_quic_client_t *client = plugin->client;
    int                 q      = client->qos;

    // 检查QoS值是否在有效范围内
    if (q < 0 || q > 2) {
        printf("Qos should be in range(0~2).\n");
        return -1;
    }

    topic_info_t *topic_info = client->topic_info;

    // 假设compose_unsubscribe_multiple是一个存在的函数，用于创建取消订阅的消息
    nng_msg *msg = compose_unsubscribe_multiple(topic_info->s_topics, topic_info->s_topic_count);

    // 发送取消订阅的消息
    sendmsg_func(client->sock, msg, NNG_FLAG_ALLOC, plugin);

    return 0; // 成功
}

int client_publish(neu_plugin_t *plugin, const char *topic, const char *data)
{
    mqtt_quic_client_t *client = plugin->client;
    int                 q      = client->qos;
    if (q < 0 || q > 2) {
        printf("Qos should be in range(0~2).\n");
        return 1;
    }

    nng_msg *msg = compose_publish(q, (char *) topic, (char *) data);
    int      res = sendmsg_func(client->sock, msg, NNG_FLAG_ALLOC, plugin);
    if (res != 0) {
        plog_error(plugin, "连接中断");
        plugin->common.link_state = NEU_NODE_LINK_STATE_DISCONNECTED;
        stop_and_free_client(plugin);
        return res;
    }
    return 0; // 成功
}

void publishInfo(neu_plugin_t *plugin, uint8_t status)
{
    device_info_t *device_info = plugin->client->device_info;
    cJSON         *root        = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "rssi", -50);
    cJSON_AddStringToObject(root, "firmwareVersion", device_info->firmware_version);
    cJSON_AddNumberToObject(root, "status", status);
    cJSON_AddStringToObject(root, "userId", device_info->user_id);
    cJSON_AddNumberToObject(root, "longitude", 0); // 经度 可选
    cJSON_AddNumberToObject(root, "latitude", 0);  // 纬度 可选

    // 设备摘要,可选（自定义配置信息）
    cJSON *summary = cJSON_CreateObject();
    cJSON_AddStringToObject(summary, "name", "monitor1");
    cJSON_AddStringToObject(summary, "chip", "esp8266");
    cJSON_AddStringToObject(summary, "author", "songyibao");
    cJSON_AddNumberToObject(summary, "version", 1.6);
    cJSON_AddStringToObject(summary, "create", "2023-10-20");

    cJSON_AddItemToObject(root, "summary", summary);

    char *output = cJSON_Print(root);

    plog_debug(plugin, "发布设备监测信息：%s", output);

    // 将 output 发布到 mqttClient
    // mqttClient.publish(pInfoTopic.c_str(), output);
    topic_info_t *topicInfo = plugin->client->topic_info;
    client_publish(plugin, topicInfo->p_topics[pInfoTopic], output);
    // 释放内存
    cJSON_Delete(root);
    free(output);
}
// 发布设备监测属性 {"id":"temperature","value":25.5,"remark":1620000000}
void publishProperty(neu_plugin_t *plugin, const char *json_str)
{
    client_publish(plugin, plugin->client->topic_info->p_topics[pPropertyTopic], json_str);
}
void publish_monitor(neu_plugin_t *plugin, const char *json_str)
{
    client_publish(plugin, plugin->client->topic_info->p_topics[pMonitorTopic], json_str);
}

// int main(int argc, char **argv)
//{
//     int rc;
//
//     if (argc < 3) {
//         goto error;
//     }
//     if (0 == strncmp(argv[1], "conn", 4) && argc == 3) {
//         client(CONN, argv[2], NULL, NULL, NULL);
//     } else if (0 == strncmp(argv[1], "sub", 3) && argc == 5) {
//         client(SUB, argv[2], argv[3], argv[4], NULL);
//     } else if (0 == strncmp(argv[1], "pub", 3) && argc == 6) {
//         client(PUB, argv[2], argv[3], argv[4], argv[5]);
//     } else {
//         goto error;
//     }
//
//     return 0;
//
// error:
//
//     printf_helper(argv[0]);
//     return 0;
// }