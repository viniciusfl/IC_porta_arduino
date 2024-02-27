static const char *TAG = "mqttman";

#include <tramela.h>

#include <Arduino.h>

#include <mqtt_client.h>

#include <mqttmanager.h>
#include <networkmanager.h>
#include <dbmanager.h> // finishDBDownload etc.
#include <door.h>
#include <keys.h>
#include <firmwareOTA.h>

// Everytime we successfully connect to the broker (which happens on boot
// but also at other times due to network failures), we subscribe to the
// "firmware" and "commands" topics, as this is harmless (the previous
// subscription is dropped by the broker). However, we do not want to do
// that with the "database" topic, because when we subscribe we receive
// the DB file again (it is a retained message). So, we keep track of
// whether we have already subscribed at boot and only resubscribe if
// something fails and we want to download a fresh DB. This means we
// always receive a copy of the DB on boot (when we first subscribe),
// even if it is not necessary, but that is harmless.

namespace  MQTT {
    enum DownloadType { DB, FIRMWARE, NONE };

    // This is a "real" function (not a method) that hands
    // the received event over to the MqttManager object.
    void callback_handler(void *handler_args, esp_event_base_t base,
                          int32_t event_id, void *event_data);

    class MqttManager {
    public:
        inline void init();
        inline bool serverConnected();
        inline bool sendLog(const char *logData, unsigned int len);
        void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, esp_mqtt_event_handle_t event);
        void handleCommand(const char* command);
        inline void resubscribe();
    private: 
        enum DownloadType downloading; // DB, FIRMWARE, or NONE
        bool connected = false;
        bool subscribed = false;

        // We should never have more than one message in transit at any
        // given time, but let's be cautious.
        //
        // We keep track of in transit messages for two reasons:
        //
        // 1. So we only delete a logfile when we have confirmation
        //    it was received.
        // 2. Because large messages that are split in multiple separate
        //    blocks generate multiple events, but only the first one
        //    includes the topic, so we check this to make sure we are
        //    indeed working with a single file
        //
        // TODO: We are not really checking for 2 right now
        int inTransitMessageIDs[5];
        int nextFreeID;
        void rememberMessage(int);
        void forgetMessage(int);
        int findMessageID(int);
        void resetMessageList();

