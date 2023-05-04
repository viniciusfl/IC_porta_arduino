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
        bool startDBDownload();
        inline void finishDBDownload();
        inline bool finishedDownload();
        inline bool serverConnected();
        inline bool sendLog(const char *filename);
        void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                                int32_t event_id, esp_mqtt_event_handle_t event);
    private: 
        bool serverStarted = false;

        bool didDownloadFinish = false;
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

    inline bool MqttManager::finishedDownload() {
        return didDownloadFinish;
    }

    // This should be called from setup()
    inline void MqttManager::init() {
        // ESP's id in MQTT connection
        char buffer[50]; 
        sprintf(buffer, "ESP_KEYLOCK_ID-%d", doorID); // FIXME: I don't know what to put here. 

        const esp_mqtt_client_config_t mqtt_cfg = {
            .host = "10.0.2.109",
            .port = 8883, 
            .client_id = buffer,
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

    inline bool MqttManager::startDBDownload() {
        if (!esp_mqtt_client_subscribe(client, "/topic/database", 0)) return false;

        didDownloadFinish = false;
        return true;
    }

    inline void MqttManager::finishDBDownload() {
        // Should i test fail on this?
        esp_mqtt_client_unsubscribe(client, "/topic/database");
        log_d("DB download finished, disconnecting from server.");
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

    void MqttManager::mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                                int32_t event_id, esp_mqtt_event_handle_t event) {
        esp_mqtt_client_handle_t client = event->client;
        switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            serverStarted = true;
            esp_mqtt_client_subscribe(client, "/topic/getLogs", 0);
            esp_mqtt_client_subscribe(client, "/topic/openDoor", 0);
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
            if (event->topic == "/topic/commands") {
                // do something
                printf("Commands");
            }

            // If is not command topic, then it means we are downloading the DB:
            writeToDatabaseFile(event->data, event->data_len);
            if (event->total_data_len - event->current_data_offset - event->data_len <= 0){
                didDownloadFinish = true;
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

bool startDownload() { return MQTT::mqttManager.startDBDownload(); }

void finishDownload() { MQTT::mqttManager.finishDBDownload(); }

bool didDownloadFinish() { return MQTT::mqttManager.finishedDownload(); }

bool isClientConnected() { return MQTT::mqttManager.serverConnected(); }

bool sendLog(const char *filename) { return MQTT::mqttManager.sendLog(filename); };