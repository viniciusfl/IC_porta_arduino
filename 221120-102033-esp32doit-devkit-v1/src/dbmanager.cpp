static const char *TAG = "HTTP_CLIENT";

#include <WiFi.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <sqlite3.h>
#include <common.h>
#include <RTClib.h>
#include <dbmanager.h>
#include "mbedtls/md.h"

#include "esp_system.h"
#include <keys.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"

#define MAX_HTTP_RECV_BUFFER 512

#define RETRY_DOWNLOAD_TIME 60000

#define DOWNLOAD_INTERVAL 30000

#define DEBUG

int received = 0;

namespace DBNS {
    // This is a wrapper around SQLite which allows us
    // to query whether a user is authorized to enter.
    class Authorizer {
    public:
        void init();
        int openDB(const char *filename);
        void closeDB();
        bool userAuthorized(const char* readerID, unsigned long cardID);

    private:
        sqlite3 *sqlitedb;
        sqlite3_stmt *dbquery;
        sqlite3 *sqlitelog;
        sqlite3_stmt *logquery;
        void generateLog(unsigned long cardID, const char* readerID,
                         bool authorized);
        int openlogDB();
        void closelogDB();
    };

    // This is an auxiliary class to UpdateDBManager. It processes small
    // chunks of data at a time to prevent blocking and writes what is
    // received to disk, minus the HTTP headers.
    class FileWriter {
    public:
        void open(const char*);
#       ifdef USE_SOCKETS
        void write(WiFiClient&);
#       else
        inline void write(byte* buffer, int size);
#       endif
        void close();
    private:
        File file;
#       ifdef USE_SOCKETS
        const static int bufsize = 512;
        byte buf[bufsize];
        int position = 0;
        char previous;
        bool headerDone = false;
        bool beginningOfLine = true;
#       endif
    };
   
    // This class periodically downloads a new version of the database
    // and sets this new version as the "active" db file in the Authorizer
    // when downloading is successful. It alternates between two filenames
    // to do its thing and, during startup, identifies which of them is
    // the current one by means of the "timestamp" file.
    class UpdateDBManager {
    public:
        void init(Authorizer *);
        void update();
        FileWriter writer;

    private:
        const char *currentFile;
        const char *otherFile;
        const char *currentTimestampFile;
        const char *otherTimestampFile;

        void chooseInitialFile();
        void swapFiles();
        bool checkFileFreshness(const char *);

        const char *SERVER = "10.0.2.106";
        unsigned long lastDownloadTime = 0;
        unsigned long downloadStartTime;
        void processDownload();

        unsigned char localHashHex[32];
        unsigned char serverHash[64];
        unsigned char oldChecksum[64];
        bool downloadingChecksum = false;
        void startChecksumDownload();
        void finishChecksumDownload();
        bool verifyChecksum();

        bool downloadingDB = false; 
        void startDBDownload();
        bool downloadEnded();
        void finishDBDownload();
        void activateNewDBFile();

        Authorizer *authorizer;
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
    void UpdateDBManager::init(Authorizer *authorizer) {
        if (!SD.begin()) {
            Serial.println("Card Mount Failed, aborting");
            Serial.flush();
            while (true)
                delay(10);
#       ifdef DEBUG
        } else {
            Serial.println("SD connected.");
#       endif
        }

        this->authorizer = authorizer;

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

            if (currentMillis - lastDownloadTime > DOWNLOAD_INTERVAL)
                startDBDownload();
            return;
        }

        if (downloadingChecksum) { 
            // If we finished downloading the checksum, procede to verify it
            // is valid and finish the db update
            if(downloadEnded()){
                finishChecksumDownload();
                activateNewDBFile();
                return;
            }

            // If we did not return yet, we are still downloading the checksum
            processDownload();
            return;
        }
        // If we did not return above, we are still downloading the DB;

        // are we done yet?
        if(downloadEnded()){
            finishDBDownload();
            startChecksumDownload();
            return;
        }

