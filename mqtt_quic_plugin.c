//
// Created by songyibao on 24-2-29.
//

#include "mqtt_quic_plugin.h"
#include "mqtt_quic_config.h"
#include "mqtt_quic_handle.h"
#include "mqtt_quic_sdk.h"
#include "neuron.h"
#include "quic_conn_status_detect/detector.h"
#include <pthread.h>
#include <stdlib.h>
#define DESCRIPTION "MQTT plugin based on QUIC protocol"
#define DESCRIPTION_ZH "基于 QUIC 协议的 MQTT 插件"
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
    plugin->common.link_state = NEU_NODE_LINK_STATE_DISCONNECTED;

    return plugin;
}

static int driver_close(neu_plugin_t *plugin)
{
    free(plugin);

    return 0;
}
// driver_init -> driver_config -> driver_start
static int driver_init(neu_plugin_t *plugin, bool load)
{
    (void) load;
    plog_notice(plugin,
                "============================================================"
                "\ninitialize "
                "plugin============================================================\n");
    plugin->client                = NULL;
    plugin->connected             = false;
    plugin->timer                 = 0;
    plugin->keep_alive_conn_count = 0;
    // 这两个函数先后顺序不能变
    plugin->events = neu_event_new();
    add_connection_status_checker(plugin);
    return 0;
}

static int driver_config(neu_plugin_t *plugin, const char *setting)
{
    plog_notice(plugin,
                "============================================================\nconfig "
                "plugin============================================================\n");
    int res = 0;
    // 解析插件配置（包含设备信息）
    res = quic_mqtt_config_parse(plugin, setting);
    if (res != 0) {
        plog_error(plugin, "config parse failed");
        return -1;
    }
    if (plugin->client != NULL) {
        stop_and_free_client(plugin);
    }
    if (plugin->keep_alive_conn_count == 1) {
        close_keep_alive_conn(plugin);
    }

    return res;
}

static int driver_start(neu_plugin_t *plugin)
{
    plog_notice(plugin,
                "============================================================\nstart "
                "plugin============================================================\n");

    plugin->timer   = 0;
    plugin->started = true;
    if (plugin->connected == true && plugin->client != NULL) {
        // 上线设备 status 3
        publishInfo(plugin, 3);
    }
    return 0;
}

static int driver_stop(neu_plugin_t *plugin)
{

    plog_notice(plugin,
                "============================================================\nstop "
                "plugin============================================================\n");
    plugin->timer   = 0;
    plugin->started = false;
    if (plugin->connected == true && plugin->client != NULL) {
        // 下线设备 status 4
        publishInfo(plugin, 4);
    }
    return 0;
}

static int driver_uninit(neu_plugin_t *plugin)
{
    plog_notice(plugin,
                "============================================================\nuninit "
                "plugin============================================================\n");

    if (plugin->client != NULL) {
        stop_and_free_client(plugin);
    }
    if (plugin->keep_alive_conn_count == 1) {
        close_keep_alive_conn(plugin);
    }
    free(plugin);
    nlog_debug("uninit success");
    return NEU_ERR_SUCCESS;
}

static int driver_request(neu_plugin_t *plugin, neu_reqresp_head_t *head, void *data)
{
    plog_notice(plugin,
                "============================================================request "
                "plugin============================================================\n");
    if (plugin->connected == false) {
        plog_error(plugin, "插件未连接，无法启动client发送数据");
        return NEU_ERR_PLUGIN_DISCONNECTED;
    }
    // 本插件未启动，不处理请求
    if (plugin->started == false) {
        return NEU_ERR_PLUGIN_NOT_RUNNING;
    }
    // client的建立逻辑属于已知连接可用时的数据传输逻辑，所以放在判断插件是否启动的逻辑之后（未启动不需要传输数据），而连接可用状态监测有单独的线程监测
    // 打印plugin->client地址
    plog_debug(plugin, "plugin->client:0x%p", plugin->client);
    // 如果client为空，重新创建client
    if (plugin->client == NULL) {
        stop_and_free_client(plugin);
        int res = create_and_config_and_start_client(plugin);
        if (res != 0) {
            plog_error(plugin, "连接失败");
            // 保证创建失败的话,plugin->client为NULL
            stop_and_free_client(plugin);
            return NEU_ERR_PLUGIN_DISCONNECTED;
        }
    }
    // 连接已就绪，插件已启动，client 已创建, 进入数据处理流程
    plugin->timer++;
    neu_err_code_e error = NEU_ERR_SUCCESS;
    switch (head->type) {
    case NEU_REQRESP_TRANS_DATA: {
        if (plugin->monitor_count > 0) {
            plog_debug(plugin, "发布监测数据");
            plugin->monitor_count--;
            handle_trans_data(plugin, data, pMonitorTopic);
        } else {
            if (plugin->timer >= plugin->interval) {
                plugin->timer = 0;
                plog_debug(plugin, "上报数据");
                handle_trans_data(plugin, data, pPropertyTopic);
            }
        }
        break;
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

static int driver_write(neu_plugin_t *plugin, void *req, neu_datatag_t *tag, neu_value_u value)
{
    (void) plugin;
    (void) req;
    (void) tag;
    (void) value;

    return 0;
}