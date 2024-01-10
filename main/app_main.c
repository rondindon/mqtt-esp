#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "stdio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "time.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "driver/ledc.h"

static const char *TAG = "MQTT_EXAMPLE";

#define     led                 15
#define     BUTTON_PIN          5
#define     TOGGLE_TOPIC        "btn-toggle"
#define     INTENSITY_TOPIC     "intensity-slider"
#define     POT_CHANNEL         ADC_CHANNEL_0
#define     POT_MAX             4095  // Maximum ADC value
#define     LED_PWM_CHANNEL     LEDC_CHANNEL_0
#define     LEDC_TIMER          LEDC_TIMER_0
#define     LEDC_FREQ_HZ        5000  // Set your desired frequency

static char receivedMessage[32];

bool toggleState = false;
bool potentiometer_changed_manually = false;

static uint32_t previous_intensity = UINT32_MAX;
static uint32_t last_intensity = 0;
static uint32_t mqtt_desired_intensity = UINT32_MAX;

void board_reset(){
    gpio_reset_pin(led);
    gpio_reset_pin(BUTTON_PIN);

    gpio_set_direction(led, GPIO_MODE_OUTPUT);

    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

void sendToggleMessage(esp_mqtt_client_handle_t client) {
    toggleState = !toggleState;
    const char *message = toggleState ? "ON" : "OFF";
    int msg_id = esp_mqtt_client_publish(client, TOGGLE_TOPIC, message, 0, 1, 0);
    ESP_LOGI(TAG, "Sent publish successful, msg_id=%d", msg_id);
}

void configure_led_pwm() {
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQ_HZ,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num = led,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LED_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
    };
    ledc_channel_config(&ledc_channel);

    printf("LED PWM configuration complete\n");
}

void set_led_intensity(uint32_t intensity) {
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LED_PWM_CHANNEL, intensity);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LED_PWM_CHANNEL);
}

void send_intensity_to_mqtt(esp_mqtt_client_handle_t client, uint32_t intensity) {
    // Update the desired intensity variable
    mqtt_desired_intensity = intensity;

    char message[10];
    snprintf(message, sizeof(message), "%lu", (unsigned long)intensity);
    esp_mqtt_client_publish(client, INTENSITY_TOPIC, message, 0, 1, 0);
}

void button_task(void *pvParameter) {
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)pvParameter;

    while (1) {
        if (gpio_get_level(BUTTON_PIN) == 0) {
            ESP_LOGI(TAG, "Button Pressed!");

            // Use the stored message
            char *message = receivedMessage;

            if (strcmp(message, "ON") == 0) {
                toggleState = true;

            } else if (strcmp(message, "OFF") == 0) {
                toggleState = false;
                set_led_intensity(0);
            }

            sendToggleMessage(client);
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Debounce delay
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);  // Adjust the delay as needed
    }
}

void update_led_intensity(esp_mqtt_client_handle_t client, uint32_t intensity) {
    // Set LED intensity only if the LED is ON
    if (toggleState) {
        set_led_intensity(intensity);

        // Check if the intensity change is manual or from MQTT
        if (potentiometer_changed_manually) {
            // Send intensity to MQTT topic only if changed manually
            send_intensity_to_mqtt(client, intensity);

            // Reset the flag
            potentiometer_changed_manually = false;
        }
    } else {
        // If LED is OFF, update the desired intensity for future use
        mqtt_desired_intensity = intensity;

        last_intensity = intensity;
    }
}

void potentiometer_task(void *pvParameters) {
    esp_mqtt_client_handle_t mqtt_client = (esp_mqtt_client_handle_t)pvParameters;

    // Initialize LED PWM
    configure_led_pwm();

    // Configure ADC for potentiometer
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(POT_CHANNEL, ADC_ATTEN_DB_11);

    uint32_t previous_pot_value = UINT32_MAX;

    while (1) {
        // Read potentiometer value
        uint32_t pot_value = adc1_get_raw(POT_CHANNEL);
        
        // Map potentiometer value to LED intensity
        uint32_t intensity = (pot_value * 4095) / POT_MAX;

        // Check if intensity has changed
        if (abs(pot_value - previous_intensity) > 10) {
            //printf("SSSSS");
            // Update the LED intensity
            send_intensity_to_mqtt(mqtt_client, intensity);
            update_led_intensity(mqtt_client, intensity);
        }

        // Update the previous intensity value
        previous_intensity = intensity;

        // Update the previous potentiometer value
        previous_pot_value = pot_value;
        

        vTaskDelay(pdMS_TO_TICKS(500));  // Adjust delay as needed
    }
}



static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:

        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        msg_id = esp_mqtt_client_publish(client, "btn-toggle", "data_btn", 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_publish(client, "intensity-slider", "data_METER", 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "btn-toggle", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "intensity-slider", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED");

        msg_id = esp_mqtt_client_publish(client, "btn-toggle", "OFF", 0, 0, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_publish(client, "intensity-slider", "0", 0, 0, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:

        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        snprintf(receivedMessage, sizeof(receivedMessage), "%.*s", event->data_len, event->data);

        if (strncmp(event->topic, "intensity-slider", event->topic_len) == 0 && toggleState) {
            // Parse the intensity value from the received message
            uint32_t intensity = atoi(receivedMessage);

            // Update the LED intensity
            update_led_intensity(client, intensity);
        } else if (strncmp(event->topic, "btn-toggle", event->topic_len) == 0) {
            if (strncmp(event->data, "ON", event->data_len) == 0) {
                toggleState = true;
                printf("Turning LED ON\n");
                gpio_set_level(led, 1);

                // When turning on the LED, set the last intensity
                set_led_intensity(last_intensity);

            } else if (strncmp(event->data, "OFF", event->data_len) == 0) {
                toggleState = false;
                printf("Turning LED OFF\n");
                gpio_set_level(led, 0);

                set_led_intensity(0);
            }
        }

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    // Start the button task and pass the MQTT client handle
    xTaskCreate(&button_task, "button_task", configMINIMAL_STACK_SIZE * 3, (void *)client, 5, NULL);
    xTaskCreate(potentiometer_task, "potentiometer_task", 2048, client, 10, NULL);
}

void app_main(void) {
    board_reset();
    gpio_reset_pin(led);
    gpio_set_direction(led, GPIO_MODE_OUTPUT);
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    mqtt_app_start();
}