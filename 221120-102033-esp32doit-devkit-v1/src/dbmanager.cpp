static const char *TAG = "dbman";

#include <common.h>

#include <mqtt_client.h>

#include "SPI.h"
#include "SD.h"
#include <sqlite3.h> // Just to check for SQLITE_OK

#ifdef USE_SOCKETS
#include <WiFi.h>
#else
#include "esp_tls.h"
// TODO: I think we do not need these, we should check
//#include "esp_system.h"
//#include "esp_event.h"
//#include "esp_netif.h"
//#include "esp_crt_bundle.h"
#endif

#include <networkmanager.h>
#include <authorizer.h>
#include <dbmanager.h>
#include <keys.h>

#define MAX_HTTP_RECV_BUFFER 512

#define RETRY_DOWNLOAD_TIME 60000

#define DOWNLOAD_INTERVAL 180000

namespace DBNS {
    // This class periodically downloads a new version of the database
    // and sets this new version as the "active" db file in the Authorizer
    // when downloading is successful. It alternates between two filenames
    // to do its thing and, during startup, identifies which of them is
    // the current one by means of the "timestamp" file.
    class UpdateDBManager {
    public:
        inline void init();
        void update();
        inline void startUpdate();
        void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                                int32_t event_id, esp_mqtt_event_handle_t event);

    private:
        const char *currentFile;
        const char *otherFile;
        const char *currentTimestampFile;
        const char *otherTimestampFile;

        inline void chooseInitialFile();
        void swapFiles();
        bool checkFileFreshness(const char *);

        const char *SERVER = "10.0.2.106";
        unsigned long lastDownloadTime = 0;

        bool downloadingDB = false;
        void startDBDownload();
        inline bool finishDBDownload();

        bool startDownload(const char*);
        inline void processCurrentDownload();
        inline bool finishCurrentDownload(const char* topic);
        inline bool activateNewDBFile();
        bool finishedDownload = false;

        File file;
        esp_mqtt_client_handle_t client;
        bool serverStarted = false;

