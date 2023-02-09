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
    // This is an auxiliary class to UpdateDBManager. It processes small
    // chunks of data at a time to prevent blocking and writes what is
    // received to disk, minus the HTTP headers.
    class FileWriter {
    public:
        inline void open(const char*);
#       ifdef USE_SOCKETS
        inline void write(WiFiClient&);
#       else
        inline void write(byte* buffer, int size);
#       endif
        inline void close();
    private:
        File file;
#       ifdef USE_SOCKETS
        const static int bufsize = 512;
        byte buf[bufsize];
        int position = 0;
#       endif
    };
   
    // This class periodically downloads a new version of the database
    // and sets this new version as the "active" db file in the Authorizer
    // when downloading is successful. It alternates between two filenames
    // to do its thing and, during startup, identifies which of them is
    // the current one by means of the "timestamp" file.
    class UpdateDBManager {
    public:
        inline void init();
        void update();

        FileWriter writer;

        void mbedtlsUpdate(byte* buffer, int size);
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
        inline void processCurrentDownload();

        byte shaResult[32];
        mbedtls_md_context_t ctx;
        mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

        char oldChecksum[65];
        bool downloadingChecksum = false;
        inline void startChecksumDownload();
        inline bool finishChecksumDownload();
        inline bool verifyChecksum();
        inline bool verifyOldChecksum();

        bool downloadingDB = false; 
        void startDBDownload();
        inline bool downloadEnded();
        inline bool finishDBDownload();

        bool startDownload(const char*);
        inline bool finishCurrentDownload();
        inline void activateNewDBFile();

#       ifdef USE_SOCKETS
        WiFiClient netclient;
#       else
        esp_http_client_handle_t httpclient;
#       endif
    };

#   ifndef USE_SOCKETS
    esp_err_t handler(esp_http_client_event_t *evt);
#   endif

    // This should be called from setup()
    inline void UpdateDBManager::init() {
        chooseInitialFile();
    }

    // This should be called from loop()
    // At each call, we determine the current state we are in, perform
    // a small chunk of work, and return. This means we do not hog the
    // processor and can pursue other tasks while updating the DB.
    void UpdateDBManager::update() {
        //FIXME: what if download stops working in the middle and never ends?

        // We start a download only if we are not already downloading
        if (!downloadingDB && !downloadingChecksum) {
            // millis() wraps every ~49 days, but
            // wrapping does not cause problems here
            // TODO: we might prefer to use dates,
            //       such as "at midnight"
            //
            // ANWSER: we can use an alarm with RTC 1307
            // https://robojax.com/learn/arduino/?vid=robojax_DS1307-clock-alarm
            // I believe the nodeMCU RTC might also have this feature

            if (currentMillis - lastDownloadTime > DOWNLOAD_INTERVAL) {
                startChecksumDownload();
            }

            return;
        }

        // If we did not return above, we are currently downloading something
        // (either the DB or the checksum)

        if (downloadingChecksum) { 
            // If we finished downloading the checksum, check whether the
            // download was successful;
            if(downloadEnded()){
                if (finishChecksumDownload()) {
                    // If old DB checksum and current DB checksum matches,
                    // it means DB didn't change and we don't need to update 
                    if (verifyOldChecksum()){
                        lastDownloadTime = currentMillis;
                        return;
                    }
                    // else, starts downloading DB
                    startDBDownload();

                }
            } else {
                // still downloading the checksum
                processCurrentDownload();
            }

            return;
        }

        // If we did not return above, we are still downloading the DB

        // If we finished downloading the DB, check whether the
        // download was successful; if so and the checksum matches,
        // start using the new DB
        if(downloadEnded()) {
            if (finishDBDownload()) {
                // Both downloads successful, update the timestamp
                activateNewDBFile();
                lastDownloadTime = currentMillis;
            }
        } else {
            // still downloading the DB
            processCurrentDownload();
        }
    }

    void UpdateDBManager::startDBDownload() {
        log_v("Starting DB download");
        if (!startDownload("/dataBaseIME.db")) {
            log_i("Network failure, cancelling DB download");
            lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;
            return;
        }
        log_v("Started DB download");
        downloadingDB = true;

        // remove old DB files
        SD.remove(otherTimestampFile);
        SD.remove(otherFile);
        writer.open(otherFile);

        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
        mbedtls_md_starts(&ctx);
    }

    inline void UpdateDBManager::startChecksumDownload() {
        log_v("Starting checksum download");
        if (!startDownload("/checksum")) {
            log_i("Network failure, cancelling checksum download");
            lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;
            return;
        }
        log_v("Started checksum download");
        downloadingChecksum = true;

        File f = SD.open("/checksum");
        f.readString().toCharArray(oldChecksum, 65);
        f.close();

        SD.remove("/checksum");
        writer.open("/checksum");
    }

    inline bool UpdateDBManager::finishDBDownload() {
        downloadingDB = false;

        bool finishedOK = finishCurrentDownload();

        if (finishedOK) {
            log_v("DB download finished, disconnecting from server.");
        } else {
            lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;
            log_v("DB download finished with error, ignoring file.");
        }

        mbedtls_md_finish(&ctx, shaResult);
        mbedtls_md_free(&ctx);

        return finishedOK;
    }

    inline bool UpdateDBManager::finishChecksumDownload() {
        downloadingChecksum = false;
        bool finishedOK = finishCurrentDownload();

        if (finishedOK) {
            log_v("Checksum download finished, disconnecting from server.");
        } else {
            lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;
            log_v("Checksum download finished with error, ignoring file.");
        }

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

        char buf[192];
        buf[0] = 0;
        snprintf(buf, 192,
                      "GET %s HTTP/1.1\n"
                      "Host: %s\n"
                      "Connection: close\n\n",
                 filename, SERVER);
        netclient.print(buf);

        return true;
    }


    inline void UpdateDBManager::processCurrentDownload() {
        writer.write(netclient);
    }

    inline bool UpdateDBManager::finishCurrentDownload() {
        netclient.flush();
        netclient.stop();
        writer.close();
        // we do not really check whether the download succeeded here
        return true;
    }

    // TODO: receiving WiFiClient as a parameter here feels very hackish...
    inline void FileWriter::write(WiFiClient& netclient) {
        int avail = netclient.available();
        if (avail <= 0) return;

            int length;
            if (avail <= bufsize - position) {
                length = avail;
            } else {
                length = bufsize - position;
            }

            int check = netclient.read(buf + position, length);
            if (! check == length) {
                log_i("Something bad happened reading from network");
            }
            position += length;

            if (position >= bufsize) {
                file.write(buf, position);
                position = 0;
            }
    }

