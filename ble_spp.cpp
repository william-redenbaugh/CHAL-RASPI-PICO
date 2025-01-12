#include <stdio.h>
#include "global_includes.h"

// PICO HAL Layer
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"

// BLE stuff
#include "ble/gatt-service/nordic_spp_service_server.h"
#include "btstack.h"
#include "nordic_spp_le_streamer.h"

#define REPORT_INTERVAL_MS 3000
#define MAX_NR_CONNECTIONS 3

#define FIFO_MAX_SIZE (4096)

#ifdef BLE_SPP_DEBUG
#define ble_spp_printf(...) os_printf(__VA_ARGS__)
#else
#define ble_spp_printf(...) ((void)0)
#endif

#define SPP_DATA_MAX_LEN (512)

static byte_array_fifo *spp_in_fifo;
static byte_array_fifo *spp_out_fifo;
int spp_mtu_size = 512;
static ble_connected_cb_t bluetooth_pair_cb = NULL;
static ble_disconnected_cb_t bluetooth_disc_cb = NULL;

const uint8_t adv_data[] = {
    // Flags general discoverable, BR/EDR not supported
    2,
    BLUETOOTH_DATA_TYPE_FLAGS,
    0x06,
    // Name
    8,
    BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'n',
    'R',
    'F',
    ' ',
    'S',
    'P',
    'P',
    // UUID ...
    17,
    BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
    0x9e,
    0xca,
    0xdc,
    0x24,
    0xe,
    0xe5,
    0xa9,
    0xe0,
    0x93,
    0xf3,
    0xa3,
    0xb5,
    0x1,
    0x0,
    0x40,
    0x6e,
};
const uint8_t adv_data_len = sizeof(adv_data);

static btstack_packet_callback_registration_t hci_event_callback_registration;

// support for multiple clients
typedef struct
{
    char name;
    int le_notification_enabled;
    hci_con_handle_t connection_handle;
    int counter;
    char output_data[200];
    int output_data_len;
    btstack_context_callback_registration_t send_request;
} nordic_spp_le_streamer_connection_t;

static nordic_spp_le_streamer_connection_t nordic_spp_le_streamer_connections[MAX_NR_CONNECTIONS];

static nordic_spp_le_streamer_connection_t *current_context;
// round robin sending
static int connection_index = 0;

static void init_connections(void)
{
    // track connections
    int i;
    for (i = 0; i < MAX_NR_CONNECTIONS; i++)
    {
        nordic_spp_le_streamer_connections[i].connection_handle = HCI_CON_HANDLE_INVALID;
        nordic_spp_le_streamer_connections[i].name = 'A' + i;
    }
}

static nordic_spp_le_streamer_connection_t *connection_for_conn_handle(hci_con_handle_t conn_handle)
{
    int i;
    for (i = 0; i < MAX_NR_CONNECTIONS; i++)
    {
        if (nordic_spp_le_streamer_connections[i].connection_handle == conn_handle)
            return &nordic_spp_le_streamer_connections[i];
    }
    return NULL;
}

/*
 * @section HCI Packet Handler
 *
 * @text The packet handler prints the welcome message and requests a connection paramter update for LE Connections
 */

/* LISTING_START(packetHandler): Packet Handler */
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    uint16_t conn_interval;
    hci_con_handle_t con_handle;

    if (packet_type != HCI_EVENT_PACKET)
        return;

    switch (hci_event_packet_get_type(packet))
    {
    case BTSTACK_EVENT_STATE:
        // BTstack activated, get started
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING)
        {
            printf("To start the streaming, please run nRF Toolbox -> UART to connect.\n");
        }
        break;
    case HCI_EVENT_META_GAP:
        switch (hci_event_gap_meta_get_subevent_code(packet))
        {
        case GAP_SUBEVENT_LE_CONNECTION_COMPLETE:
            // print connection parameters (without using float operations)
            con_handle = gap_subevent_le_connection_complete_get_connection_handle(packet);
            conn_interval = gap_subevent_le_connection_complete_get_conn_interval(packet);
            printf("LE Connection - Connection Interval: %u.%02u ms\n", conn_interval * 125 / 100, 25 * (conn_interval & 3));
            printf("LE Connection - Connection Latency: %u\n", gap_subevent_le_connection_complete_get_conn_latency(packet));

            // request min con interval 15 ms for iOS 11+
            printf("LE Connection - Request 15 ms connection interval\n");
            gap_request_connection_parameter_update(con_handle, 12, 12, 4, 0x0048);
            break;
        default:
            break;
        }
        break;

    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet))
        {
        case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
            // print connection parameters (without using float operations)
            con_handle = hci_subevent_le_connection_update_complete_get_connection_handle(packet);
            conn_interval = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
            printf("LE Connection - Connection Param update - connection interval %u.%02u ms, latency %u\n", conn_interval * 125 / 100,
                   25 * (conn_interval & 3), hci_subevent_le_connection_update_complete_get_conn_latency(packet));
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}
/* LISTING_END */

/*
 * @section ATT Packet Handler
 *
 * @text The packet handler is used to setup and tear down the spp-over-gatt connection and its MTU
 */

