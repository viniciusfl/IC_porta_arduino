static const char *TAG = "dbman";

#include <common.h>

#include "SPI.h"
#include "SD.h"
#include <sqlite3.h> // Just to check for SQLITE_OK

#include "mbedtls/md.h" // SHA256

#ifdef USE_SOCKETS
#include <WiFi.h>
#else
#include "esp_http_client.h"
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

#define DOWNLOAD_INTERVAL 30000

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

#       ifndef USE_SOCKETS
        esp_err_t processCallback(esp_http_client_event_t*);
#       endif
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

        bool downloadingHash = false;
        inline void startHashDownload();
        inline bool finishHashDownload();

        bool startDownload(const char*);
        inline void processCurrentDownload();
        inline bool finishCurrentDownload();
        inline bool downloadEnded();
        inline bool activateNewDBFile();

        char *currentHash;
        char calculatedHash[65];
        char *downloadedHash;
        inline bool verifyHash();
        inline bool hashChanged();
        mbedtls_md_context_t ctx;
        mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

        File file;
#       ifdef USE_SOCKETS
        WiFiClient netclient;
#       else
        esp_http_client_handle_t httpclient;
#       endif
    };

    // This should be called from setup()
    inline void UpdateDBManager::init() {
        chooseInitialFile();
    }

    // This should be called from loop()
    // At each call, we determine the current state we are in, perform
    // a small chunk of work, and return. This means we do not hog the
    // processor and can pursue other tasks while updating the DB.
    void UpdateDBManager::update() {

        // We start a download only if we are not already downloading
        if (!downloadingDB && !downloadingHash) {
            // millis() wraps every ~49 days, but
            // wrapping does not cause problems here
            // TODO: we might prefer to use dates,
            //       such as "at midnight"
            //
            // ANWSER: we can use an alarm with RTC 1307
            // https://robojax.com/learn/arduino/?vid=robojax_DS1307-clock-alarm
            // I believe the nodeMCU RTC might also have this feature

            if (currentMillis - lastDownloadTime > DOWNLOAD_INTERVAL) {
                startHashDownload();
            }
            return;
        }

        // If we did not return above, we are currently downloading something
        // (either the DB or the hash)

        if (downloadingHash) {
            // If we finished downloading the hash, check whether the
            // download was successful;
            if(downloadEnded()){
                if (finishHashDownload()) {
                    // If old DB hash and current DB hash matches,
                    // it means DB didn't change and we don't need to update
                    if (hashChanged()){
                        startDBDownload();
                    } else {
                        lastDownloadTime = currentMillis;
                    }
                }
            } else {
                // still downloading the hash
                processCurrentDownload();
            }
            return;
        }

        // If we did not return above, we are still downloading the DB

        // If we finished downloading the DB, check whether the
        // download was successful; if so and the hash matches,
        // start using the new DB
        if (downloadEnded()) {
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
        log_v("Starting DB download");

        calculatedHash[0] = 0;

        if (!startDownload("/dataBaseIME.db")) {
            log_i("Network failure, cancelling DB download");
            lastDownloadTime += RETRY_DOWNLOAD_TIME;
            return;
        }
        log_v("Started DB download");
        downloadingDB = true;

        // remove old DB files
        SD.remove(otherTimestampFile);
        SD.remove(otherFile);

        file = SD.open(otherFile, FILE_WRITE);
        if(!file) {
            log_e("Error openning file.");
        }
        log_v("Writing to %s", otherFile);

        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
        mbedtls_md_starts(&ctx);
    }

    inline void UpdateDBManager::startHashDownload() {

        if (!startDownload("/hash")) {
            log_i("Network failure, cancelling hash download");
            lastDownloadTime += RETRY_DOWNLOAD_TIME;
            return;
        }
        log_v("Started hash download");
        downloadingHash = true;

        if (SD.exists("/hash")) {
            file = SD.open("/hash");
            int fileSize = file.size();
            currentHash = (char *)malloc(65);
            currentHash[64] = '\0';
            file.readBytes(currentHash, 65);
            file.close();
        } else {
            log_v("old hash doesnt exist");
            currentHash = "xd";
        }

        SD.remove("/serverhash");
        file = SD.open("/serverhash", FILE_WRITE);
        if (!file) {
            log_e("Error openning file.");
        }

        log_v("Writing to /serverhash");
    }

    inline bool UpdateDBManager::finishDBDownload() {
        downloadingDB = false;

        bool finishedOK = finishCurrentDownload();

        byte buf[32];
        mbedtls_md_finish(&ctx, buf);
        mbedtls_md_free(&ctx);

        if (finishedOK) {
            log_v("DB download finished, disconnecting from server.");
            for (int i = 0; i < 32; ++i) {
                snprintf(calculatedHash + 2*i, 3, "%02hhx", buf[i]);
            }
        } else {
            log_v("DB download finished with error, ignoring file.");
            lastDownloadTime += RETRY_DOWNLOAD_TIME;
        }

        return finishedOK;
    }

    inline bool UpdateDBManager::finishHashDownload() {
        downloadingHash = false;
        bool finishedOK = finishCurrentDownload();
        file.close();

        if (finishedOK) {
            log_v("Hash download finished, disconnecting from server.");
            file = SD.open("/serverhash");
            downloadedHash = (char *)malloc(65);
            file.readBytes(downloadedHash, 65);
            downloadedHash[64] = '\0';
            file.close();
        } else {
            log_v("Hash download finished with error, ignoring file.");
            lastDownloadTime += RETRY_DOWNLOAD_TIME;
        }

        // SD.remove("/serverhash");

        return finishedOK;
    }

#   ifdef USE_SOCKETS

    bool UpdateDBManager::startDownload(const char* filename) {
        // If WiFI is disconnected, pretend nothing
        // ever happened and try again later
        if (!connected()) {
            log_i("No internet available.");
            return false;
        }

        netclient.connect(SERVER, 80);

        // If connection failed, pretend nothing
        // ever happened and try again later
        if (!netclient.connected()) {
            log_i("Connection failed.");
            netclient.stop();
            return false;
        }

        log_v("Connected to server.");

        return true;
    }

    inline bool UpdateDBManager::finishCurrentDownload() {
        netclient.flush();
        netclient.stop();
        file.close();
        // we do not really check whether the download succeeded here
        return true;
    }

#   else

    // The callback for the HTTP client
    esp_err_t handler(esp_http_client_event_t *evt) {
        UpdateDBManager *obj = (UpdateDBManager *)evt->user_data;
        return obj->processCallback(evt);
    }

    bool UpdateDBManager::startDownload(const char *filename) {
        // If WiFI is disconnected, pretend nothing
        // ever happened and try again later
        if (!connected()) {
            log_i("No internet available.");
            return false;
        }

        esp_http_client_config_t config = {
            .host = "10.0.2.106",
            .port = 443,
            .path = filename,
            .cert_pem = rootCA,
            .client_cert_pem = clientCertPem,
            .client_key_pem = clientKeyPem,
            .method = HTTP_METHOD_GET,
            .timeout_ms = 60000,
            .event_handler = handler,
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .buffer_size = 4096,
            .user_data = this,
            .is_async = true,
            .skip_cert_common_name_check = true,
        };

        httpclient = esp_http_client_init(&config);

        esp_err_t err = esp_http_client_perform(httpclient);

        if (err != ESP_ERR_HTTP_EAGAIN and err != ESP_OK) {
            // Connection failed. No worries, just pretend
            // nothing ever happened and try again later
            log_i("Network connection failed.");
            esp_http_client_cleanup(httpclient);
            return false;
        }

        // Download started ok! Normally "err" should be EAGAIN,
        // meaning the download started but has not yet finished,
        // but maybe the file is very small and download ended
        // already. If this happens, we do nothing special here.

        return true;
    }

    inline bool UpdateDBManager::finishCurrentDownload() {
        bool finishedOK = true;
        if (!esp_http_client_is_complete_data_received(httpclient)) {
            finishedOK = false;
        };

        file.close();
        esp_http_client_cleanup(httpclient);

        return finishedOK;
    }

    esp_err_t UpdateDBManager::processCallback(esp_http_client_event_t *evt) {
        const char *TAG = "http_callback";
        switch (evt->event_id) {
            case HTTP_EVENT_ERROR:
                log_i("HTTP_EVENT_ERROR");
                break;
            case HTTP_EVENT_ON_CONNECTED:
                log_d("HTTP_EVENT_ON_CONNECTED");
                break;
            case HTTP_EVENT_HEADER_SENT:
                log_v("HTTP_EVENT_HEADER_SENT");
                break;
            case HTTP_EVENT_ON_HEADER:
                log_v("HTTP_EVENT_ON_HEADER, key=%s, value=%s",
                    evt->header_key, evt->header_value);
                break;
            case HTTP_EVENT_ON_DATA:
                log_v("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
                if (!esp_http_client_is_chunked_response(evt->client)) {
                    file.write((byte *)evt->data, evt->data_len);
                    if (downloadingDB) {
                        mbedtls_md_update(&ctx,
                                        (const unsigned char *)evt->data,
                                        evt->data_len);
                    }
                } else {
                    log_e("CHUNKED!");
                }
                break;
            case HTTP_EVENT_ON_FINISH:
                log_d("HTTP_EVENT_ON_FINISH");
                break;
            case HTTP_EVENT_DISCONNECTED:
                log_i("HTTP_EVENT_DISCONNECTED");
                int mbedtls_err = 0;
                esp_err_t err = esp_tls_get_and_clear_last_error(
                    (esp_tls_error_handle_t)evt->data,
                    &mbedtls_err, NULL);
                if (err != 0) {
                    log_i("Last esp error code: 0x%x", err);
                    log_i("Last mbedtls failure: 0x%x", mbedtls_err);
                }
            break;
            // case HTTP_EVENT_REDIRECT:
            //     log_d("HTTP_EVENT_REDIRECT");
            //     break;
        }
        return ESP_OK;
    }

#endif

    inline void UpdateDBManager::processCurrentDownload() {
        // With http, all work is done by the callback function
#       ifdef USE_SOCKETS
        byte buf[512];
        int avail = netclient->available();
        if (avail <= 0) {
            return;
        }
        if (avail > 512) {
            avail = 512;
        }

        int check = netclient.read(buf, avail);
        if (check != avail) {
            log_i("Something bad happened reading from network");
        }
        file.write(buf, avail);
#endif
    }

    inline bool UpdateDBManager::downloadEnded() {
#       ifdef USE_SOCKETS
        if (!netclient.available() && !netclient.connected()) {
#       else
        esp_err_t err = esp_http_client_perform(httpclient);
        if (err != ESP_ERR_HTTP_EAGAIN) {
#       endif
            return true;
        }
        return false;
    }

    inline bool UpdateDBManager::hashChanged() {
        log_v("Hash from server: %s;", downloadedHash);
        log_v("Hash from old db: %s", currentHash);

        if (strcmp(downloadedHash, currentHash) == 0) {
            return false;
        } else {
            return true;
        }
    }

    inline bool UpdateDBManager::verifyHash() {
        // calculates hash from local recent downloaded db
        log_v("Finished calculating hash");

        log_v("Hash calculated from local file: %s;\n Hash from server: %s",
              calculatedHash, downloadedHash);

        if (strcmp(calculatedHash, downloadedHash) == 0) {
            return true;
        } else {
            return false;
        }
    }

    inline bool UpdateDBManager::activateNewDBFile() {
        bool ok = true;

        if (!verifyHash()) {
            ok = false;
            log_i("Downloaded DB file is corrupted "
                  "(hashs are not equal), ignoring.");
            SD.remove(otherFile);
        } else {
            swapFiles();
            closeDB();

            if (openDB(currentFile) != SQLITE_OK) {
                ok = false;
                log_w("Error opening the updated DB, reverting to old one");
                swapFiles();
                if (openDB(currentFile) != SQLITE_OK) { // FIXME: in the unlikely event that this fails too, we are doomed
                    // TODO:
                }
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

        SD.remove("/hash");
        file = SD.open("/hash", FILE_WRITE);
        file.write((byte *) downloadedHash, 65);
        file.close();
    }

    bool UpdateDBManager::checkFileFreshness(const char *tsfile) {
        // In some exceptional circumstances, we might end up writing
        // "1" to the file more than once; that's ok, 11 > 0 too :) .
        File f = SD.open(tsfile);
        int t = 0;
        if (f.available()) {
            t = f.parseInt(); // If reading fails, this returns 0
        }
        f.close();
        return t > 0;
    }

    inline void UpdateDBManager::chooseInitialFile() {
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
            } else if (!downloadingDB && !downloadingHash) {
                log_i("Downloading DB for the first time...");
                startHashDownload();
            } else {
                update();
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
