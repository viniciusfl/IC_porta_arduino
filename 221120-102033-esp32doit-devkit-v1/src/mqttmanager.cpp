static const char *TAG = "mqttman";

#include <common.h>
#include <mqtt_client.h>
#include <networkmanager.h>
#include <authorizer.h>
#include <dbmanager.h>
#include <keys.h>
#include <cardreader.h>
#include <SD.h>

namespace  MQTT {
    class MqttManager {
    public:
        void init();
        inline bool serverConnected();
        inline bool sendLog(const char *filename);
        void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                                int32_t event_id, esp_mqtt_event_handle_t event);
        void treatCommands(const char* command);
        inline void resubscribe();
    private: 
        bool downloading = false;
        bool serverStarted = false;

        esp_mqtt_client_handle_t client;
    };

    static void callback_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
        esp_mqtt_event_handle_t event = (esp_mqtt_event_t *) event_data;
        MqttManager *obj = (MqttManager *)event->user_context;
        obj->mqtt_event_handler(handler_args, base, event_id, event);
    }

    inline bool MqttManager::serverConnected() {
        return serverStarted;
    }

    // This should be called from setup()
    inline void MqttManager::init() {
        char buffer[50]; 
        snprintf(buffer, 50, "ESP_KEYLOCK_ID-%d", doorID);

        const esp_mqtt_client_config_t mqtt_cfg = {
            .host = "10.0.2.109",
            .port = 8883, 
            .client_id = buffer,
            .disable_clean_session = true,
            .keepalive = 180000, //FIXME:
            .user_context = this,
            .cert_pem = brokerCert,
            .client_cert_pem = espCertPem,
            .client_key_pem = espCertKey,  
            .transport = MQTT_TRANSPORT_OVER_SSL,
            .skip_cert_common_name_check = true, // FIXME:
        };

        client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t) ESP_EVENT_ANY_ID, callback_handler, NULL);
        esp_mqtt_client_start(client);
    }


    inline bool MqttManager::sendLog(const char *filename) {
        File f = SD.open(filename, "r");

        unsigned int fileSize = f.size();
        char* pBuffer = (char*)malloc(fileSize + 1);
        f.read((uint8_t*) pBuffer, fileSize);
        pBuffer[fileSize] = '\0';
        if (!esp_mqtt_client_enqueue(client, "/topic/sendLogs", pBuffer, fileSize, 1, 0, 0)) {
            free(pBuffer);
            return false;
        }
        free(pBuffer);

        return true;
    }

    void MqttManager::treatCommands(const char* command) {
        // QUESTION: Should i return an anwser in another topic?
        if (!strcmp(command, "openDoor")) {
            log_v("Received command to open door.");
            openDoorCommand();
        } else if (!strcmp(command, "reboot")) {
            log_v("Received command to reboot ESP.");
            ESP.restart();
        } else {
            log_e("Invalid command: %s", command);
        }
    }

    inline void MqttManager::resubscribe() {
        while (!serverStarted) { delay(1000); }

        esp_mqtt_client_unsubscribe(client, "/topic/commands");
        esp_mqtt_client_unsubscribe(client, "/topic/database");
        esp_mqtt_client_subscribe(client, "/topic/commands", 2);
        esp_mqtt_client_subscribe(client, "/topic/database", 2);
    }

    void MqttManager::mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                                int32_t event_id, esp_mqtt_event_handle_t event) {
        esp_mqtt_client_handle_t client = event->client;
        switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            serverStarted = true;
            esp_mqtt_client_subscribe(client, "/topic/commands", 2);
            esp_mqtt_client_subscribe(client, "/topic/database", 2);
            break;
        case MQTT_EVENT_DISCONNECTED:
            serverStarted = false;
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            flushSentLogfile();
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            char buffer[100];
            snprintf(buffer, 100, "%.*s",  event->topic_len, event->topic);
            log_v("Received message from topic: %s", buffer);
            if (!strcmp(buffer, "/topic/commands")) {
                snprintf(buffer, 100, "%.*s",  event->data_len, event->data);
                this->treatCommands(buffer);
            }
            // If is not a message to command topic, then it means we are downloading the DB
            if (!downloading) {
                if (!startDBDownload()) {
                    // TODO: Do something smart here
                    log_w("Cannot start download!");
                }
                downloading = true;
            }
            writeToDatabaseFile(event->data, event->data_len);
            if (event->total_data_len - event->current_data_offset - event->data_len <= 0){
                // TODO: what if downloading is aborted/fails?
                downloading = false;
                finishDBDownload();
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGI(TAG, "-> ", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGI(TAG, "reported from tls stack", event->error_handle->esp_tls_stack_err);
                ESP_LOGI(TAG, "captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

            }
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
        }
    }

    MqttManager mqttManager;
}


void initMqtt() { MQTT::mqttManager.init(); }

bool isClientConnected() { return MQTT::mqttManager.serverConnected(); }

bool sendLog(const char *filename) { return MQTT::mqttManager.sendLog(filename); };

void forceDBDownload() { MQTT::mqttManager.resubscribe(); }