        bool sendLogs = false;
        bool openDoor = false;
    };  

    static void callback_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
        esp_mqtt_event_handle_t event = (esp_mqtt_event_t *) event_data;
        UpdateDBManager *obj = (UpdateDBManager *)event->user_context;
        obj->mqtt_event_handler(handler_args, base, event_id, event);
    }

    // This should be called from setup()
    inline void UpdateDBManager::init() {
        const esp_mqtt_client_config_t mqtt_cfg = {
            .host = "10.0.2.109",
            .port = 8883, 
            .user_context = this,
            .cert_pem = (const char*) brokerCert,
            .client_cert_pem = espCertPem,
            .client_key_pem = espCertKey,  
            .transport = MQTT_TRANSPORT_OVER_SSL,
            .skip_cert_common_name_check = true,
        };

        ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
        client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t) ESP_EVENT_ANY_ID, callback_handler, NULL);
        esp_mqtt_client_start(client);

        if (sdPresent) {
            chooseInitialFile();
        }
    }

    inline void UpdateDBManager::startUpdate() {
        if (connected() && serverStarted)
            startDBDownload();
    }

    // This should be called from loop()
    // At each call, we determine the current state we are in, perform
    // a small chunk of work, and return. This means we do not hog the
    // processor and can pursue other tasks while updating the DB.
    void UpdateDBManager::update() {
        if (!sdPresent || !connected() || !serverStarted)
            return;
        // We start a download only if we are not already downloading
        if (!downloadingDB) {
            // millis() wraps every ~49 days, but
            // wrapping does not cause problems here
            // TODO: we might prefer to use dates,
            //       such as "at midnight"
            //
            // ANWSER: we can use an alarm with RTC 1307
            // https://robojax.com/learn/arduino/?vid=robojax_DS1307-clock-alarm
            // I believe the nodeMCU RTC might also have this feature

            if (currentMillis - lastDownloadTime > DOWNLOAD_INTERVAL) {
                startDBDownload();
            }
            return;
        }
        // If we did not return above, we are still downloading the DB

        // If we finished downloading the DB, check whether the
        // download was successful; if so and the hash matches,
        // start using the new DB
        if (finishedDownload) {
            if (finishDBDownload()) {
                // Both downloads successful, update the timestamp
                if (activateNewDBFile()) {
                    lastDownloadTime = currentMillis;
                }
            }
        } else {
            // still downloading the DB
            processCurrentDownload();
        }
    }

    void UpdateDBManager::startDBDownload() {
        if (!sdPresent) 
            return;

        log_v("Starting DB download");
        downloadingDB = true;

        if (!connected() || !serverStarted) {
            lastDownloadTime += RETRY_DOWNLOAD_TIME;
            log_i("Internet failure, cancelling DB download");
            return;
        }

        SD.remove(otherTimestampFile);
        SD.remove(otherFile);

        file = SD.open(otherFile, FILE_WRITE);
        if(!file) {
            log_e("Error openning file, cancelling DB Download.");
            return;
        }
        log_v("Writing to %s", otherFile);

        startDownload("database");
        log_v("Started DB download");
    }

    inline bool UpdateDBManager::finishDBDownload() {
        downloadingDB = false;

        bool finishedOK = finishCurrentDownload("/topic/database");

        log_v("DB download finished, disconnecting from server.");

        return finishedOK;
    }

    bool UpdateDBManager::startDownload(const char *filename) {
        char buffer[50];
        sprintf(buffer, "/topic/%s", filename);
        esp_mqtt_client_subscribe(client, buffer, 0);

        return true;
    }

    inline bool UpdateDBManager::finishCurrentDownload(const char* topic) {
        log_d("Disconnecting from DB topic...");
        esp_mqtt_client_unsubscribe(client, topic); 
        file.close();
        return true;
    }

    void UpdateDBManager::mqtt_event_handler(void *handler_args, esp_event_base_t base, 
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
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            if (event->topic == "/topic/getLogs") {
                sendLogs = true;
                return;
            } else if (event->topic == "/topic/openDoor") {
                openDoor = true;
                return;
            } 
            // If topic isn't the ones above, then we are downloading DB
            // NOTE (maybe FIXME): When downloading retained messages, just the first
            // block of data comes with "topic", and the others blocks have empty topic.    
            file.write((byte *)event->data, event->data_len);
            if (event->total_data_len - event->current_data_offset - event->data_len <= 0){
                finishedDownload = true;
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

    inline void UpdateDBManager::processCurrentDownload() {
    }

    inline bool UpdateDBManager::activateNewDBFile() {
        bool ok = true;
        swapFiles();
        closeDB();

        if (openDB(currentFile) != SQLITE_OK) {
            ok = false;
            log_w("Error opening the updated DB, reverting to old one");
            while(true){

            }
            swapFiles();
            if (openDB(currentFile) != SQLITE_OK) { // FIXME: in the unlikely event that this fails too, we are doomed
                // TODO:
            }
        }
    

        if (!ok) {
            lastDownloadTime += RETRY_DOWNLOAD_TIME;
        }
        return ok;
    }

    void UpdateDBManager::swapFiles() {
        const char *tmp = currentFile;
        currentFile = otherFile;
        otherFile = tmp;
        tmp = currentTimestampFile;
        currentTimestampFile = otherTimestampFile;
        otherTimestampFile = tmp;

        // If we crash before these 5 lines, on restart we will continue
        // using the old version of the DB; if we crash after, we will
        // use the new version of the DB. If we crash in the middle (with
        // both files containing "1"), on restart we will use "bancoA.db",
        // which may be either.
        file = SD.open(currentTimestampFile, FILE_WRITE);
        file.print(1);
        file.close();

        file = SD.open(otherTimestampFile, FILE_WRITE);
        file.print(0);
        file.close();
    }

    bool UpdateDBManager::checkFileFreshness(const char *tsfile) {
        // In some exceptional circumstances, we might end up writing
        // "1" to the file more than once; that's ok, 11 > 0 too :) .
        File f = SD.open(tsfile);

        if (!f) return false;

        int t = 0;
        if (f.available()) {
            t = f.parseInt(); // If reading fails, this returns 0
        }
        f.close();
        return t > 0;
    }

    inline void UpdateDBManager::chooseInitialFile() {
        if (!sdPresent)
            return;

        START_OF_FUNCTION: // We will use goto further below
        currentFile = "/bancoA.db";
        otherFile = "/bancoB.db";
        currentTimestampFile = "/TSA.TXT";
        otherTimestampFile = "/TSB.TXT";
        bool dbFileOK = false;

        while (!dbFileOK) {
            if (checkFileFreshness(currentTimestampFile)) {
                dbFileOK = true;
            } else if (checkFileFreshness(otherTimestampFile)) {
                swapFiles();
                dbFileOK = true;
            } else {
                if (downloadingDB) {
                    update();
                } else if (connected() && serverStarted) {
                    startDBDownload();
                }
            }
        }

        // If we were forced to download a new file above, it was already
        // opened by update(), but there is no harm in doing it here again
        log_d("Choosing %s as current DB.\n", currentFile);
        if (openDB(currentFile) != SQLITE_OK) {
            log_e("Something bad happen with the DB file! "
                  "Downloading a fresh one");

            // start over
            closeDB();
            SD.remove(currentFile);
            SD.remove(otherFile);
            SD.remove(currentTimestampFile);
            SD.remove(otherTimestampFile);
            goto START_OF_FUNCTION;
        }
    }

    UpdateDBManager updateDBManager;
}
    

void initDBMan() { DBNS::updateDBManager.init(); }

void updateDB() { DBNS::updateDBManager.update(); }

void startUpdateDB() { DBNS::updateDBManager.startUpdate(); }
