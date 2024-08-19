//
// Created by root on 8/19/24.
//

#ifndef QUIC_CLIENTV5_UPDATE_INTERVAL_H
#define QUIC_CLIENTV5_UPDATE_INTERVAL_H
#include <curl/curl.h>
CURLcode update_interval(char *node, char *group, int interval, neu_plugin_t *plugin);
#endif //QUIC_CLIENTV5_UPDATE_INTERVAL_H
