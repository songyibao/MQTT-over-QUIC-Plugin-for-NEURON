//
// Created by root on 4/9/24.
//

#ifndef NEURON_MQTT_QUIC_PLUGIN_H
#define NEURON_MQTT_QUIC_PLUGIN_H
#include "neuron.h"
#include "nng/mqtt/mqtt_quic.h"
#include "nng/nng.h"

// 设备信息配置结构体
typedef struct device_info {
    char *device_id;
    char *user_id;
    char *product_id;
    char *firmware_version;
} device_info_t;



// 主题信息结构体 14个主题
typedef struct topic_info {
    char **s_topics;
    char **p_topics;
    int   *qos;
    int    s_topic_count;
    int    p_topic_count;

} topic_info_t;
typedef struct mqtt_quic_client {
    nng_socket     sock;
    char          *url;
    int            qos;
    conf_quic     *conf;
    device_info_t *device_info;
    topic_info_t  *topic_info;

} mqtt_quic_client_t;

struct neu_plugin {
    neu_plugin_common_t common;
    mqtt_quic_client_t *client;
    // 插件内计时器
    uint16_t timer;
    // 实时监测请求的监测次数，默认为 0，表示无实时监测请求
    uint16_t monitor_count;
    // 数据上报间隔
    uint16_t interval;
    bool started;
};
static neu_plugin_t *driver_open(void);

static int driver_close(neu_plugin_t *plugin);
static int driver_init(neu_plugin_t *plugin, bool load);
static int driver_uninit(neu_plugin_t *plugin);
static int driver_start(neu_plugin_t *plugin);
static int driver_stop(neu_plugin_t *plugin);
static int driver_config(neu_plugin_t *plugin, const char *config);
static int driver_request(neu_plugin_t *plugin, neu_reqresp_head_t *head,
                          void *data);

static int driver_validate_tag(neu_plugin_t *plugin, neu_datatag_t *tag);
static int driver_group_timer(neu_plugin_t *plugin, neu_plugin_group_t *group);
static int driver_write(neu_plugin_t *plugin, void *req, neu_datatag_t *tag,
                        neu_value_u value);
#endif // NEURON_MQTT_QUIC_PLUGIN_H
