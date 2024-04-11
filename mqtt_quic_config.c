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
    plugin->client->url = url.v.val_str;
    switch (qos.v.val_int) {
    case 0:
        plugin->client->qos = 0;
        break;
    case 1:
        plugin->client->qos = 1;
        break;
    case 2:
        plugin->client->qos = 2;
        break;
    default:
        break;
    }
    plugin->client->device_info->device_id        = deviceid.v.val_str;
    plugin->client->device_info->user_id          = userid.v.val_str;
    plugin->client->device_info->product_id       = productid.v.val_str;
    plugin->client->device_info->firmware_version = firmwareversion.v.val_str;
    plugin->interval        = interval.v.val_int;
    plog_notice(plugin, "config url            : %s", plugin->client->url);
    plog_notice(plugin, "config qos            : %d", plugin->client->qos);
    plog_notice(plugin, "config deviceid       : %s",
                plugin->client->device_info->device_id);
    plog_notice(plugin, "config userid         : %s",
                plugin->client->device_info->user_id);
    plog_notice(plugin, "config productid      : %s",
                plugin->client->device_info->product_id);
    plog_notice(plugin, "config firmwareversion: %s",
                plugin->client->device_info->firmware_version);
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

// 释放mqtt_quic_client_t结构体
void free_mqtt_quic_client(mqtt_quic_client_t *client) {
    if (client) {
        free(client->url);
        free_client_config(client->conf); // 假设这个函数已经定义
        free_device_info(client->device_info);
        free_topic_info(client->topic_info);
        free(client);
    }
}