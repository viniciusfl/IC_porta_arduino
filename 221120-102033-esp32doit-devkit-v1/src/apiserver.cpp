static const char* TAG = "server";

#include <common.h>
#include <Arduino.h>
#include <apiserver.h>
#include <esp_http_server.h>
#include <authorizer.h>
#include <timemanager.h>
#include <SD.h>

//TODO: Need to see which of these libraries we are using
#include "esp_err.h"
#include "esp_log.h"
#include <keysserver.h>
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include <esp_https_server.h>
#include "esp_tls.h"

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#define SCRATCH_BUFSIZE  512

namespace webServer {

    static esp_err_t download_get_handler(httpd_req_t *req);

    static esp_err_t open_door_handler(httpd_req_t *req);

    esp_err_t start_webserver(void) {
        // Command to make request:
        // curl -v -GET --key client.key --cert client.crt https://10.0.2.101/open --cacert rootCA.crt

        /* Generate default configuration */
        httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();

        /* Empty handle to esp_http_server */
        httpd_handle_t server = NULL;

        // CA certificate (here it is treated as server cert, documentation is wrong)
        config.cacert_pem = server_pem;
        config.cacert_len = sizeof(server_pem)/sizeof(server_pem[0]);

        // Private key
        config.prvtkey_pem = key;
        config.prvtkey_len = sizeof(key)/sizeof(key[0]);

        // Client verify authority certificate (CA used to sign clients, or client cert itself
        config.client_verify_cert_pem = test_ca_cert;
        config.client_verify_cert_len = sizeof(test_ca_cert)/sizeof(test_ca_cert[0]);

        /* Start the httpd server */
        if (httpd_ssl_start(&server, &config) != ESP_OK) {
            // Set URI handlers
            log_v("Failed to start file server!");
            return ESP_FAIL;
        }
        /* If server failed to start, handle will be NULL */
        httpd_uri_t file_download = {
            .uri       = "/getLogs",  // Match all URIs of type /path/to/file
            .method    = HTTP_GET,
            .handler   = download_get_handler,
        };

        httpd_register_uri_handler(server, &file_download);

        httpd_uri_t open_door = {
                .uri       = "/open",  // Match all URIs of type /path/to/file
                .method    = HTTP_GET,
                .handler   = open_door_handler,
            };

        httpd_register_uri_handler(server, &open_door);

        log_d("Finished setting up server");

        return ESP_OK;
    }


    static esp_err_t download_get_handler(httpd_req_t *req) {
        File f = SD.open("/latestBackup", FILE_READ);
        log_d("[APP] Free memory: %d bytes", esp_get_free_heap_size());
        if (!f) {
            log_v("Failed to read backup file");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
            return ESP_FAIL;
        }
        unsigned long timestamp = f.parseInt();
        f.close();

        char filepath[50];
        sprintf(filepath, "/%lu", timestamp);

        f = SD.open(filepath, FILE_READ);
        if (!f) {
            log_v("Failed to read backup file");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
            return ESP_FAIL;
        }
        log_d("[APP] Free memory: %d bytes", esp_get_free_heap_size());
        log_v("Sending file : %s (%ld bytes)...", filepath, f.size());
        httpd_resp_set_type(req, "text/txt");
        httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=log.txt");
        log_d("[APP] Free memory: %d bytes", esp_get_free_heap_size());
        /* Retrieve the pointer to scratch buffer for temporary storage */

        char chunk[SCRATCH_BUFSIZE];
        size_t chunksize;
        do {
            log_d("[APP] Free memory: %d bytes", esp_get_free_heap_size());
            /* Read file in chunks into the scratch buffer */
            chunksize = f.readBytes(chunk, SCRATCH_BUFSIZE);
            if (chunksize > 0) {
                /* Send the buffer contents as HTTP response chunk */
                if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                    log_v("ccc");
                    f.close();
                    ESP_LOGE(TAG, "File sending failed!");
                    /* Abort sending file */
                    httpd_resp_sendstr_chunk(req, NULL);
                    /* Respond with 500 Internal Server Error */
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                    return ESP_FAIL;
                }
            }
            /* Keep looping till the whole file is sent */
        } while (chunksize != 0);

        /* Close file after sending complete */
        f.close();
        ESP_LOGI(TAG, "File sending complete");

        /* Respond with an empty chunk to signal HTTP response completion */
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }
    
    static esp_err_t open_door_handler(httpd_req_t *req) {
        char *basic_auth_resp = NULL;
        if (!openDoor()) {
            log_v("Failed to open door");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open door");
            return ESP_FAIL;
        }
        log_e("Openned door");

        // TODO: What should i return here?
        httpd_resp_set_status(req, HTTPD_200);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Connection", "keep-alive");
        asprintf(&basic_auth_resp, "{\"opened\": true,\"doorID\": \"%d\"}", doorID);
        httpd_resp_send(req, basic_auth_resp, strlen(basic_auth_resp));
        free(basic_auth_resp);
    }
}

void initServer() { webServer::start_webserver(); }
