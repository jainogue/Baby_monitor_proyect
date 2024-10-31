#pragma once

#include "mqtt_client.h"
#include "esp_log.h"

namespace app
{
    // AppMqtt class: Manages the MQTT client for connecting, handling events, and publishing messages
    class AppMqtt
    {
    private:
        // MQTT client handle for managing MQTT operations
        esp_mqtt_client_handle_t m_mqtt_client;

        static constexpr const char* TAG = "AppMqtt";

        // Static callback function to handle MQTT events, such as connection, disconnection, and message arrival
        static void mqtt_event_handler_cb(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
        {
            // Cast the event_data to the appropriate event type
            esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;

            // Obtain the client handle from the event
            esp_mqtt_client_handle_t client = event->client;

            // Handle different event types based on the event ID
            switch (event->event_id)
            {
                // Event triggered when the client successfully connects to the MQTT broker
                case MQTT_EVENT_CONNECTED:
                    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
                    break;

                // Event triggered when the client disconnects from the MQTT broker
                case MQTT_EVENT_DISCONNECTED:
                    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
                    break;

                // Event triggered when the client receives data (message) from the broker
                case MQTT_EVENT_DATA:
                    ESP_LOGI(TAG, "MQTT_EVENT_DATA, received topic: %.*s, data: %.*s", 
                             event->topic_len, event->topic, event->data_len, event->data);
                    break;

                // Handle other events as needed (optional)
                default:
                    break;
            }
        }

    public:
        // Initializes the MQTT client configuration and starts the client
        void init(const char* username, const char* password)
        {
            // Set up MQTT configuration struct
            esp_mqtt_client_config_t mqtt_cfg = {};
            mqtt_cfg.broker.address.uri = "mqtt://" CONFIG_MQTT_BROKER_IP; // Set broker URI from config
            mqtt_cfg.broker.address.port = CONFIG_MQTT_PORT;                // Set broker port from config
            mqtt_cfg.credentials.username = username;                       // Set MQTT username
            mqtt_cfg.credentials.authentication.password = password;        // Set MQTT password

            // Initialize the MQTT client with the specified configuration
            m_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

            // Register the event handler callback to handle all MQTT events
            esp_mqtt_client_register_event(m_mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler_cb, NULL);

            // Start the MQTT client, initiating connection to the broker
            esp_mqtt_client_start(m_mqtt_client);
        }

        // Publishes a message to a specific topic on the MQTT broker
        void publish(const char* topic, const char* data)
        {
            // Publish the message with QoS 1 (ensure delivery) and retain flag 0 (do not retain message on broker)
            int msg_id = esp_mqtt_client_publish(m_mqtt_client, topic, data, 0, 1, 0);

            // Log the message ID, topic, and data for confirmation
            ESP_LOGI(TAG, "Published: msg_id=%d, topic=%s, data=%s", msg_id, topic, data);
        }
    };
}