        // If we did not return above, we are still downloading the DB
        processDownload();
    }

#   ifdef USE_SOCKETS
    void UpdateDBManager::startDBDownload() {
        downloadStartTime = millis();

        // If WiFI is disconnected, pretend nothing
        // ever happened and try again later
        if (WiFi.status() != WL_CONNECTION_LOST){
            Serial.println("No internet available, canceling db update.");
            lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;
            return;
        } 

        netclient.connect(SERVER, 80);

#       ifdef DEBUG
        if (netclient.connected()) {
            Serial.println("Connected to server.");
        } else {
            Serial.println("Connection to server failed.");
        }
#       endif

        // If connection failed, pretend nothing
        // ever happened and try again later
        if (!netclient.connected()) {
#           ifdef DEBUG
            Serial.println("Client is not connected... aborting DB update.");
#           endif
            lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;
            netclient.stop();
            return;
        }

        downloadingDB = true;

        netclient.println("GET /dataBaseIME.db HTTP/1.1");
        netclient.println(((String) "Host: ") + SERVER);
        netclient.println("Connection: close");
        netclient.println();

#       ifdef DEBUG
        Serial.println("---> Started DB upgrade...");
#       endif

        // remove old DB files
        SD.remove(otherTimestampFile);
        SD.remove(otherFile);

        writer.open(otherFile);
    }

    void UpdateDBManager::processDownload() {
        writer.write(netclient);
    }

    void UpdateDBManager::finishDBDownload() {
        unsigned int long downloadFinishTime = millis() - downloadStartTime;
        netclient.flush();
        netclient.stop();
        writer.close();
        downloadingDB = false;
        lastDownloadTime = currentMillis;

#       ifdef DEBUG
        Serial.println("Disconnecting from server and finishing db update.");
        Serial.printf("Download took %lu ms\n",  downloadFinishTime);
        Serial.println("Started checksum download.");
#       endif
    }


    void UpdateDBManager::startChecksumDownload() {
        netclient.connect(SERVER, 80);

#       ifdef DEBUG
        if (netclient.connected()) {
            Serial.println("Connected to server.");
        } else {
            Serial.println("Connection to server failed.");
        }
#       endif


        // If connection failed, pretend nothing
        // ever happened and try again later
        if (!netclient.connected()) {
            Serial.println("Client checksum disconnected... trying again later");
            netclient.stop();
            return;
        }

        downloadingChecksum = true;

        // http request to get hash of db from server
        netclient.println("GET /checksum HTTP/1.1");
        netclient.println(((String) "Host: ") + SERVER);
        netclient.println("Connection: close");
        netclient.println();

        SD.remove("/checksum");

        writer.open("/checksum");
    }

    void UpdateDBManager::finishChecksumDownload() {
        netclient.flush();
        netclient.stop();
        downloadingChecksum = false;
        writer.close();
    }

    // TODO: receiving WiFiClient as a parameter here feels very hackish...
    void FileWriter::write(WiFiClient& netclient) {
        int avail = netclient.available();
        if (avail <= 0) return;

        if (headerDone) {
            int length;
            if (avail <= bufsize - position) {
                length = avail;
            } else {
                length = bufsize - position;
            }

            int check = netclient.read(buf + position, length);
            if (! check == length) {
                Serial.println("Something bad happened reading from network");
            }
            position += length;

            if (position >= bufsize) {
                file.write(buf, position);
                position = 0;
            }
        } else {
            char c = netclient.read();
            if (c == '\n') {
                if (beginningOfLine && previous == '\r') {
                    headerDone = true;
#                   ifdef DEBUG
                    Serial.println("Header done!");
#                   endif
                } else {
                    previous = 0;
                }
                beginningOfLine = true;
            } else {
                previous = c;
                if (c != '\r')
                    beginningOfLine = false;
            }
        }
    }

#   else // USE_SOCKETS is undefined

    void UpdateDBManager::startDBDownload() {
        esp_http_client_config_t config = {
            .host = "10.0.2.106",
            .port = 443,
            .path = "/dataBaseIME.db",
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
#           ifdef DEBUG
            ESP_LOGE(TAG, "Network connection failed, aborting DB update.");
#           endif
            lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;

            esp_http_client_cleanup(httpclient);

            return;
        }

        // Download started ok! Normally "err" should be EAGAIN,
        // meaning the download started but has not yet finished,
        // but maybe the file is very small and download ended
        // already. If this happens, we do nothing special here;
        // processDBDownload() will also receive ESP_OK and handle it.
        downloadingDB = true;
        downloadStartTime = millis();

        // remove old DB files
        SD.remove(otherTimestampFile);
        SD.remove(otherFile);

        writer.open(otherFile);
    }

