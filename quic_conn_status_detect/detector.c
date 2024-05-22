//
// Created by root on 5/8/24.
//
#include "detector.h"

#include <msquic.h>
typedef struct det_arg {
    neu_plugin_t *plugin;
    bool          connected;
} det_arg_t;
static const QUIC_API_TABLE *msapi;
static QUIC_STATUS           Status;
// 初始化为NULL，用于判断是否已经创建了连接
static HQUIC Connection = NULL;
static HQUIC Registration;
static HQUIC Configuration;
// Config for msquic
const QUIC_REGISTRATION_CONFIG this_quic_reg_config = { "mqtt", QUIC_EXECUTION_PROFILE_LOW_LATENCY };

const QUIC_BUFFER Alpn = { sizeof("mqtt") - 1, (uint8_t *) "mqtt" };
void              this_quic_open()
{
    QUIC_STATUS rv = QUIC_STATUS_SUCCESS;
    // only Open MsQUIC lib once, otherwise cause memleak
    if (msapi == NULL)
        if (QUIC_FAILED(rv = MsQuicOpen2(&msapi))) {
            //            log_error("msapiOpen2 failed, 0x%x!\n", rv);
            nlog_debug("msapiOpen2 failed, 0x%x!\n", rv);
            return;
        }

    // Create a registration for the app's connections.
    if (QUIC_FAILED(rv = msapi->RegistrationOpen(&this_quic_reg_config, &Registration))) {
        //        log_error("RegistrationOpen failed, 0x%x!\n", rv);
        nlog_debug("RegistrationOpen failed, 0x%x!\n", rv);
        return;
    }

    return;
}
BOOLEAN ClientLoadConfiguration(BOOLEAN Unsecure)
{
    QUIC_SETTINGS Settings = { 0 };
    //
    // Configures the client's idle timeout.
    //
    Settings.IdleTimeoutMs             = 2000;
    Settings.IsSet.IdleTimeoutMs       = TRUE;
    Settings.KeepAliveIntervalMs       = 1000;
    Settings.IsSet.KeepAliveIntervalMs = TRUE;

    //
    // Configures a default client configuration, optionally disabling
    // server certificate validation.
    //
    QUIC_CREDENTIAL_CONFIG CredConfig;
    memset(&CredConfig, 0, sizeof(CredConfig));
    CredConfig.Type  = QUIC_CREDENTIAL_TYPE_NONE;
    CredConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;
    if (Unsecure) {
        CredConfig.Flags |= QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
    }

    this_quic_open();
    //
    // Allocate/initialize the configuration object, with the configured ALPN
    // and settings.
    //
    QUIC_STATUS Status = QUIC_STATUS_SUCCESS;
    if (QUIC_FAILED(Status = msapi->ConfigurationOpen(Registration, &Alpn, 1, &Settings, sizeof(Settings), NULL,
                                                      &Configuration))) {
        printf("ConfigurationOpen failed, 0x%x!\n", Status);
        return FALSE;
    }

    //
    // Loads the TLS credential part of the configuration. This is required even
    // on client side, to indicate if a certificate is required or not.
    //
    if (QUIC_FAILED(Status = msapi->ConfigurationLoadCredential(Configuration, &CredConfig))) {
        printf("ConfigurationLoadCredential failed, 0x%x!\n", Status);
        return FALSE;
    }

    return TRUE;
}
_IRQL_requires_max_(DISPATCH_LEVEL) _Function_class_(QUIC_CONNECTION_CALLBACK) QUIC_STATUS QUIC_API
    ClientConnectionCallback(_In_ HQUIC connection, _In_opt_ void *Context, _Inout_ QUIC_CONNECTION_EVENT *Event)
{
    det_arg_t    *arg    = (det_arg_t *) Context;
    neu_plugin_t *plugin = arg->plugin;

    //    UNREFERENCED_PARAMETER(Context);
    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        //
        // The handshake has completed for the connection.
        //
        plog_debug(plugin, "[conn][%p] Connected\n", connection);

        // 针对于插件的生命周期, 默认false,
        // 表示插件的连接状态(误差范围为每个探活连接的请求间隔)，只有在本事件类型触发时才设置为true
        plugin->connected         = true;
        plugin->common.link_state = NEU_NODE_LINK_STATE_CONNECTED;
        // 针对于单个探活连接的生命周期, 默认false, 表示当前探活连接是否已经连接
        arg->connected = true;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        //
        // The connection has been shut down by the transport. Generally, this
        // is the expected way for the connection to shut down with this
        // protocol, since we let idle timeout kill the connection.
        //
        if (Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_CONNECTION_IDLE) {
            plog_debug(plugin, "[conn][%p] Successfully shut down on idle.\n", connection);
            if (arg->connected == false) {
                // 表示当前探活连接未连接成功，需要将插件的连接状态设置为false
                plugin->common.link_state = NEU_NODE_LINK_STATE_DISCONNECTED;
                plugin->connected         = false;
            }
        } else {
            plog_debug(plugin, "[conn][%p] Shut down by transport, 0x%x\n", connection,
                       Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
            if (arg->connected == false) {
                plugin->common.link_state = NEU_NODE_LINK_STATE_DISCONNECTED;
                plugin->connected         = false;
            }
        }
        //        plugin->common.link_state = NEU_NODE_LINK_STATE_DISCONNECTED;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        //
        // The connection was explicitly shut down by the peer.
        //
        plog_debug(plugin, "[conn][%p] Shut down by peer, 0x%llu\n", connection,
                   (unsigned long long) Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        if (arg->connected == false) {
            // 表示当前探活连接未连接成功，需要将插件的连接状态设置为false
            plugin->common.link_state = NEU_NODE_LINK_STATE_DISCONNECTED;
            plugin->connected         = false;
        }

        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        //
        // The connection has completed the shutdown process and is ready to be
        // safely cleaned up.
        //
        plog_debug(plugin, "[conn][%p] All done\n", connection);
        if (!Event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
            plog_debug(plugin, "SHUTDOWN_COMPLETE.AppCloseInProgress");
            if (arg->connected == false) {
                plugin->common.link_state = NEU_NODE_LINK_STATE_DISCONNECTED;
                plugin->connected         = false;
            }
            //            msapi->ConfigurationClose(Configuration);
            //            msapi->RegistrationClose(Registration);
            //            MsQuicClose(msapi);
            plugin->keep_alive_conn_count = 0;
            msapi->ConnectionClose(connection);
        }

        break;
    case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
        //
        // A resumption ticket (also called New Session Ticket or NST) was
        // received from the server.
        //
        plugin->connected         = true;
        plugin->common.link_state = NEU_NODE_LINK_STATE_CONNECTED;
        plog_debug(plugin, "[conn][%p] Resumption ticket received (%u bytes):\n", connection,
                   Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength);
        //        for (uint32_t i = 0; i < Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicketLength; i++) {
        //            printf("%.2X", (uint8_t) Event->RESUMPTION_TICKET_RECEIVED.ResumptionTicket[i]);
        //        }
        //        printf("\n");
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}
int base_timer_callback(void *arg){
    neu_plugin_t *plugin = (neu_plugin_t *) arg;
    plugin->base_timer_count++;
    return 0;
}
int check_connect_status_callback(void *arg)
{
    neu_plugin_t *plugin = (neu_plugin_t *) arg;
    //    if (plugin->connected == true) {
    //        plog_debug(plugin, "插件已经连接，不再重复发起探活连接");
    //        return 0;
    //    }
    if (plugin->keep_alive_conn_count == 1) {
        plog_debug(plugin, "已经创建连接，不再重复发起探活连接");
        return 0;
    }
    plugin->keep_alive_conn_count = 1;
    det_arg_t *conn_arg           = (det_arg_t *) malloc(sizeof(det_arg_t));
    conn_arg->plugin              = plugin;
    conn_arg->connected           = false;
    ClientLoadConfiguration(TRUE);
    plog_debug(plugin, "探测 MQTT 服务端");

    if (QUIC_FAILED(msapi->ConnectionOpen(Registration, ClientConnectionCallback, conn_arg, &Connection))) {
        plog_debug(plugin, "ConnectionOpen Failed, 0x%x", Status);
    }

    //
    // Get the target / server name or IP from the command line.
    //
    const char *Target  = plugin->host;
    uint16_t    UdpPort = plugin->port;

    plog_debug(plugin, "ConnectionOpen Done=>[conn][%p] Connecting mqtt-quic://%s:%u...\n", Connection, Target,
               UdpPort);

    //
    // Start the connection to the server.
    //
    if (QUIC_FAILED(
            Status = msapi->ConnectionStart(Connection, Configuration, QUIC_ADDRESS_FAMILY_UNSPEC, Target, UdpPort))) {
        plog_debug(plugin, "ConnectionStart failed, 0x%x!", Status);
    }
}

void close_keep_alive_conn(neu_plugin_t *plugin)
{
    if (plugin->keep_alive_conn_count == 1) {
        plog_debug(plugin, "配置已修改,关闭探活连接");
        msapi->ConnectionClose(Connection);
        plugin->connected             = false;
        plugin->common.link_state     = NEU_NODE_LINK_STATE_DISCONNECTED;
        plugin->keep_alive_conn_count = 0;
        //        msapi->ConnectionShutdown(Connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
    }
}
