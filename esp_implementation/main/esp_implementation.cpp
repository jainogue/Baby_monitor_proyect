#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>
#include <iostream>

// Include header files for Wi-Fi, Audio, MQTT, and Memory components
#include "AppWifiBle.hpp"
#include "AppAudio.hpp"
#include "AppMqtt.hpp"
#include "AppMem.hpp"

// Define a logging tag for the application
#define TAG "app"

// Create instances of each application component in an unnamed namespace for internal use
namespace
{
    app::AppWifiBle app_wifi;   // Manages Wi-Fi and BLE connections
    app::AppAudio app_audio;    // Manages audio capture and processing
    app::AppMqtt app_mqtt;      // Handles MQTT communications
    app::AppMem app_mem;        // Monitors memory usage
}

// The main application entry point, required for ESP32 programs
extern "C" void app_main()
{
    // Define the callback for when Wi-Fi is connected
    auto wifi_connected = [=](esp_ip4_addr_t *ip)
    {
        ESP_LOGI(TAG, "wifi connected");
        // Initialize MQTT with user and password after Wi-Fi connection is established
        app_mqtt.init(CONFIG_MQTT_USER, CONFIG_MQTT_PWD);
    };

    // Define the callback for when Wi-Fi is disconnected
    auto wifi_disconnected = []()
    {
        ESP_LOGW(TAG, "wifi disconnected");
    };
    
    // Initialize Wi-Fi, set connection and disconnection callbacks, and start connecting
    app_wifi.init(wifi_connected, wifi_disconnected);
    app_wifi.connect();

    // Print initial memory stats to the log
    app_mem.print();

    // Initialize audio processing with a callback for when crying is detected
    app_audio.init([](bool crying)
    { 
         // Check if the detected sound is crying or general noise
        if (crying)
        {
            // Publish "1" if crying is detected
            app_mqtt.publish("bedroom/sensor/baby_monitor", "1");
        }
        else
        {
            // Publish "0" if general noise is detected
            app_mqtt.publish("bedroom/sensor/baby_monitor", "0");
        } 
    });

    // Print memory stats after initializing audio
    app_mem.print();

    // Start audio tasks for capturing, processing, and detecting sound events
    app_audio.start();

    // Start periodic memory monitoring to log memory usage regularly
    app_mem.monitor();
}
