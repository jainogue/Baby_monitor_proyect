#pragma once

// Standard libraries and necessary FreeRTOS and ESP32 headers
#include <cstring>
#include <mutex>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

// Edge Impulse and ESP32 audio-processing libraries
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "dl_lib_coefgetter_if.h"
#include "esp_afe_sr_models.h"
#include "esp_board_init.h"
#include "model_path.h"

// Define the app namespace to encapsulate the AppAudio class
namespace app
{
    // AppAudio class: Manages audio data capture, processing, and sound classification
    class AppAudio
    {
    private:
        // Indices for specific sounds detected by the classifier
        static constexpr int CRYING_IDX{0};
        static constexpr int NOISE_IDX{1};

        // Buffer size for audio samples and pointers to store audio and feature data
        static constexpr int AUDIO_BUFFER_SIZE{16000};
        float *m_audio_buffer;       // Buffer to store raw audio data
        float *m_features;           // Buffer to store extracted audio features
        int m_audio_buffer_ptr{0};   // Pointer to track the current position in the audio buffer
        std::mutex m_features_mutex; // Mutex to protect access to m_features

        // Task buffer sizes and static memory allocation for each task
        static constexpr int DETECT_TASK_BUFF_SIZE{4 * 1024};
        inline static uint8_t *m_detect_task_buffer;
        inline static StaticTask_t m_detect_task_data;

        static constexpr int ACTION_TASK_BUFF_SIZE{4 * 1024};
        inline static uint8_t *m_action_task_buffer;
        inline static StaticTask_t m_action_task_data;

        static constexpr int FEED_TASK_BUFF_SIZE{8 * 1024};
        inline static uint8_t m_feed_task_buffer[FEED_TASK_BUFF_SIZE];
        inline static StaticTask_t m_feed_task_data;

        // Pointers to audio front-end (AFE) handlers and data
        esp_afe_sr_iface_t *m_afe_handle{nullptr};
        esp_afe_sr_data_t *m_afe_data{nullptr};

        // Function to call when crying is detected, and variable to store crying state
        std::function<void(bool)> m_crying_fn;
        bool m_crying;

        // Task functions and default AFE configuration
        static void feedTask(void *arg);
        static void detectTask(void *arg);
        static void actionTask(void *arg);
        static afe_config_t defaultAfeConfig();

    public:
        // Initializes the audio system, configuring buffers, callbacks, and AFE setup
        void init(std::function<void(bool)> f)
        {
            // Store the crying detection callback function
            m_crying_fn = f;
            m_crying = false;

            // Allocate memory for the audio and feature buffers
            m_audio_buffer = new float[AUDIO_BUFFER_SIZE];
            m_features = new float[AUDIO_BUFFER_SIZE];

            // Allocate memory for detect and action task buffers
            m_detect_task_buffer = new uint8_t[DETECT_TASK_BUFF_SIZE];
            m_action_task_buffer = new uint8_t[ACTION_TASK_BUFF_SIZE];

            // Initialize the ESP32 board for audio processing
            esp_board_init(16000, 1, 16);

            // Set up the AFE handle and configuration for audio processing
            m_afe_handle = const_cast<esp_afe_sr_iface_t *>(&ESP_AFE_VC_HANDLE);
            afe_config_t afe_config = defaultAfeConfig();
            m_afe_data = m_afe_handle->create_from_config(&afe_config);
        }

        // Starts FreeRTOS tasks for feeding, detecting, and acting on audio data
        void start(void)
        {
            // Create the feed task on core 0 for audio data capture
            xTaskCreateStaticPinnedToCore(feedTask, "feed", FEED_TASK_BUFF_SIZE, this, 5, m_feed_task_buffer, &m_feed_task_data, 0);

            // Create the detect task on core 1 for audio feature extraction and buffering
            xTaskCreateStaticPinnedToCore(detectTask, "detect", DETECT_TASK_BUFF_SIZE, this, 5, m_detect_task_buffer, &m_detect_task_data, 1);

            // Create the action task on core 1 to classify and act on detected audio
            xTaskCreateStaticPinnedToCore(actionTask, "action", ACTION_TASK_BUFF_SIZE, this, 5, m_action_task_buffer, &m_action_task_data, 1);
        }
    };

    // feedTask: Captures audio data from the microphone and feeds it to the AFE for processing
    void AppAudio::feedTask(void *arg)
    {
        // Get instance of AppAudio
        AppAudio *obj{static_cast<AppAudio *>(arg)};

        // Set up buffer and parameters for audio data capture
        int audio_chunksize = obj->m_afe_handle->get_feed_chunksize(obj->m_afe_data);
        int feed_channel = esp_get_feed_channel();
        int16_t *i2s_buff = new int16_t[audio_chunksize * feed_channel];

        // Continuously capture and feed audio data to the AFE
        while (true)
        {
            esp_get_feed_data(false, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);
            obj->m_afe_handle->feed(obj->m_afe_data, i2s_buff);
        }
    }

