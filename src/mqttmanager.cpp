static const char *TAG = "mqttman";

#include <tramela.h>

#include <Arduino.h>

#include <mqtt_client.h>

#include <mqttmanager.h>
#include <networkmanager.h>
#include <dbmanager.h> // finishDBDownload etc.
#include <cardreader.h> // openDoor etc.
#include <keys.h>
#include <firmwareOTA.h>

namespace  MQTT {
    // This is a "real" function (not a method) that hands
    // the received event over to the MqttManager object.
    void callback_handler(void *handler_args, esp_event_base_t base,
                          int32_t event_id, void *event_data);

    class MqttManager {
    public:
        void init();
        inline bool serverConnected();
        inline bool sendLog(const char *logData, unsigned int len);
        void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                                int32_t event_id, esp_mqtt_event_handle_t event);
        void treatCommands(const char* command);
        inline void resubscribe();
    private: 
        bool serverStarted = false;
        int inTransitMessageIDs[5];
        int nextFreeID;
        void addMessageID(int);
        void removeMessageID(int);
        int findMessageID(int);
        void resetMessageList();
        int isDownloadingDB = 0; // 1 - downloading DB, 2 - downloading firmware 

        esp_mqtt_client_handle_t client;
    };


    inline bool MqttManager::serverConnected() {
        return serverStarted;
    }

    // This should be called from setup()
    inline void MqttManager::init() {
        resetMessageList();

        char buffer[50]; 
        snprintf(buffer, 50, "ESP_KEYLOCK_ID-%d", doorID);

        const esp_mqtt_client_config_t mqtt_cfg = {
            .host = "10.0.2.109",
            .port = 8883, 
            .client_id = buffer,
            .disable_clean_session = true,
            .keepalive = 180000, //FIXME:
            /* .cert_pem = brokerCert,
            .client_cert_pem = espCertPem,
            .client_key_pem = espCertKey,  
            .transport = MQTT_TRANSPORT_OVER_SSL, */
            .skip_cert_common_name_check = true, // FIXME:
        };

        client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(client,
                                       (esp_mqtt_event_id_t) ESP_EVENT_ANY_ID,
                                       callback_handler, NULL);
        esp_mqtt_client_start(client);
    }

    void MqttManager::resetMessageList() {
        nextFreeID = 0;
        for (int i = 0; i < 5; ++i) {inTransitMessageIDs[i] = -1;}
    }

    void MqttManager::addMessageID(int id) {
        if (findMessageID(id) >= 0) { return; }
        inTransitMessageIDs[nextFreeID++] = id;
    }

    void MqttManager::removeMessageID(int id) {
        int pos = findMessageID(id);
        if (pos < 0) { return; }
        for (int i = pos; i < 4; ++i) {
            inTransitMessageIDs[i] = inTransitMessageIDs[i+1];
        }
        inTransitMessageIDs[4] = -1;
        --nextFreeID;
    }

    int MqttManager::findMessageID(int id) {
        for (int i = 0; i < 5; ++i) {
            if (inTransitMessageIDs[i] == id) { return i; }
        }
        return -1;
    }

    inline bool MqttManager::sendLog(const char *logData, unsigned int len) {
        int result = esp_mqtt_client_enqueue(client,
                                    "/topic/logs",
                                    logData, len, 1, 0, 0);
        if (result > 0) {
            addMessageID(result);
            return true;
        }
        return false;
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
        while (!serverStarted) { checkDoor(); delay(100); currentMillis = millis();}

        esp_mqtt_client_unsubscribe(client, "/topic/commands");
        esp_mqtt_client_subscribe(client, "/topic/commands", 2);

        esp_mqtt_client_unsubscribe(client, "/topic/database");
        //esp_mqtt_client_subscribe(client, "/topic/database", 2);
    }

    void MqttManager::mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                                int32_t event_id, esp_mqtt_event_handle_t event) {

        esp_mqtt_client_handle_t client = event->client;
        switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            log_i("MQTT_EVENT_CONNECTED");
            serverStarted = true;
            esp_mqtt_client_subscribe(client, "/topic/commands", 2);
            // esp_mqtt_client_subscribe(client, "/topic/database", 2);
            esp_mqtt_client_subscribe(client, "/topic/firmware", 0);
            break;
        case MQTT_EVENT_DISCONNECTED:
            serverStarted = false;
            log_i("MQTT_EVENT_DISCONNECTED");
            cancelDBDownload();
            cancelLogUpload();
            resetMessageList();
            break;
        case MQTT_EVENT_SUBSCRIBED:
            log_i("MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            log_i("MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            log_i("MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            if (findMessageID(event->msg_id)) {
                removeMessageID(event->msg_id);
                flushSentLogfile();
            }
            break;
        case MQTT_EVENT_DATA:
            char buffer[100];
            snprintf(buffer, 100, "%.*s",  event->topic_len, event->topic);
            if (!strcmp(buffer, "/topic/commands")) {
                log_i("MQTT_EVENT_DATA from topic %s", buffer);
                snprintf(buffer, 100, "%.*s",  event->data_len, event->data);
                this->treatCommands(buffer);
            } else {
                // If it's not a command, it's a DB to download or a firmware update
                if (event->current_data_offset == 0) {
                    if (!strcmp(buffer, "/topic/firmware")) {
                        log_i("MQTT_EVENT_DATA from /topic/firmware -- full message");
                        isDownloadingDB = 0;
                    } else{
                        log_i("MQTT_EVENT_DATA from /topic/database -- full message");
                        isDownloadingDB = 1;
                    }
                    addMessageID(event->msg_id);
                }
                /* if (isDownloadingDB) {
                    if (event->current_data_offset == 0) {
                        log_i("MQTT_EVENT_DATA from topic/database -- first");
                    } else if (event->total_data_len
                                        - event->current_data_offset
                                        - event->data_len <= 0) {
                        log_i("MQTT_EVENT_DATA from /topic/database -- last");
                        removeMessageID(event->msg_id);
                        finishDBDownload();
                        isDownloadingDB = 0;
                    } else {
                        log_d("MQTT_EVENT_DATA from /topic/database -- ongoing");
                    }
                    return;
                } */

                writeToFile(event->data, event->data_len);
                if (event->current_data_offset == 0) {
                    log_i("MQTT_EVENT_DATA from topic/firmware -- first");
                } else if (event->total_data_len - event->current_data_offset - event->data_len <= 0) {
                    log_i("MQTT_EVENT_DATA from /topic/firmware -- last");
                    /* removeMessageID(event->msg_id); */
                    performUpdate();
                } else {
                    /* log_d("MQTT_EVENT_DATA from /topic/firmware -- ongoing %d", event->current_data_offset); */
                }
            }
            break;
        case MQTT_EVENT_ERROR:
            log_i("MQTT_EVENT_ERROR");
            // Handle MQTT connection problems
            cancelDBDownload();
            cancelLogUpload();
            resetMessageList();

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


    // This is a "real" function (not a method) that hands
    // the received event over to the MqttManager object.
    void callback_handler(void *handler_args, esp_event_base_t base,
                          int32_t event_id, void *event_data) {

        esp_mqtt_event_handle_t event = (esp_mqtt_event_t *) event_data;
        mqttManager.mqtt_event_handler(handler_args, base, event_id, event);
    }

}


void initMqtt() { MQTT::mqttManager.init(); }

bool isClientConnected() { return MQTT::mqttManager.serverConnected(); }

bool sendLog(const char *logData, unsigned int len) {
    return MQTT::mqttManager.sendLog(logData, len);
};

void forceDBDownload() { MQTT::mqttManager.resubscribe(); }
