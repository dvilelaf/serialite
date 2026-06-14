#include "ui_status_format.h"

#include <stdio.h>

bool ui_status_format(const ui_status_format_input_t *input, ui_status_format_output_t *output)
{
    if (input == NULL || output == NULL) {
        return false;
    }

    if (input->usb_connected) {
        snprintf(
            output->usb_line,
            sizeof(output->usb_line),
            "USB connected  RX %llu  TX %llu",
            (unsigned long long)input->usb_rx_bytes,
            (unsigned long long)input->usb_tx_bytes);
    } else {
        snprintf(output->usb_line, sizeof(output->usb_line), "USB disconnected");
    }

    snprintf(
        output->client_line,
        sizeof(output->client_line),
        "AP %s  WiFi %u  Web %u",
        input->ap_started ? "active" : "inactive",
        (unsigned)input->wifi_clients,
        (unsigned)input->web_clients);

    if (input->web_locked) {
        snprintf(output->audit_line, sizeof(output->audit_line), "Web locked");
    } else if (input->web_writer_active) {
        snprintf(output->audit_line, sizeof(output->audit_line), "Web write active");
    } else {
        snprintf(output->audit_line, sizeof(output->audit_line), "Web read-only");
    }

    if (input->bridge_drops == 0) {
        snprintf(output->error_line, sizeof(output->error_line), "No bridge drops");
    } else {
        snprintf(
            output->error_line,
            sizeof(output->error_line),
            "Bridge drops %llu",
            (unsigned long long)input->bridge_drops);
    }

    return true;
}