#   else // USE_SOCKETS is undefined

    bool UpdateDBManager::startDownload(const char* filename) {
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

        writer.close();
        esp_http_client_cleanup(httpclient);

        return finishedOK;
    }

    // Nothing to do here, all work is done by the callback function
    inline void UpdateDBManager::processCurrentDownload() { }

    inline void FileWriter::write(byte* buffer, int size) {
        file.write(buffer, size);
    }

#   endif // USE_SOCKETS


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

    inline bool UpdateDBManager::verifyOldChecksum(){
        char serverHash[65];

        // If checksum file doesn't exist, there is no
        // reason to proceed
        if(SD.exists("/checksum")) return false;

        File f = SD.open("/checksum");
        f.readString().toCharArray(serverHash, 65);
        f.close();

        Serial.println(serverHash);

        log_v("Hash from server: %s; Hash from old db: %s",
              serverHash, oldChecksum);

        if(strcmp(serverHash, oldChecksum)) {
            return false;
        } else {
            return true;
        }
    }

    inline bool UpdateDBManager::verifyChecksum() {
        // calculates hash from local recent downloaded db
        log_v("Finished downloading hash");

        char calculatedHash[65];
        for (int i = 0; i < 32; ++i) {
            snprintf(calculatedHash + 2*i, 3,"%02hhx", shaResult[i]);
        }

        File f = SD.open("/checksum");
        char downloadedHash[65];
        f.readString().toCharArray(downloadedHash, 65);
        f.close();

        log_v("Hash from local file: %s; \nHash from server file: %s",
              calculatedHash, downloadedHash);

        if(strcmp(calculatedHash, downloadedHash)) {
            return false;
        } else {
            return true;
        }
    }

    inline void UpdateDBManager::activateNewDBFile() {
        if (!verifyChecksum()) {
            log_i("Downloaded DB file is corrupted (checksums are not equal), ignoring.");
            SD.remove(otherFile);
            SD.remove("/checksum"); // TODO: there may be better ways to manage this
            return;
        }

        swapFiles();
        closeDB();

        if (openDB(currentFile) != SQLITE_OK) {
            log_w("Error opening the updated DB, reverting to old one");
            swapFiles();
            if(openDB(currentFile) != SQLITE_OK){ // FIXME: in the unlikely event that this fails too, we are doomed
                SD.remove("/checksum");
                // TODO:
            }
        }
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
        File f = SD.open(currentTimestampFile, FILE_WRITE);
        f.print(1);
        f.close();

        f = SD.open(otherTimestampFile, FILE_WRITE);
        f.print(0);
        f.close();
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
            } else if (!downloadingDB && !downloadingChecksum) {
                log_i("Downloading DB for the first time...");
                startChecksumDownload();
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

    void UpdateDBManager::mbedtlsUpdate(byte* buffer, int size) {
        if (!downloadingDB) {
            return;
        }

        mbedtls_md_update(&ctx, (const unsigned char *) buffer, size);
    }

    inline void FileWriter::open(const char *filename) {
        file = SD.open(filename, FILE_WRITE);
        if(!file) { 
            log_e("Error openning file.");
        }

#       ifdef USE_SOCKETS
        buf[0] = 0;
        position = 0;
#       endif

        log_v("Writing to %s", filename);
    }


    inline void FileWriter::close() {
#       ifdef USE_SOCKETS
        file.write(buf, position);
#       endif
        file.close();
    }

    UpdateDBManager updateDBManager;

#   ifndef USE_SOCKETS
    // The callback for the HTTP client
    esp_err_t handler(esp_http_client_event_t *evt) {
        const char* TAG = "http_callback";
        switch(evt->event_id) {
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
                    updateDBManager.writer.write((byte*) evt->data,
                                                 evt->data_len);
                    updateDBManager.mbedtlsUpdate((byte*) evt->data,
                                                    evt->data_len);
                } else {log_e("CHUNKED!");}
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
            //case HTTP_EVENT_REDIRECT:
            //    log_d("HTTP_EVENT_REDIRECT");
            //    break;
        }
        return ESP_OK;
    }
#   endif
}


void initDBMan() { DBNS::updateDBManager.init(); }

void updateDB() { DBNS::updateDBManager.update(); }