    // detectTask: Fetches processed audio data from the AFE and updates the audio buffer
    void AppAudio::detectTask(void *arg)
    {
        AppAudio *obj{static_cast<AppAudio *>(arg)};
        int afe_chunksize{obj->m_afe_handle->get_fetch_chunksize(obj->m_afe_data)};

        // Infinite loop to continually fetch audio features and update buffer
        while (true)
        {
            // Fetch AFE data and handle errors
            afe_fetch_result_t *res = obj->m_afe_handle->fetch(obj->m_afe_data);
            if (res == nullptr || res->ret_value == ESP_FAIL)
            {
                continue;
            }

            // Populate the circular audio buffer with new data from AFE
            for (int i = 0; i < afe_chunksize; ++i)
            {
                obj->m_audio_buffer_ptr %= AUDIO_BUFFER_SIZE;
                obj->m_audio_buffer[obj->m_audio_buffer_ptr++] = res->data[i];
            }

            // Lock and update the feature buffer
            {
                std::lock_guard<std::mutex> guard(obj->m_features_mutex);
                for (int i = 0; i < AUDIO_BUFFER_SIZE; ++i)
                {
                    obj->m_features[i] = obj->m_audio_buffer[(obj->m_audio_buffer_ptr + i) % AUDIO_BUFFER_SIZE];
                }
            }
        }
    }

    // actionTask: Classifies audio data and triggers action if crying or noise is detected
    void AppAudio::actionTask(void *arg)
    {
        AppAudio *obj{static_cast<AppAudio *>(arg)};
        ei_impulse_result_t result = {nullptr};

        // Lambda to retrieve features for Edge Impulse classifier
        auto get_data_fn = [&obj](size_t offset, size_t length, float *out_ptr) -> int
        {
            memcpy(out_ptr, obj->m_features + offset, length * sizeof(float));
            return 0;
        };

        // Infinite loop to classify audio features and take action based on results
        while (true)
        {
            signal_t features_signal{get_data_fn, AUDIO_BUFFER_SIZE};
            int result_idx{NOISE_IDX};

            // Lock features, classify, and determine detected sound type
            {
                std::lock_guard<std::mutex> guard(obj->m_features_mutex);
                if (run_classifier(&features_signal, &result) == EI_IMPULSE_OK)
                {
                    for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; ++i)
                    {
                        if (result.classification[i].value > result.classification[result_idx].value)
                        {
                            result_idx = i;
                        }
                    }
                }
            }

            // Trigger actions based on the classification result (crying or noise)
            switch (result_idx)
            {
            case CRYING_IDX:
                if (!obj->m_crying)
                {
                    obj->m_crying_fn(true);  // Notify crying state
                    obj->m_crying = true;
                }
                break;
            case NOISE_IDX:
                if (obj->m_crying)
                {
                    obj->m_crying_fn(false); // Notify noise or no-crying state
                    obj->m_crying = false;
                }
                break;
            default:
                break;
            }

            // Delay between classifications to avoid frequent triggers
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Default AFE configuration: Sets default parameters for noise suppression, sample rate, etc.
    afe_config_t AppAudio::defaultAfeConfig()
    {
        afe_config_t afe_config = {.debug_hook = {{AFE_DEBUG_HOOK_MASE_TASK_IN, nullptr}, {AFE_DEBUG_HOOK_FETCH_TASK_IN, nullptr}}};

        afe_config.aec_init = false;
        afe_config.se_init = true;               // Enable noise suppression
        afe_config.vad_init = false;
        afe_config.wakenet_init = false;
        afe_config.voice_communication_init = true;
        afe_config.voice_communication_agc_init = false;
        afe_config.voice_communication_agc_gain = 15;
        afe_config.vad_mode = VAD_MODE_3;
        afe_config.wakenet_model_name = nullptr;
        afe_config.wakenet_model_name_2 = nullptr;
        afe_config.wakenet_mode = DET_MODE_2CH_90;
        afe_config.afe_mode = SR_MODE_LOW_COST;
        afe_config.afe_perferred_core = 0;
        afe_config.afe_perferred_priority = 5;
        afe_config.afe_ringbuf_size = 50;
        afe_config.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
        afe_config.afe_linear_gain = 1.0;
        afe_config.agc_mode = AFE_MN_PEAK_AGC_MODE_2;
        afe_config.debug_init = false;
        afe_config.afe_ns_mode = NS_MODE_SSP;
        afe_config.afe_ns_model_name = nullptr;

        afe_config.pcm_config.total_ch_num = 3;
        afe_config.pcm_config.mic_num = 2;
        afe_config.pcm_config.ref_num = 1;
        afe_config.pcm_config.sample_rate = 16000;

        afe_config.aec_init = false;
        return afe_config;
    }
}