/* LISTING_START(packetHandler): Packet Handler */
static void att_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET)
        return;

    int mtu;
    nordic_spp_le_streamer_connection_t *context;

    switch (hci_event_packet_get_type(packet))
    {
    case ATT_EVENT_CONNECTED:
        // setup new
        context = connection_for_conn_handle(HCI_CON_HANDLE_INVALID);
        current_context = context;

        if (!context)
            break;
        context->connection_handle = att_event_connected_get_handle(packet);
        break;
    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        mtu = att_event_mtu_exchange_complete_get_MTU(packet) - 3;
        context = connection_for_conn_handle(att_event_mtu_exchange_complete_get_handle(packet));
        if (!context)
            break;
        context->output_data_len = btstack_min(mtu - 3, sizeof(context->output_data));
        printf("%c: ATT MTU = %u => use test data of len %u\n", context->name, mtu, context->output_data_len);
        break;

    default:
        break;
    }
}

static void nordic_can_send(void *some_context)
{
    nordic_spp_le_streamer_connection_t *context = current_context;

    int count = fifo_byte_array_count(spp_out_fifo);
    // Max transfer size
    if (count > context->output_data_len)
        count = context->output_data_len;

    // os_printf("Sending data out!");
    dequeue_bytes_bytearray_fifo(spp_out_fifo, (uint8_t *)context->output_data, count);

    nordic_spp_service_server_send(context->connection_handle, (uint8_t *)context->output_data, context->output_data_len);
}

static void nordic_spp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    hci_con_handle_t con_handle;
    nordic_spp_le_streamer_connection_t *context;
    switch (packet_type)
    {
    case HCI_EVENT_PACKET:
        if (hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META)
            break;
        switch (hci_event_gattservice_meta_get_subevent_code(packet))
        {
        case GATTSERVICE_SUBEVENT_SPP_SERVICE_CONNECTED:
            con_handle = gattservice_subevent_spp_service_connected_get_con_handle(packet);
            context = connection_for_conn_handle(con_handle);
            if (!context)
                break;

            printf("%c: Nordic SPP connected\n", context->name);
            context->le_notification_enabled = 1;
            context->send_request.callback = &nordic_can_send;
            break;
        case GATTSERVICE_SUBEVENT_SPP_SERVICE_DISCONNECTED:
            con_handle = gattservice_subevent_spp_service_disconnected_get_con_handle(packet);
            context = connection_for_conn_handle(con_handle);
            current_context = NULL;
            if (!context)
                break;
            // free connection
            printf("%c: Nordic SPP disconnected\n", context->name);
            context->le_notification_enabled = 0;
            context->connection_handle = HCI_CON_HANDLE_INVALID;
            break;
        default:
            break;
        }
        break;
    case RFCOMM_DATA_PACKET:
        printf("RECV: ");
        printf_hexdump(packet, size);
        enqueue_bytes_bytearray_fifo(spp_in_fifo, packet, size);

        context = connection_for_conn_handle((hci_con_handle_t)channel);
        if (!context)
            break;
        break;
    default:
        break;
    }
}

static inline void process_ble_data(uint8_t *arr)
{
    // Eventually will panic for now, but no connection so?
    if (current_context == NULL)
        return;

    // Sit n wait for any data to come in
    int ret = block_until_n_bytes_fifo(spp_out_fifo, 1);
    if (ret != OS_RET_OK)
    {
        os_panic(ret);
    }

    nordic_spp_service_server_request_can_send_now(&current_context->send_request, current_context->connection_handle);
}

void hal_ble_serial_send_task(void *parameters)
{
    for (;;)
    {
        process_ble_data(NULL);
    }
}

int hal_ble_serial_receive(uint8_t *data, size_t len)
{
    return dequeue_bytes_bytearray_fifo(spp_in_fifo, data, len);
}

int hal_ble_serial_receive_block(uint8_t *data, size_t len)
{
    int ret = block_until_n_bytes_fifo(spp_in_fifo, len);

    if (ret != OS_RET_OK)
    {
        return ret;
    }
    // os_printf("\nhmm: %d\n", ret);
    int n = dequeue_bytes_bytearray_fifo(spp_in_fifo, data, len);

    if (n != len)
    {
        ble_spp_printf("\nMismatched data rx sise %d, %d\n", n, len);
        hal_ble_flush_serial();
        return OS_RET_INVALID_PARAM;
    }
    return 0;
}

int hal_ble_serial_receive_block_timeout(uint8_t *data, size_t len, uint32_t timeout_ms)
{

    int ret = block_until_n_bytes_fifo_timeout(spp_in_fifo, len, timeout_ms);
    if (ret != OS_RET_OK)
    {
        return ret;
    }

    // os_printf("\nhmm: %d\n", ret);
    int n = dequeue_bytes_bytearray_fifo(spp_in_fifo, data, len);

    if (n != len)
    {
        ble_spp_printf("\nMismatched data rx sise %d, %d\n", n, len);
        hal_ble_flush_serial();
        return OS_RET_INVALID_PARAM;
    }
    return 0;
}

int hal_ble_flush_serial(void)
{
    fifo_flush(spp_in_fifo);
    return 0;
}

hal_bt_serial_err_t hal_ble_serial_init(ble_connected_cb_t cb, ble_disconnected_cb_t disc_cb, char *name, size_t name_len)
{
    // turn on!
    int n = hci_power_control(HCI_POWER_ON);

    return HAL_BT_SERIAL_OK;
}