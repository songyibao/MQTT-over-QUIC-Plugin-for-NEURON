//
// Created by root on 5/8/24.
//

#ifndef NEURON_DETECTOR_H
#define NEURON_DETECTOR_H
#include "../mqtt_quic_plugin.h"
int  check_connect_status_callback(void *arg);
int base_timer_callback(void *arg);
void close_keep_alive_conn(neu_plugin_t *plugin);
#endif // NEURON_DETECTOR_H
