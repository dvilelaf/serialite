#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_status_format.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void test_formats_healthy_state(void)
{
    ui_status_format_input_t input = {
        .ap_started = true,
        .wifi_clients = 2,
        .web_clients = 1,
        .usb_connected = true,
        .usb_rx_bytes = 1234,
        .usb_tx_bytes = 56,
        .bridge_drops = 0,
    };
    ui_status_format_output_t output;

    CHECK(ui_status_format(&input, &output));
    CHECK(strcmp(output.usb_line, "USB connected  RX 1234  TX 56") == 0);
    CHECK(strcmp(output.client_line, "AP active  WiFi 2  Web 1") == 0);
    CHECK(strcmp(output.error_line, "No bridge drops") == 0);
}

static void test_formats_problem_state(void)
{
    ui_status_format_input_t input = {
        .ap_started = false,
        .wifi_clients = 0,
        .web_clients = 0,
        .usb_connected = false,
        .usb_rx_bytes = 0,
        .usb_tx_bytes = 0,
        .bridge_drops = 7,
    };
    ui_status_format_output_t output;

    CHECK(ui_status_format(&input, &output));
    CHECK(strcmp(output.usb_line, "USB disconnected") == 0);
    CHECK(strcmp(output.client_line, "AP inactive  WiFi 0  Web 0") == 0);
    CHECK(strcmp(output.error_line, "Bridge drops 7") == 0);
}

static void test_rejects_invalid_arguments(void)
{
    ui_status_format_input_t input = {0};
    ui_status_format_output_t output;

    CHECK(!ui_status_format(NULL, &output));
    CHECK(!ui_status_format(&input, NULL));
}

int main(void)
{
    test_formats_healthy_state();
    test_formats_problem_state();
    test_rejects_invalid_arguments();
    return 0;
}
