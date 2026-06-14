#include "ui_status_format.h"

#include <stdio.h>

bool ui_status_format(const ui_status_format_input_t *input, ui_status_format_output_t *output)
{
    if (input == NULL || output == NULL) {
        return false;
    }

    (void)input->usb_rx_bytes;
    (void)input->usb_tx_bytes;
    snprintf(output->usb_line, sizeof(output->usb_line), "%s", input->usb_connected ? "USB OK" : "USB LOST");

    if (input->ap_started) {
        snprintf(
            output->client_line,
            sizeof(output->client_line),
            "%u WiFi  %u Web",
            (unsigned)input->wifi_clients,
            (unsigned)input->web_clients);
    } else {
        snprintf(output->client_line, sizeof(output->client_line), "AP OFF");
    }

    if (input->web_locked) {
        snprintf(output->audit_line, sizeof(output->audit_line), "Locked");
    } else if (input->web_writer_active) {
        snprintf(output->audit_line, sizeof(output->audit_line), "Input active");
    } else {
        snprintf(output->audit_line, sizeof(output->audit_line), "Input idle");
    }

    if (input->bridge_drops == 0) {
        output->error_line[0] = '\0';
    } else {
        snprintf(
            output->error_line,
            sizeof(output->error_line),
            "Drops %llu",
            (unsigned long long)input->bridge_drops);
    }

    return true;
}