    void UpdateDBManager::finishDBDownload() {
        unsigned int long downloadFinishTime = millis() - downloadStartTime;

        if (esp_http_client_is_complete_data_received(httpclient)) {
            ESP_LOGE(TAG, "Finished ok\n");
        } else {
            ESP_LOGE(TAG, "Did not finish ok\n");
        };

        esp_http_client_close(httpclient); // TODO: this should not be needed
        esp_http_client_cleanup(httpclient);
        writer.close();
        downloadingDB = false;
        lastDownloadTime = currentMillis;
        received = 0;
        downloadingChecksum = true;

#       ifdef DEBUG
        Serial.println("Disconnecting from server and finishing db update.");
        Serial.printf("Download took %lu ms\n",  downloadFinishTime);
#       endif
    }


void UpdateDBManager::startChecksumDownload() {
        Serial.println("Started checksum download");

        esp_http_client_config_t config = {
            .host = "10.0.2.106",
            .port = 443,
            .path = "/checksum",
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
#           ifdef DEBUG
            ESP_LOGE(TAG, "Network connection failed, aborting DB update.");
#           endif
            lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;

            esp_http_client_cleanup(httpclient);

            return;
        }

        downloadingChecksum = true;
        downloadStartTime = millis();

        File f = SD.open("/checksum");
        f.read(oldChecksum, 64);

        SD.remove("/checksum");

        writer.open("/checksum");
    }

    void UpdateDBManager::processDownload(){
        esp_err_t err = esp_http_client_perform(httpclient);

        if (err != ESP_ERR_HTTP_EAGAIN and err != ESP_OK) {
            // Connection failed. No worries, just pretend
            // nothing ever happened and try again later
#           ifdef DEBUG
            ESP_LOGE(TAG, "Network connection failed, aborting DB update.");
#           endif
            lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;

            downloadingDB = false;
            downloadingChecksum = false;

            esp_http_client_cleanup(httpclient);
        }
    }

    void UpdateDBManager::finishChecksumDownload() {
        if (esp_http_client_is_complete_data_received(httpclient)) {
            ESP_LOGE(TAG, "Finished checksum ok\n");
        } else {
            ESP_LOGE(TAG, "Did not finish ok\n");
        };

        esp_http_client_close(httpclient); // TODO: this should not be needed
        esp_http_client_cleanup(httpclient);
        writer.close();
        downloadingChecksum = false;
        lastDownloadTime = currentMillis;
        received = 0;

#       ifdef DEBUG
        Serial.println("Disconnecting from server and finishing checksum download.");
#       endif
    }

    inline void FileWriter::write(byte* buffer, int size) {
        file.write(buffer, size);
    }


#   endif // USE_SOCKETS

