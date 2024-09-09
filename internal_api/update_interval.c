//
// Created by SongYibao on 8/19/24.
//
#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include "../mqtt_quic_plugin.h"
#include "update_interval.h"



char *params_to_string(const char *node, const char *group, int interval) {
    char *str = (char *) malloc(strlen(node)+strlen(group)+2+40);
    sprintf(str, "{\"node\":\"%s\",\"group\":\"%s\",\"interval\":%d}", node,
            group, interval);
    return str;
}

CURLcode update_interval(char *node, char *group, int interval, neu_plugin_t *plugin) {
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_URL,
                         "http://127.0.0.1:7000/api/v2/group");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "http");
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers,
                                    "Authorization: Bearer eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJieXdnIiwiaWF0IjoiMTcxOTkyNDUxMSIsImV4cCI6IjE3ODI5OTY0NTQiLCJhdWQiOiJuZXVyb24iLCJib2R5RW5jb2RlIjoiMCJ9.eQGzzOF10cgav7dO1rdpMnSyQtZtCmzKDOuLPbLYAQzfOteifLWGM6dD3QoBb1-oD6HLqabouMVn9LMwbeV5mnyOgKFbCNzIwke6N6pqrtd_500bZQDmSIYCDytZkXWj4__g4Zy5oPCK0Xfz8n-w4bLNKzGK-Uo7nxMIfBvxyNhyqth7g8UZebcUJwxECaHluUuocWkS6iD-_rWcIR3cbC7oWWryFvC0ZE34BmkHDXNBtL6yL_eg5XXHOzjQynLOvVG_EXBKKrhdVZeBrFiykpeSE4Uo5REZZUtJ0BPwN6n8kjPQzCA3k9x0JIMSvN7FOob0X3-BGSzpqjBwFzSEPw");
        headers = curl_slist_append(headers,
                                    "User-Agent: Apifox/1.0.0 (https://apifox.com)");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        char *data = params_to_string(node, group, interval);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        res = curl_easy_perform(curl);
        free(data);
    }else{
        res = CURLE_FAILED_INIT;
    }
    curl_easy_cleanup(curl);

    return res;
}
// update_interval(plugin->node_name, plugin->group_name, plugin->node_group_interval + 1, plugin);
