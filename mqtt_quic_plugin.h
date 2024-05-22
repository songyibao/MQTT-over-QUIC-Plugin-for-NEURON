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
    nng_aio       *aio;
    char          *url;
    int            qos;
    conf_quic     *conf;
    device_info_t *device_info;
    topic_info_t  *topic_info;

} mqtt_quic_client_t;

struct neu_plugin {
    neu_plugin_common_t common;

    mqtt_quic_client_t *client;

    // 数据临时存储区，创建 client 时需要用到
    // ps：为什么不直接放在client 里面？因为 client 是在 driver_request
    // 里面从零开始创建的（为了将client的创建和销毁逻辑统一化简单化），而这些临时数据在 driver_config
    // 里面就需要被先解析好备用
    int qos;
    // url 由host和port组成，例如：mqtt-quic://host:port
    char    *url;
    char    *host;
    uint16_t port;
    char    *device_id;
    char    *user_id;
    char    *product_id;
    char    *firmware_version;

    // 插件连接状态
    bool connected;
    // 插件内简单的加法计时器, 创建 client 后才开始记时
    uint16_t base_timer_count;
    // 是否已开启保活连接
    uint8_t keep_alive_conn_count;
    // neuron 内部实现的定时器
    neu_event_timer_t *base_timer;
    neu_event_timer_t *neu_timer;
    neu_events_t      *events;
    // 实时监测请求的监测次数，默认为 0，表示无实时监测请求
    uint16_t monitor_count;
    // 数据上报间隔
    uint16_t interval;
    bool     started;
};
static neu_plugin_t *driver_open(void);

static int driver_close(neu_plugin_t *plugin);
static int driver_init(neu_plugin_t *plugin, bool load);
static int driver_uninit(neu_plugin_t *plugin);
static int driver_start(neu_plugin_t *plugin);
static int driver_stop(neu_plugin_t *plugin);
static int driver_config(neu_plugin_t *plugin, const char *config);
static int driver_request(neu_plugin_t *plugin, neu_reqresp_head_t *head, void *data);

static int driver_validate_tag(neu_plugin_t *plugin, neu_datatag_t *tag);
static int driver_group_timer(neu_plugin_t *plugin, neu_plugin_group_t *group);
static int driver_write(neu_plugin_t *plugin, void *req, neu_datatag_t *tag, neu_value_u value);
#endif // NEURON_MQTT_QUIC_PLUGIN_H
