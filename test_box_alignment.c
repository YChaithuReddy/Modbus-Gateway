#include <stdio.h>
#include <string.h>

// Test program to verify box alignment
int main() {
    char buffer[100];

    // Test data
    int mqtt_connected = 1;
    unsigned long telemetry_send_count = 12345;
    int sensor_count = 4;
    int web_server_running = 1;
    int trigger_gpio = 34;
    int webserver_led_on = 1;
    int mqtt_led_on = 0;
    int sensor_led_on = 1;
    unsigned long heap_size = 123456;
    unsigned long min_heap = 114596;

    printf("+----------------------------------------------+\n");
    printf("|           SYSTEM STATUS MONITOR              |\n");
    printf("+----------------------------------------------+\n");

    snprintf(buffer, sizeof(buffer),
             "| MQTT: %-15s Messages: %-10lu |",
             mqtt_connected ? "CONNECTED" : "OFFLINE",
             telemetry_send_count);
    printf("%s\n", buffer);

    snprintf(buffer, sizeof(buffer),
             "| Sensors: %-12d Web: %-14s |",
             sensor_count,
             web_server_running ? "RUNNING" : "STOPPED");
    printf("%s\n", buffer);

    snprintf(buffer, sizeof(buffer),
             "| GPIO: %-11d LEDs: W=%3s M=%3s S=%3s |",
             trigger_gpio,
             webserver_led_on ? "ON" : "OFF",
             mqtt_led_on ? "ON" : "OFF",
             sensor_led_on ? "ON" : "OFF");
    printf("%s\n", buffer);

    snprintf(buffer, sizeof(buffer),
             "| Free Heap: %-10lu Min Free: %-10lu |",
             heap_size,
             min_heap);
    printf("%s\n", buffer);

    snprintf(buffer, sizeof(buffer),
             "| Tasks: Modbus=%3s    MQTT=%3s    Telem=%3s |",
             "OK", "OK", "OK");
    printf("%s\n", buffer);

    printf("+----------------------------------------------+\n");

    // Print character counts for verification
    printf("\nLine lengths:\n");
    printf("Border: %zu chars\n", strlen("+----------------------------------------------+"));
    printf("Title:  %zu chars\n", strlen("|           SYSTEM STATUS MONITOR              |"));

    snprintf(buffer, sizeof(buffer),
             "| MQTT: %-15s Messages: %-10lu |",
             "CONNECTED", telemetry_send_count);
    printf("MQTT:   %zu chars\n", strlen(buffer));

    snprintf(buffer, sizeof(buffer),
             "| GPIO: %-11d LEDs: W=%3s M=%3s S=%3s |",
             34, "ON", "OFF", "ON");
    printf("GPIO:   %zu chars\n", strlen(buffer));

    return 0;
}