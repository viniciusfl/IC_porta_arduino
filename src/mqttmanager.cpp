static const char *TAG = "mqttman";

#include <tramela.h>

#include <Arduino.h>

#ifdef USE_SD
#include <SD.h>
#else
#include <FFat.h>
#endif

#include <mqtt_client.h>

#include <mqttmanager.h>
#include <networkmanager.h>
#include <dbmanager.h> // startDBDownload etc.
#include <cardreader.h> // openDoor etc.
#include <keys.h>

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

    // This is a "real" function (not a method) that hands
    // the received event over to the MqttManager object.
    void callback_handler(void *handler_args, esp_event_base_t base,
                          int32_t event_id, void *event_data) {

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
        esp_mqtt_client_register_event(client,
                                       (esp_mqtt_event_id_t) ESP_EVENT_ANY_ID,
                                       callback_handler, NULL);
        esp_mqtt_client_start(client);
    }


    inline bool MqttManager::sendLog(const char *filename) {
        File f = DISK.open(filename, "r");

        unsigned int fileSize = f.size();
        char* pBuffer = (char*)malloc(fileSize + 1);
        f.read((uint8_t*) pBuffer, fileSize);
        pBuffer[fileSize] = '\0';
        if (!esp_mqtt_client_enqueue(client, "/topic/logs", pBuffer, fileSize, 1, 0, 0)) {
            free(pBuffer);
            return false;
        }
        free(pBuffer);

        return true;
    }

    void MqttManager::treatCommands(const char* command) {
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
        esp_mqtt_client_subscribe(client, "/topic/commands", 2);

        if (sdPresent) {
            esp_mqtt_client_unsubscribe(client, "/topic/database");
            esp_mqtt_client_subscribe(client, "/topic/database", 2);
        }
    }

    void MqttManager::mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                                int32_t event_id, esp_mqtt_event_handle_t event) {
        esp_mqtt_client_handle_t client = event->client;
        switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            log_i("MQTT_EVENT_CONNECTED");
            serverStarted = true;
            esp_mqtt_client_subscribe(client, "/topic/commands", 2);
            if (sdPresent)
                esp_mqtt_client_subscribe(client, "/topic/database", 2);
            break;
        case MQTT_EVENT_DISCONNECTED:
            serverStarted = false;
            log_i("MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            log_i("MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            log_i("MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            log_i("MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            flushSentLogfile();
            break;
        case MQTT_EVENT_DATA:
            char buffer[100];
            snprintf(buffer, 100, "%.*s",  event->topic_len, event->topic);
            if (!strcmp(buffer, "/topic/commands")) {
                log_i("MQTT_EVENT_DATA from topic %s", buffer);
                snprintf(buffer, 100, "%.*s",  event->data_len, event->data);
                this->treatCommands(buffer);
            } else if (!strcmp(buffer, "/topic/database")) { /* Just for log purposes */
                log_i("MQTT_EVENT_DATA from topic %s, first data", buffer);
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
                log_i("MQTT_EVENT_DATA from /topic/database, last data");
                // TODO: what if downloading is aborted/fails?
                downloading = false;
                finishDBDownload();
            } else {
                log_d("MQTT_EVENT_DATA from database", buffer);
            }

            break;
        case MQTT_EVENT_ERROR:
            log_i("MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_i("-> ", event->error_handle->esp_tls_last_esp_err);
                log_i("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_i("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                log_i("Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;
        default:
            log_i("Other event id:%d", event->event_id);
            break;
        }
    }

    MqttManager mqttManager;
}


void initMqtt() { MQTT::mqttManager.init(); }

bool isClientConnected() { return MQTT::mqttManager.serverConnected(); }

bool sendLog(const char *filename) { return MQTT::mqttManager.sendLog(filename); };

void forceDBDownload() { MQTT::mqttManager.resubscribe(); }
