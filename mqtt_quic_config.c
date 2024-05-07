/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2022 EMQ Technologies Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 **/

#include "mqtt_quic_config.h"
#include "mqtt_quic_plugin.h"
#include "mqtt_quic_sdk.h"
#include "neuron.h"
#include "json/json.h"
#include "json/neu_json_param.h"

#define MB 1000000
char *concatenate(const char *prefix, const char *suffix)
{
    char *result = malloc(strlen(prefix) + strlen(suffix) +
                          1); // Allocate memory for the concatenated string
    if (result == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    strcpy(result, prefix); // Copy prefix to result
    strcat(result, suffix); // Concatenate suffix to result
    return result;
}
int quic_mqtt_config_parse(neu_plugin_t *plugin, const char *setting)
{
    int   ret       = 0;
    char *err_param = NULL;

    neu_json_elem_t qos       = { .name = "qos", .t = NEU_JSON_INT };
    neu_json_elem_t url       = { .name = "url", .t = NEU_JSON_STR };
    neu_json_elem_t deviceid  = { .name = "deviceid", .t = NEU_JSON_STR };
    neu_json_elem_t userid    = { .name = "userid", .t = NEU_JSON_STR };
    neu_json_elem_t productid = { .name = "productid", .t = NEU_JSON_STR };
    neu_json_elem_t firmwareversion = { .name = "firmwareversion",
                                        .t    = NEU_JSON_STR };
    neu_json_elem_t interval = { .name = "interval", .t = NEU_JSON_INT };
    ret = neu_parse_param(setting, &err_param, 7, &qos, &url, &deviceid,
                          &userid, &productid, &firmwareversion,&interval);
    if (ret < 0) {
        plog_error(plugin, "parse setting failed: %s", err_param);
        goto error;
    }
    // host, required
    if (0 == strlen(url.v.val_str)) {
        plog_error(plugin, "setting invalid url: `%s`", url.v.val_str);
        goto error;
    }

    // port, required
    if (qos.v.val_int < 0 || qos.v.val_int > 2) {
        plog_error(plugin, "setting invalid port: %" PRIi64, qos.v.val_int);
        goto error;
    }
    plugin->url = url.v.val_str;
    switch (qos.v.val_int) {
    case 0:
        plugin->qos = 0;
        break;
    case 1:
        plugin->qos = 1;
        break;
    case 2:
        plugin->qos = 2;
        break;
    default:
        break;
    }
    plugin->device_id = deviceid.v.val_str;;
    plugin->user_id = userid.v.val_str;
    plugin->product_id = productid.v.val_str;
    plugin->firmware_version = firmwareversion.v.val_str;
    plugin->interval        = interval.v.val_int;
    plog_notice(plugin, "config url            : %s", plugin->url);
    plog_notice(plugin, "config qos            : %d", plugin->qos);
    plog_notice(plugin, "config deviceid       : %s",
                plugin->device_id);
    plog_notice(plugin, "config userid         : %s",
                plugin->user_id);
    plog_notice(plugin, "config productid      : %s",
                plugin->product_id);
    plog_notice(plugin, "config firmwareversion: %s",
                plugin->firmware_version);
    plog_notice(plugin, "config interval       : %d", plugin->interval);

    return 0;

error:
    return -1;
}
int config_topic_info(neu_plugin_t *plugin)
{
    int           qos         = plugin->client->qos;
    topic_info_t *topic_info  = (topic_info_t *) malloc(sizeof(topic_info_t));
    topic_info->s_topic_count = 8;
    topic_info->p_topic_count = 6;
    int count       = topic_info->s_topic_count + topic_info->p_topic_count;
    topic_info->qos = (int *) malloc(sizeof(int) * count);
    for (int i = 0; i < count; i++) {
        topic_info->qos[i] = qos;
    }
    device_info_t *device_info = plugin->client->device_info;
    char *prefix = (char *) malloc(
        (1 + 2 + strlen(device_info->product_id) +
         strlen(device_info->device_id)) *
        sizeof(char));
    sprintf(prefix, "/%s/%s", device_info->product_id,device_info->device_id);
    char **s_topics =
        (char **) malloc(sizeof(char *) * topic_info->s_topic_count);
    s_topics[0]          = concatenate(prefix, "/info/get");
    s_topics[1]          = concatenate(prefix, "/ota/get");
    s_topics[2]          = concatenate(prefix, "/ntp/get");
    s_topics[3]          = concatenate(prefix, "/property/get");
    s_topics[4]          = concatenate(prefix, "/function/get");
    s_topics[5]          = concatenate(prefix, "/property-online/get");
    s_topics[6]          = concatenate(prefix, "/function-online/get");
    s_topics[7]          = concatenate(prefix, "/monitor/get");
    topic_info->s_topics = s_topics;

    char **p_topics =
        (char **) malloc(sizeof(char *) * topic_info->p_topic_count);
    p_topics[0]          = concatenate(prefix, "/info/post");
    p_topics[1]          = concatenate(prefix, "/ntp/post");
    p_topics[2]          = concatenate(prefix, "/property/post");
    p_topics[3]          = concatenate(prefix, "/function/post");
    p_topics[4]          = concatenate(prefix, "/monitor/post");
    p_topics[5]          = concatenate(prefix, "/event/post");
    topic_info->p_topics = p_topics;

    for (int i = 0; i < topic_info->s_topic_count; i++) {
        plog_notice(plugin, "config s_topic[%d] : %s", i,
                    topic_info->s_topics[i]);
    }
    for (int j = 0; j < topic_info->p_topic_count; j++) {
        plog_notice(plugin, "config p_topic[%d] : %s", j,
                    topic_info->p_topics[j]);
    }

    plugin->client->topic_info = topic_info;
    return 0;
}
int config_device_info(neu_plugin_t *plugin){
    plugin->client->qos = plugin->qos;
    plugin->client->url = (char *)malloc(strlen(plugin->url)+1);
    strcpy(plugin->client->url,plugin->url);
    plugin->client->device_info->device_id = (char *)malloc(strlen(plugin->device_id)+1);
    strcpy(plugin->client->device_info->device_id,plugin->device_id);
    plugin->client->device_info->user_id = (char *)malloc(strlen(plugin->user_id)+1);
    strcpy(plugin->client->device_info->user_id,plugin->user_id);
    plugin->client->device_info->product_id = (char *)malloc(strlen(plugin->product_id)+1);
    strcpy(plugin->client->device_info->product_id,plugin->product_id);
    plugin->client->device_info->firmware_version = (char *)malloc(strlen(plugin->firmware_version)+1);
    strcpy(plugin->client->device_info->firmware_version,plugin->firmware_version);

    return 0;
}
// 释放device_info_t结构体
void free_device_info(device_info_t *info) {
    if (info) {
        free(info->device_id);
        free(info->user_id);
        free(info->product_id);
        free(info->firmware_version);
        free(info);
    }
}

// 释放topic_info_t结构体
void free_topic_info(topic_info_t *info) {
    if (info) {
        for (int i = 0; i < info->s_topic_count; ++i) {
            free(info->s_topics[i]);
        }
        for (int i = 0; i < info->p_topic_count; ++i) {
            free(info->p_topics[i]);
        }
        free(info->s_topics);
        free(info->p_topics);
        free(info->qos);
        free(info);
    }
}

// 初始化 client 结构体
void init_mqtt_quic_client(neu_plugin_t *plugin) {
    if(plugin->client!=NULL){
        stop_and_free_client(plugin);
    }
    plugin->client = (mqtt_quic_client_t *) malloc(sizeof(mqtt_quic_client_t));
    plugin->client->device_info =
            (device_info_t *) malloc(sizeof(device_info_t));
    plugin->client->topic_info = (topic_info_t *) malloc(sizeof(topic_info_t));
}

// 发起 client 连接
int start_mqtt_quic_client(neu_plugin_t *plugin) {
    if(plugin->client == NULL){
        plog_error(plugin,"plugin->client is NULL");
        return -1;
    }
    // 初始化 MQTT over QUIC 客户端(开启一个 nng_sock)
    int res=0;
    res = client_init(plugin);
    if(res!=0){
        plog_error(plugin,"client init failed");
        return -1;
    }
    // 用创建好的 client 发起连接
    res = client_connect(plugin);
    if(res!=0){
        plog_error(plugin,"client connect failed");
        return -1;
    }
    // 订阅主题
    res = client_subscribe(plugin);
    if(res!=0){
        plog_error(plugin,"client subscribe failed");
        return -1;
    }
    // 发布设备在线信息
    publishInfo(plugin,3);
    return res;
}
int stop_mqtt_quic_client(neu_plugin_t *plugin) {
    int res=0;
    plugin->monitor_count = 0;
    plugin->timer = 0;
    // 发送设备离线信息
    if(plugin->common.link_state == NEU_NODE_LINK_STATE_CONNECTED){
        publishInfo(plugin,4);
    }

    // 取消订阅主题
    res = client_unsubscribe(plugin);
    if(res!=0){
        plog_error(plugin,"publish device offline info failed");
        return -1;
    }
    res = client_disconnect(plugin);
    if(res!=0){
        plog_error(plugin,"client disconnect failed");
        return -1;
    }
    return res;
}

// 配置 client 参数
int config_mqtt_quic_client(neu_plugin_t *plugin) {
    if(plugin->client == NULL){
        plog_error(plugin,"plugin->client is NULL");
        return -1;
    }
    int res=0;
    // 配置 MQTT over QUIC 连接
    res = config_client(plugin);
    if(res!=0){
        plog_error(plugin,"config client failed");
        return res;
    }
    // 配置设备信息
    res = config_device_info(plugin);
    if(res!=0){
        plog_error(plugin,"config device info failed");
        return res;
    }
    // 配置订阅和发布的主题
    res = config_topic_info(plugin);
    if(res!=0){
        plog_error(plugin,"config topic info failed");
        return res;
    }
    // 实时监测次数，默认为 0 ,表示没有实时监测事件
    plugin->monitor_count = 0;
    return res;
}

// 释放 client 结构体
void free_mqtt_quic_client(neu_plugin_t *plugin) {
    mqtt_quic_client_t *client = plugin->client;
    if (client!=NULL) {
        free(client->url);
        free_client_config(client->conf); // 假设这个函数已经定义
        free_device_info(client->device_info);
        free_topic_info(client->topic_info);
        free(client);
    }
    plugin->client = NULL;
}

int create_and_config_and_start_client(neu_plugin_t *plugin) {
    int res=0;
    if(plugin->client != NULL){
        res = stop_and_free_client(plugin);
        if(res == -1){
            plog_error(plugin,"stop and free client failed");
            return -1;
        }
    }
    init_mqtt_quic_client(plugin);
    res = config_mqtt_quic_client(plugin);
    if(res!=0){
        plog_error(plugin,"config mqtt quic client failed");
        free_mqtt_quic_client(plugin);
        return -1;
    }
    res = start_mqtt_quic_client(plugin);
    if(res!=0){
        plog_error(plugin,"start mqtt quic client failed");
        free_mqtt_quic_client(plugin);
        return -1;
    }
    return res;
}

int stop_and_free_client(neu_plugin_t *plugin) {
    if(plugin->client == NULL){
        return 0;
    }
    int res=0;
    res = stop_mqtt_quic_client(plugin);
    if(res!=0){
        plog_error(plugin,"stop mqtt quic client failed");
        free_mqtt_quic_client(plugin);
        return -1;
    }
    free_mqtt_quic_client(plugin);
    plugin->common.link_state = NEU_NODE_LINK_STATE_DISCONNECTED;
    return res;
}
int check_plugin_status_callback(void *data){
    neu_plugin_t  *plugin = (neu_plugin_t *)data;
    if(plugin->client == NULL){
        plugin->common.link_state = NEU_NODE_LINK_STATE_DISCONNECTED;
    }else{
        plugin->common.link_state = NEU_NODE_LINK_STATE_CONNECTED;
    }
    return 0;
}
void add_connection_status_checker(neu_plugin_t *plugin)
{
    neu_event_timer_param_t param = { .second      = 3,
            .millisecond = 0,
            .cb          = check_plugin_status_callback,
            .usr_data    = (void *) plugin,
            .type        = NEU_EVENT_TIMER_NOBLOCK };

    plugin->neu_timer = neu_event_add_timer(plugin->events, param);
}