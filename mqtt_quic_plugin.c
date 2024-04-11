//
// Created by songyibao on 24-2-29.
//

#include "mqtt_quic_plugin.h"
#include "mqtt_quic_config.h"
#include "mqtt_quic_handle.h"
#include "mqtt_quic_sdk.h"
#include "neuron.h"
#include <pthread.h>
#include <stdlib.h>
#define DESCRIPTION "Northbound MQTT over QUIC plugin bases on NanoSDK."
#define DESCRIPTION_ZH "基于 NanoSDK 的北向应用 MQTT over QUIC 插件"
static const neu_plugin_intf_funs_t plugin_intf_funs = {
    .open    = driver_open,
    .close   = driver_close,
    .init    = driver_init,
    .uninit  = driver_uninit,
    .start   = driver_start,
    .stop    = driver_stop,
    .setting = driver_config,
    .request = driver_request,

    .driver.validate_tag = driver_validate_tag,
    .driver.group_timer  = driver_group_timer,
    .driver.write_tag    = driver_write,
};

const neu_plugin_module_t neu_plugin_module = {
    .version         = NEURON_PLUGIN_VER_1_0,
    .schema          = "mqtt-quic",
    .module_name     = "MQTT over QUIC",
    .module_descr    = DESCRIPTION,
    .module_descr_zh = DESCRIPTION_ZH,
    .intf_funs       = &plugin_intf_funs,
    .kind            = NEU_PLUGIN_KIND_SYSTEM,
    .type            = NEU_NA_TYPE_APP,
    .display         = true,
    .single          = false,
};

static neu_plugin_t *driver_open(void)
{

    neu_plugin_t *plugin = calloc(1, sizeof(neu_plugin_t));

    neu_plugin_common_init(&plugin->common);
    return plugin;
}

static int driver_close(neu_plugin_t *plugin)
{
    free(plugin);

    return 0;
}

static int driver_init(neu_plugin_t *plugin, bool load)
{
    (void) load;
    plog_notice(
        plugin,
        "============================================================"
        "\ninitialize "
        "plugin============================================================\n");
    plugin->client = (mqtt_quic_client_t *) malloc(sizeof(mqtt_quic_client_t));
    plugin->client->device_info =
        (device_info_t *) malloc(sizeof(device_info_t));
    plugin->client->topic_info = (topic_info_t *) malloc(sizeof(topic_info_t));
    return 0;
}

static int driver_config(neu_plugin_t *plugin, const char *setting)
{
    plog_notice(
        plugin,
        "============================================================\nconfig "
        "plugin============================================================\n");
    int res = 0;
    if(plugin->started == true || plugin->common.link_state ==
            NEU_NODE_LINK_STATE_CONNECTED){
        plog_debug(plugin,"销毁正在执行的实例");
        client_unsubscribe(plugin);
        client_disconnect(plugin);
        client_uninit(plugin);
    }
    // 解析插件配置（包含设备信息）
    res = quic_mqtt_config_parse(plugin, setting);
    if(res!=0){
        plog_error(plugin,"config parse failed");
        return -1;
    }

    // 配置 MQTT over QUIC 连接
    res = config_client(plugin);
    if(res!=0){
            plog_error(plugin,"config client failed");
            return -1;
    }
    // 配置订阅和发布的主题
    res = config_topic_info(plugin);
    if(res!=0){
            plog_error(plugin,"config topic info failed");
            return -1;
    }
    // 实时监测次数，默认为 0 ,表示没有实时监测事件
    plugin->monitor_count = 0;
    if(plugin->started == true){
        res = driver_start(plugin);
        if(res!=0){
            plog_error(plugin,"driver start failed");
            return -1;
        }
    }
    return res;

}

static int driver_start(neu_plugin_t *plugin)
{
    plog_notice(
        plugin,
        "============================================================\nstart "
        "plugin============================================================\n");
    // 初始化 MQTT over QUIC 客户端(开启一个 nng_sock)
    int res;
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
    plugin->timer = 0;
    plugin->started = true;
    return res;
}

static int driver_stop(neu_plugin_t *plugin)
{
    plugin->monitor_count = 0;
    plugin->timer = 0;
    // 发送设备离线信息
    publishInfo(plugin,4);
    // 取消订阅主题
    client_unsubscribe(plugin);
    client_disconnect(plugin);
    plog_notice(
        plugin,
        "============================================================\nstop "
        "plugin============================================================\n");
    plugin->started = false;
    return 0;
}

static int driver_uninit(neu_plugin_t *plugin)
{
    plog_notice(
        plugin,
        "============================================================\nuninit "
        "plugin============================================================\n");


    client_uninit(plugin);
    free(plugin);
    nlog_debug("uninit success");
    return NEU_ERR_SUCCESS;
}

static int driver_request(neu_plugin_t *plugin, neu_reqresp_head_t *head,
                          void *data)
{
    plugin->timer++;
    plog_notice(
        plugin,
        "============================================================request "
        "plugin============================================================\n");
    neu_err_code_e error = NEU_ERR_SUCCESS;
    switch (head->type) {

    case NEU_REQRESP_TRANS_DATA: {
        if(plugin->common.link_state == NEU_NODE_LINK_STATE_DISCONNECTED){
            break;
        }
        if(plugin->monitor_count>0){
            plog_debug(plugin,"发布监测数据");
            plugin->monitor_count--;
            handle_trans_data(plugin,data,pMonitorTopic);
        }else{
            if(plugin->timer >= plugin->interval){
                plugin->timer = 0;
                plog_debug(plugin,"上报数据");
                handle_trans_data(plugin,data,pPropertyTopic);
            }
        }
    }
    default:
        break;
    }
    plog_notice(plugin, "Exit request function");
    return error;
}

static int driver_validate_tag(neu_plugin_t *plugin, neu_datatag_t *tag)
{
    plog_notice(plugin, "validate tag: %s", tag->name);

    return 0;
}

static int driver_group_timer(neu_plugin_t *plugin, neu_plugin_group_t *group)
{
    (void) plugin;
    (void) group;

    plog_notice(plugin, "timer....");

    return 0;
}

static int driver_write(neu_plugin_t *plugin, void *req, neu_datatag_t *tag,
                        neu_value_u value)
{
    (void) plugin;
    (void) req;
    (void) tag;
    (void) value;

    return 0;
}