        esp_mqtt_client_handle_t client;
    };

    inline bool MqttManager::serverConnected() { return connected; }

    // This should be called from setup()
    inline void MqttManager::init() {
        resetMessageList();

        char buffer[50]; 
        snprintf(buffer, 50, "ESP_KEYLOCK_ID-%d", doorID);

        const esp_mqtt_client_config_t mqtt_cfg = {
            .host = "mosquito.ime.usp.br",
            .port = 8883, 
            .client_id = buffer,
            .disable_clean_session = true,
            .keepalive = 180000,
            .cert_pem = brokerCert,
            .client_cert_pem = espCertPem,
            .client_key_pem = espCertKey,  
            .transport = MQTT_TRANSPORT_OVER_SSL,
            .skip_cert_common_name_check=true,
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

    void MqttManager::rememberMessage(int id) {
        if (findMessageID(id) >= 0) { return; }
        inTransitMessageIDs[nextFreeID++] = id;
    }

    void MqttManager::forgetMessage(int id) {
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
        int result = esp_mqtt_client_enqueue(client, "/topic/logs",
                                             logData, len, 1, 0, 0);

        if (result > 0) {
            rememberMessage(result);
            return true;
        }
        return false;
    }

    void MqttManager::handleCommand(const char* command) {
        int slashpos;

        // "4" -> a "reasonable" max length for the door ID;
        //        either "all" or up to "999"
        for (slashpos = 0; slashpos < 4; ++slashpos) {
            if (command[slashpos] == 0) { slashpos = 4; }
            if (command[slashpos] == '/') { break; }
        }

        if (slashpos >= 4) {
            log_e("Invalid command: %s", command);
            return;
        }

        char recipient[4];
        strncpy(recipient, command, slashpos);
        char us[4];
        snprintf(us, 4, "%d", doorID);

        if (!strcmp(recipient, "all") or !strcmp(recipient, us)) {
            const char* actualCommand = command + slashpos +1;
            if (!strcmp(actualCommand, "openDoor")) {
                log_i("Received command to open door.");
                openDoor();
            } else if (!strcmp(actualCommand, "reboot")) {
                log_i("Received command to reboot ESP.");
                delay(2000); // time to flush pending logs
                esp_restart();
            } else {
                log_e("Invalid command: %s", actualCommand);
            }
        }
    }

    inline void MqttManager::resubscribe() {
        if (not subscribed) { return; }
        if (downloading == DB) { return; }
        if (serverConnected()) {
            esp_mqtt_client_subscribe(client, "/topic/database", 2);
        } else {
            subscribed = false;
        }
    }

    void MqttManager::mqtt_event_handler(void *handler_args,
                                    esp_event_base_t base, int32_t event_id,
                                    esp_mqtt_event_handle_t event) {

        esp_mqtt_client_handle_t client = event->client;

        switch ((esp_mqtt_event_id_t)event_id) {

        case MQTT_EVENT_CONNECTED:
            log_i("MQTT_EVENT_CONNECTED");
            connected = true;
            esp_mqtt_client_subscribe(client, "/topic/commands", 2);
            esp_mqtt_client_subscribe(client, "/topic/firmware", 2);
            if (not subscribed) {
                // Re-downloads the DB, because it is a retained message
                if (esp_mqtt_client_subscribe(client,
                                              "/topic/database", 2) > 0) {

                    subscribed = true;
                }
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            connected = false;
            log_i("MQTT_EVENT_DISCONNECTED");
            cancelDBDownload();
            cancelLogUpload();
            cancelFirmwareDownload();
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
            if (findMessageID(event->msg_id) >= 0) {
                forgetMessage(event->msg_id);
                flushSentLogfile();
            }
            break;

        case MQTT_EVENT_DATA:
            char buffer[100];
            snprintf(buffer, 100, "%.*s",  event->topic_len, event->topic);

            // Commands always fit in a single message
            if (!strcmp(buffer, "/topic/commands")) {
                log_i("MQTT_EVENT_DATA from topic %s", buffer);
                snprintf(buffer, 100, "%.*s",  event->data_len, event->data);
                this->handleCommand(buffer);
                break;
            }

            // File downloads are normally split into multiple "slices",
            // so they result in multiple events; when this happens, the
            // topic is only present in the first one, so we need to
            // remember it. We also remember the ID of the message being
            // received just to detect if some error causes a different
            // download to start.

            // The file was not split. This should not
            // really happen, but let's handle it anyway
            if (event->total_data_len == event->data_len) {
                if (!strcmp(buffer, "/topic/firmware")) {
                    log_i("MQTT_EVENT_DATA from /topic/firmware -- full message");
                    writeToFirmwareFile(event->data, event->data_len);
                    performFirmwareUpdate();
                } else {
                    log_i("MQTT_EVENT_DATA from /topic/database -- full message");
                    writeToDatabaseFile(event->data, event->data_len);
                    finishDBDownload();
                }

                break;
            }

            // First slice
            if (event->current_data_offset == 0) {
                if (!strcmp(buffer, "/topic/firmware")) {
                    downloading = FIRMWARE;
                    log_i("MQTT_EVENT_DATA from /topic/firmware -- first");
                } else {
                    downloading = DB;
                    log_i("MQTT_EVENT_DATA from /topic/database -- first");
                }
                rememberMessage(event->msg_id);
            }

            bool lastSlice;
            lastSlice = event->total_data_len - event->current_data_offset
                                              - event->data_len <= 0;

            if (downloading == FIRMWARE) {
                writeToFirmwareFile(event->data, event->data_len);
                if (lastSlice) {
                    log_i("MQTT_EVENT_DATA from /topic/firmware -- last");
                    performFirmwareUpdate();
                    downloading = NONE;
                    forgetMessage(event->msg_id);
                } else {
                    log_v("MQTT_EVENT_DATA from /topic/firmware -- ongoing %d",
                            event->current_data_offset);
                }
            } else {
                writeToDatabaseFile(event->data, event->data_len);
                if (lastSlice) {
                    log_i("MQTT_EVENT_DATA from /topic/database -- last");

                    finishDBDownload();
                    downloading = NONE;
                    forgetMessage(event->msg_id);
                } else {
                    log_v("MQTT_EVENT_DATA from /topic/database -- ongoing %d",
                            event->current_data_offset);
                }
            }

            break;

        case MQTT_EVENT_ERROR:
            log_i("MQTT_EVENT_ERROR");
            // Handle MQTT connection problems
            cancelDBDownload();
            cancelLogUpload();
            cancelFirmwareDownload();
            resetMessageList();

            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_i("-> ", event->error_handle->esp_tls_last_esp_err);
                log_i("   reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_i("   captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                log_i("   Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;

        case MQTT_EVENT_BEFORE_CONNECT:
            log_d("Ready to connect to mqtt broker");
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
