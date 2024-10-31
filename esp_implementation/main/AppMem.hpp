#pragma once

// Include ESP-IDF libraries for timers, logging, error handling, and memory management
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

// Define a namespace to encapsulate the AppMem class
namespace app
{
    // AppMem class: Manages memory monitoring and logging periodically on the ESP32
    class AppMem
    {
        private: 
            // Logging tag for memory-related operations
            constexpr static const char *TAG{"app-mem"};

            // Timer handle for managing periodic memory monitoring
            esp_timer_handle_t m_periodic_timer;

            // Static callback function triggered by the timer to log memory statistics
            static void periodic_timer_callback(void *arg)
            {
                // Log free and total memory available in internal RAM
                ESP_LOGI(TAG, "------- mem stats -------");
                ESP_LOGI(TAG, "internal\t: %10u (free) / %10u (total)", 
                         heap_caps_get_free_size(MALLOC_CAP_INTERNAL), 
                         heap_caps_get_total_size(MALLOC_CAP_INTERNAL));

                // Log free and total memory available in SPIRAM (external RAM)
                ESP_LOGI(TAG, "spiram\t: %10u (free) / %10u (total)", 
                         heap_caps_get_free_size(MALLOC_CAP_SPIRAM), 
                         heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
            }

        public:
            // Initializes and starts the memory monitor with periodic logging
            void monitor(void)
            {
                // Configure the timer arguments, specifying the callback function and context
                const esp_timer_create_args_t periodic_timer_args = {
                    .callback = periodic_timer_callback,
                    .arg = this // Pass instance pointer if needed in the callback (currently unused)
                };

                // Create the periodic timer and check for errors
                ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &m_periodic_timer));

                // Start the timer to trigger every 5,000,000 microseconds (5 seconds)
                ESP_ERROR_CHECK(esp_timer_start_periodic(m_periodic_timer, 5000000u));
            }

            // Immediately print the current memory stats by manually calling the callback function
            void print(void)
            {
                // Call the periodic callback to log memory stats instantly
                periodic_timer_callback(nullptr);
            }
    };
}