    bool UpdateDBManager::downloadEnded(){
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

    bool UpdateDBManager::verifyChecksum() {
        // calculates hash from local recent downloaded db
#       ifdef DEBUG
        Serial.println("Finished downloading hash");
#       endif

        String name = (String) "/sd" + otherFile; // FIXME:

        int rc = mbedtls_md_file(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                                 name.c_str(), localHashHex);

        if (rc != 0) {
            Serial.printf("Failed to access file %s\n", name.c_str());
            return false;
        }

        char hash[65];
        for (int i = 0; i < 32; ++i) {
            snprintf(hash + 2*i, 3,"%02hhx", localHashHex[i]);
        }

        File f = SD.open("/checksum");
        char servHash[65];
        f.readString().toCharArray(servHash, 65);

#       ifdef DEBUG
        Serial.print("Hash from local file:  ");
        Serial.println(hash);
        Serial.print("Hash from server file: ");
        Serial.println(servHash);
#       endif

        if(strcmp(hash, servHash))
            return false;
        return true;
    }

    void UpdateDBManager::activateNewDBFile() {
        if (!verifyChecksum()) {
#           ifdef DEBUG
            Serial.print("Downloaded DB file is corrupted (checksums are not equal), ignoring.");
#           endif
            SD.remove(otherFile);
            return;
        }

        swapFiles();
        authorizer->closeDB();

        if (authorizer->openDB(currentFile) != SQLITE_OK) {
#           ifdef DEBUG
            Serial.println("Error opening the updated DB, reverting to old one");
#           endif
            swapFiles();
            // FIXME: in the unlikely event that this fails too, we are doomed
            authorizer->openDB(currentFile);
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
        if (f) {
            int t = f.parseInt(); // If reading fails, this returns 0
        }
        f.close();
        return t > 0;
    }

    void UpdateDBManager::chooseInitialFile() {
        currentFile = "/bancoA.db";
        otherFile = "/bancoB.db";
        currentTimestampFile = "/TSA.TXT";
        otherTimestampFile = "/TSB.TXT";
        bool dbFileOK = false;

         while (!dbFileOK) {
            dbFileOK = true; // a little optimism might pay off :)
            if (!checkFileFreshness(currentTimestampFile)) {
                if (!checkFileFreshness(otherTimestampFile)) {
                    if (!downloadingDB && !downloadingChecksum) {
    #                   ifdef DEBUG
                        Serial.printf("Downloading DB for the first time...");
    #                   endif
                        startDBDownload();
                    } else {
                        update();
                    }
                    dbFileOK = false; // it didn't :(
                } else {
                    swapFiles();
                }
            }
        }

#       ifdef DEBUG
        Serial.printf("Choosing %s as current DB.\n", currentFile);
#       endif
        authorizer->openDB(currentFile); 
    }

    void FileWriter::open(const char *filename) {
        file = SD.open(filename, FILE_WRITE);
        if(!file) { 
            Serial.println("Error openning DB file.");
        }
  
#       ifdef USE_SOCKETS
        headerDone = false;
        beginningOfLine = true;
        buf[0] = 0;
        position = 0;
        previous = 0;
#       endif

#       ifdef DEBUG
        Serial.print("Writing to ");
        Serial.println(filename);
#       endif
    }


    void FileWriter::close() {
#       ifdef USE_SOCKETS
        file.write(buf, position);
#       endif
        file.close();
    }

    // This should be called from setup()
    void Authorizer::init() {
        sqlitedb = NULL; // check the comment near Authorizer::closeDB()
        dbquery = NULL;
        sqlitelog = NULL;
        logquery = NULL;
        sqlite3_initialize();
    }

    int Authorizer::openDB(const char *filename) {
        closeDB();
        String name = (String) "/sd" + filename; // FIXME: 

        int rc = sqlite3_open(name.c_str(), &sqlitedb);
        if (rc != SQLITE_OK)
        {
            Serial.printf("Can't open database: %s\n", sqlite3_errmsg(sqlitedb));
        } else {

#           ifdef DEBUG
            Serial.printf("Opened database successfully %s\n", filename);
#           endif
            rc = sqlite3_prepare_v2(sqlitedb,
                                    "SELECT EXISTS(SELECT * FROM auth WHERE userID=? AND doorID=?)",
                                    -1, &dbquery, NULL);

            if (rc != SQLITE_OK) {
                Serial.printf("Can't generate prepared statement: \n");
                Serial.printf("%s: %s\n", sqlite3_errstr(sqlite3_extended_errcode(sqlitedb)), sqlite3_errmsg(sqlitedb));
#           ifdef DEBUG
            } else {
                Serial.printf("Prepared statement created\n");
#           endif
            }
        }

        return rc;
    }

    int Authorizer::openlogDB() {
        const char* filename = "/sd/log.db"; // FIXME: 
        int rc = sqlite3_open(filename, &sqlitelog);

        if (rc != SQLITE_OK) {
            Serial.printf("Can't open database: %s\n", sqlite3_errmsg(sqlitelog));
        } else {
            Serial.printf("Opened database successfully\n");

            // prepare query
            rc = sqlite3_prepare_v2(sqlitelog,
                                    "INSERT INTO log(cardID, doorID, readerID, unixTimestamp, authorized) VALUES(?, ?, ?, ?, ?)",
                                    -1, &logquery, NULL);

            if (rc != SQLITE_OK) {
                Serial.printf("Can't generate prepared statement for log DB: %s\n",
                              sqlite3_errmsg(sqlitelog));
#           ifdef DEBUG
            } else {
                Serial.printf("Prepared statement created for log DB\n");
#           endif
            }
        }
        return rc;
    }


    // search element through current database
    bool Authorizer::userAuthorized(const char* readerID,
                                    unsigned long cardID) {

        if (sqlitedb == NULL)
            return false;

#       ifdef DEBUG
        Serial.print("Card reader ");
        Serial.print(readerID);
        Serial.println(" was used.");
        Serial.print("We received -> ");
        Serial.println(cardID);
#       endif
        sqlite3_int64 card = cardID;
        sqlite3_reset(dbquery);
        sqlite3_bind_int64(dbquery, 1, card);
        sqlite3_bind_int(dbquery, 2, doorID); 

        // should i verify errors while binding?

        bool authorized = false;
        int rc = sqlite3_step(dbquery);
        while (rc == SQLITE_ROW) {
            if (1 == sqlite3_column_int(dbquery, 0))
                authorized = true;
            rc = sqlite3_step(dbquery);
        }

        if (rc != SQLITE_DONE) {
            Serial.printf("Error querying DB: %s\n", sqlite3_errmsg(sqlitedb));
        }

        generateLog(cardID, readerID, authorized);

        if (authorized) {
            authorized = false;
            return true;
        } else {
            return false;
        }
    }

    void Authorizer::generateLog(unsigned long cardID, const char* readerID,
                                 bool authorized) {

        //TODO: create error column in db 

        // get unix time
        time_t now;
        time(&now);
        unsigned long systemtime = now;

        openlogDB();

        // should i verify errors while binding? 

        sqlite3_int64 card = cardID;
        sqlite3_int64 unixTime = systemtime;

        sqlite3_reset(logquery);
        sqlite3_bind_int64(logquery, 1, card);
        sqlite3_bind_int(logquery, 2, doorID); 
        sqlite3_bind_text(logquery, 3, readerID, -1, SQLITE_STATIC);
        sqlite3_bind_int64(logquery, 4, unixTime); 
        sqlite3_bind_int(logquery, 5, authorized); 
        
        int rc = sqlite3_step(dbquery);
        while (rc == SQLITE_ROW) {
            rc = sqlite3_step(logquery);
        }

        if (rc != SQLITE_DONE) {
            Serial.printf("Error querying DB: %s\n", sqlite3_errmsg(sqlitelog));
        }
        closelogDB();
    }

    // The sqlite3 docs say "The C parameter to sqlite3_close(C) and
    // sqlite3_close_v2(C) must be either a NULL pointer or an sqlite3
    // object pointer [...] and not previously closed". So, we always
    // make it NULL here to avoid closing a pointer previously closed.
    void Authorizer::closeDB() {
        sqlite3_finalize(dbquery);
        dbquery = NULL;
        sqlite3_close_v2(sqlitedb);
        sqlitedb = NULL;
    }

    void Authorizer::closelogDB(){
        sqlite3_finalize(logquery);
        logquery = NULL;
        sqlite3_close_v2(sqlitelog);
        sqlitelog = NULL;
    }

    Authorizer authorizer;
    UpdateDBManager updateDBManager;

    // TODO: this needs to write the data out. It is probably a good
    //       idea to use user_data in the config to pass the file
    //       handler to this function.
#   ifndef USE_SOCKETS
    esp_err_t handler(esp_http_client_event_t *evt) {
        switch(evt->event_id) {
            case HTTP_EVENT_ERROR:
                ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
                break;
            case HTTP_EVENT_ON_CONNECTED:
                ESP_LOGE(TAG, "HTTP_EVENT_ON_CONNECTED");
                break;
            case HTTP_EVENT_HEADER_SENT:
                ESP_LOGE(TAG, "HTTP_EVENT_HEADER_SENT");
                break;
            case HTTP_EVENT_ON_HEADER:
                ESP_LOGE(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
                break;
            case HTTP_EVENT_ON_DATA:
                ESP_LOGE(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
                if (!esp_http_client_is_chunked_response(evt->client)) {
                    received += evt->data_len;
                    updateDBManager.writer.write((byte*) evt->data, evt->data_len);
                } else {ESP_LOGE(TAG, "CHUNKED!");}
                break;
            case HTTP_EVENT_ON_FINISH:
                ESP_LOGE(TAG, "HTTP_EVENT_ON_FINISH");
                ESP_LOGE(TAG, "Received: %d", received);
                break;
            case HTTP_EVENT_DISCONNECTED:
                ESP_LOGE(TAG, "HTTP_EVENT_DISCONNECTED");
                int mbedtls_err = 0;
                esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
                if (err != 0) {
                    ESP_LOGE(TAG, "Last esp error code: 0x%x", err);
                    ESP_LOGE(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
                }
                received = 0;
                break;
            //case HTTP_EVENT_REDIRECT:
            //    ESP_LOGE(TAG, "HTTP_EVENT_REDIRECT");
            //    break;
        }
        return ESP_OK;
    }
#   endif
}



void initDB() {
    DBNS::authorizer.init();
    DBNS::updateDBManager.init(&DBNS::authorizer);
}

void updateDB() {
    DBNS::updateDBManager.update();
}

bool userAuthorized(const char* readerID, unsigned long cardID) {
    return DBNS::authorizer.userAuthorized(readerID, cardID);
}
