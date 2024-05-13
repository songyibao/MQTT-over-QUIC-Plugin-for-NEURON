//
// Created by root on 4/10/24.
//
#include "mqtt_quic_handle.h"
#include "json_rw.h"
#include "mqtt_quic_plugin.h"
#include "mqtt_quic_sdk.h"
#include "neuron.h"
#include <cjson/cJSON.h>
char* transform(const char* inputJson) {
    // 解析输入的JSON字符串
    cJSON *input = cJSON_Parse(inputJson);
    if (input == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
        return NULL;
    }

    // 创建一个JSON数组作为最终输出
    cJSON *outputArray = cJSON_CreateArray();

    // 获取timestamp
    long long timestamp = cJSON_GetObjectItemCaseSensitive(input, "timestamp")
                           ->valuedouble;

    // 获取values对象
    cJSON *values = cJSON_GetObjectItemCaseSensitive(input, "values");
    // 遍历values对象的每个成员
    cJSON *current_value = values->child; // 获取第一个子项
    if(current_value==NULL){
        return NULL;
    }

    int i=0;
    while (current_value != NULL) {
        i++;
        // 为当前键值对创建一个新的JSON对象
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", current_value->string);
        cJSON_AddNumberToObject(item, "value", current_value->valuedouble);
        cJSON_AddNumberToObject(item, "remark", timestamp+i);
        // 将新创建的对象添加到输出数组中
        cJSON_AddItemToArray(outputArray, item);

        // 移动到下一个子项
        current_value = current_value->next;
    }
    // 生成输出的JSON字符串
    char *outputJson = cJSON_Print(outputArray);

    // 清理
    cJSON_Delete(input);
    cJSON_Delete(outputArray);

    return outputJson;
}
int handle_trans_data(neu_plugin_t *plugin,void *data,int ptopic_index)
{
    int              ret      = 0;
    char            *json_str = NULL;
    json_read_resp_t resp     = {
            .plugin     = plugin,
            .trans_data = data,
    };
    ret = neu_json_encode_by_fn(&resp, json_encode_read_resp, &json_str);
    if (ret != 0) {
        plog_notice(plugin, "parse json failed");
        return -1;
    }
    plog_debug(plugin, "parse json str succeed: %s", json_str);
    char *res = transform(json_str);
    if(res==NULL){
        free(json_str);
        return -1;
    }
    free(json_str);
    plog_debug(plugin, "transform json str succeed: %s", res);
    switch (ptopic_index) {
        case pPropertyTopic:
            publishProperty(plugin,res);
            break;
        case pMonitorTopic:
            publish_monitor(plugin,res);
            break;
        default:
            break;
    }
    free(res);
    return 0;
}