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
#include <curl/curl.h>
#include "internal_api/update_interval.h"
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
    plugin->base_timer_count      = 0;
    plugin->keep_alive_conn_count = 0;
    plugin->base_timer_count      = 0;
    plugin->keep_alive_conn_count = 0;
    plugin->node_name = NULL;
    plugin->group_name = NULL;
    // 先初始化events, 再添加定时器, 先后顺序不能变
    plugin->events = neu_event_new();
    add_connection_status_checker(plugin);
    // 暂时停用单独的上报间隔控制，而是根据订阅的南向设备点位组的间隔来采集
//    add_base_timer(plugin);
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

    plugin->base_timer_count = 0;
    plugin->started          = true;
    // 当前采集间隔，初始化为0代表两种状态都不是
    plugin->interval = 0;
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
    plugin->base_timer_count = 0;
    plugin->started          = false;
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
    neu_event_close(plugin->events);
    config_uint(plugin);
    nlog_debug("uninit success");
    return NEU_ERR_SUCCESS;
}

static int driver_request(neu_plugin_t *plugin, neu_reqresp_head_t *head, void *data)
{
    plog_notice(plugin,
                "============================================================request "
                "plugin============================================================\n");
    neu_reqresp_trans_data_t *trans_data = (neu_reqresp_trans_data_t *)data;
    if (plugin->connected == false) {
        plog_error(plugin, "MQTT 服务器未连接，无法启动 client 发送数据");
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
            // 创建失败, 销毁 plugin->client, 并置为 NULL
            stop_and_free_client(plugin);
            return NEU_ERR_PLUGIN_DISCONNECTED;
        }
    }
    // 连接已就绪，插件已启动，client 已创建, 进入数据处理流程
    neu_err_code_e error = NEU_ERR_SUCCESS;
    switch (head->type) {
        case NEU_REQ_SUBSCRIBE_GROUP:
            neu_req_subscribe_t *req_data = (neu_req_subscribe_t *)data;
            // log the request
            plog_notice(plugin, "app [%s] subscribe [%s]'s group: [%s]", req_data->app,
                        req_data->driver, req_data->group);
            strcpy(plugin->node_name, req_data->driver);
            strcpy(plugin->group_name, req_data->group);
            update_interval(trans_data->driver,trans_data->group,plugin->config_interval,plugin);
        case NEU_REQRESP_TRANS_DATA: {

            plog_debug(plugin,"driver name:%s,group_name:%s",trans_data->driver,trans_data->group);
            if (plugin->monitor_count > 0) {
                plog_debug(plugin,"plugin->monitor_inerval:%d",plugin->monitor_interval);
                if(plugin->interval!=plugin->monitor_interval){
                    plog_debug(plugin,"实时监测，更新采集间隔");
                    CURLcode res = update_interval(trans_data->driver,trans_data->group,plugin->monitor_interval,plugin);
                    if(res==CURLE_OK){
                        plog_debug(plugin,"更新实时监测间隔成功,plugin->monitor_interval:%d",plugin->monitor_interval);
                        plugin->interval=plugin->monitor_interval;
                        plugin->monitor_count++;
                    }
                }
                plog_info(plugin, "发布实时监测数据,monitor_count:%d", plugin->monitor_count);
                plugin->monitor_count--;
                handle_trans_data(plugin, data, pMonitorTopic);
            } else {
                if(plugin->interval!=plugin->config_interval){

                    CURLcode res = update_interval(trans_data->driver,trans_data->group,plugin->config_interval,plugin);
                    if(res==CURLE_OK){
                        plugin->interval=plugin->config_interval;
                        plog_debug(plugin,"定期上报，更新采集间隔成功 %d",plugin->interval);
                    }else{
                        plog_debug(plugin,"定期上报，更新采集间隔失败 %d",plugin->interval);
                    }
                }
                // 暂时停用单独的上报间隔控制，而是根据订阅的南向设备点位组的间隔来采集
    //            if (plugin->base_timer_count >= plugin->interval) {
    //                plugin->base_timer_count = 0;
    //                plog_info(plugin, "发布定期上报数据");
    //                handle_trans_data(plugin, data, pPropertyTopic);
    //            }
                plog_info(plugin, "发布定期上报数据");
                handle_trans_data(plugin, data, pPropertyTopic);
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