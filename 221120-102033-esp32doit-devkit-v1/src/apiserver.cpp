static const char* TAG = "server";

#include <common.h>
#include <Arduino.h>
#include <apiserver.h>
#include <authorizer.h>
#include <timemanager.h>
#include <RTClib.h>
#include <dbmanager.h>

//TODO: Need to see which of these libraries we are using
#include "esp_err.h"
#include "esp_log.h"
#include <keysserver.h>
#include <esp_https_server.h>
#include "esp_tls.h"

#include <stdio.h>
#include <sys/param.h>

#include "esp_vfs.h"
#include "esp_spiffs.h"

#define SCRATCH_BUFSIZE  512

namespace webServer {
    #define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)

    /* Max size of an individual file. Make sure this
    * value is same as that set in upload_script.html */
    #define MAX_FILE_SIZE   (200*1024) // 200 KB
    #define MAX_FILE_SIZE_STR "200KB"

    #define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)

    /* Scratch buffer size */
    #define SCRATCH_BUFSIZE  8192
    struct file_server_data {
        /* Base path of file storage */
        char base_path[ESP_VFS_PATH_MAX + 1];

        /* Scratch buffer for temporary storage during file transfer */
        char scratch[SCRATCH_BUFSIZE];
    };

    static const char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize) {
        const size_t base_pathlen = strlen(base_path);
        size_t pathlen = strlen(uri);

        const char *quest = strchr(uri, '?');
        if (quest) {
            pathlen = MIN(pathlen, quest - uri);
        }
        const char *hash = strchr(uri, '#');
        if (hash) {
            pathlen = MIN(pathlen, hash - uri);
        }

        if (base_pathlen + pathlen + 1 > destsize) {
            /* Full path string won't fit into destination buffer */
            return NULL;
        }

        /* Construct full path (base + path) */
        strcpy(dest, base_path);
        strlcpy(dest + base_pathlen, uri, pathlen + 1);
        /* Return pointer to path, skipping the base */
        Serial.println(dest + base_pathlen);
        return dest + base_pathlen;
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
        return ESP_OK;
    }


    static esp_err_t logs_handler(httpd_req_t *req) {
        log_d("[APP1] Free memory: %d bytes", esp_get_free_heap_size());
        const char* dirpath = "/sd/"; 
        if (!sdPresent) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start SD reader.");
            return ESP_FAIL;
        }
        log_d("[APP2] Free memory: %d bytes", esp_get_free_heap_size());
        char entrypath[FILE_PATH_MAX];
        char entrysize[16];
        const char *entrytype;

        struct dirent *entry;
        struct stat entry_stat;
        log_d("[APP3] Free memory: %d bytes", esp_get_free_heap_size());
        DIR *dir = opendir(dirpath);
        const size_t dirpath_len = strlen(dirpath);
        log_d("[APP4] Free memory: %d bytes", esp_get_free_heap_size());
        /* Retrieve the base path of file storage to construct the full path */
        strlcpy(entrypath, dirpath, sizeof(entrypath));

        if (!dir) {
            ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
            /* Respond with 404 Not Found */
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
            return ESP_FAIL;
        }
        log_d("[APP5] Free memory: %d bytes", esp_get_free_heap_size());
        /* Send HTML file header */
        httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>");

        /* Get handle to embedded file upload script */
        extern const unsigned char upload_script_start[] asm("_binary_src_upload_script_html_start");
        extern const unsigned char upload_script_end[]   asm("_binary_src_upload_script_html_end");
        const size_t upload_script_size = (upload_script_end - upload_script_start);
        log_d("[APP6] Free memory: %d bytes", esp_get_free_heap_size());
        /* Add file upload form and script which on execution sends a POST request to /upload */
        httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);

        /* Send file-list table definition and column labels */
        httpd_resp_sendstr_chunk(req,
            "<table class=\"fixed\" border=\"1\">"
            "<col width=\"800px\" /><col width=\"300px\" /><col width=\"300px\" /><col width=\"100px\" />"
            "<thead><tr><th>Name</th><th>Date of creation</th><th>Size (Bytes)</th><th>Delete</th><th>Download</th></tr></thead>"
            "<tbody>");
        log_d("[APP7] Free memory: %d bytes", esp_get_free_heap_size());
        /* Iterate over all files / folders and fetch their names and sizes */


        char daysOfTheWeek[15][15] = {"domingo", "segunda", "terça",
                                      "quarta", "quinta", "sexta", "sabado"};


        while ((entry = readdir(dir)) != NULL) {
            entrytype = (entry->d_type == DT_DIR ? "directory" : "file");
            log_d("[APP8] Free memory: %d bytes", esp_get_free_heap_size());    
            strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
            if (stat(entrypath, &entry_stat) == -1) {
                ESP_LOGE(TAG, "Failed to stat %s : %s", entrytype, entry->d_name);
                continue;
            }
            sprintf(entrysize, "%ld", entry_stat.st_size);
            ESP_LOGI(TAG, "Found %s : %s (%s bytes)", entrytype, entry->d_name, entrysize);

            unsigned long time = strtoul(entry->d_name, NULL, 10);
    
            DateTime moment = DateTime(time);
            Serial.println(entry->d_name);
            Serial.println(time);
            Serial.println(moment.unixtime());
            Serial.println(moment.year());
            char buf[192];
            sprintf(buf, "%u/%u/%u (%s) %u:%u:%u", moment.year(), moment.month(),
                    moment.day(), daysOfTheWeek[moment.dayOfTheWeek()],
                    moment.hour(), moment.minute(), moment.second());

            /* Send chunk of HTML file containing table entries with file name and size */
            httpd_resp_sendstr_chunk(req, "<tr><td><a href=\"");
            httpd_resp_sendstr_chunk(req, req->uri);
            httpd_resp_sendstr_chunk(req, entry->d_name);
            if (entry->d_type == DT_DIR) {
                httpd_resp_sendstr_chunk(req, "/");
            }
            httpd_resp_sendstr_chunk(req, "\">");
            httpd_resp_sendstr_chunk(req, entry->d_name);
            httpd_resp_sendstr_chunk(req, "</a></td><td>");
            httpd_resp_sendstr_chunk(req, buf);
            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, entrysize);
            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/delete");
            httpd_resp_sendstr_chunk(req, req->uri);
            httpd_resp_sendstr_chunk(req, entry->d_name);
            httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Delete</button></form>");
            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/download");
            httpd_resp_sendstr_chunk(req, req->uri);
            httpd_resp_sendstr_chunk(req, entry->d_name);
            httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Download</button></form>");
            httpd_resp_sendstr_chunk(req, "</td></tr>\n");
        }
        closedir(dir);
        log_d("[APP9] Free memory: %d bytes", esp_get_free_heap_size());
        /* Finish the file list table */
        httpd_resp_sendstr_chunk(req, "</tbody></table>");

        /* Send remaining chunk of HTML file to complete it */
        httpd_resp_sendstr_chunk(req, "</body></html>");

        /* Send empty chunk to signal HTTP response completion */
        httpd_resp_sendstr_chunk(req, NULL);
        log_d("[APP10] Free memory: %d bytes", esp_get_free_heap_size());
        return ESP_OK;
    }

    static esp_err_t download_handler(httpd_req_t *req) {
        if (!sdPresent) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start SD reader.");
            return ESP_FAIL;
        }
    
        char filepath[FILE_PATH_MAX];
        FILE* fd = NULL;
        struct stat file_stat;

        const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri + sizeof("/download") - 1, sizeof(filepath));

        if (!filename) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
            return ESP_FAIL;
        }
        log_d("[APP] Free memory: %d bytes", esp_get_free_heap_size());
        char buf[100];

        Serial.println(buf);
        fd = fopen(buf, "r");
        if (!fd) {
            ESP_LOGE(TAG, "Failed to read existing file : %s", buf);
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
            return ESP_FAIL;
        }
        log_v("Sending file : %s (%ld bytes)...", filepath, file_stat.st_size);
        httpd_resp_set_type(req, "text/txt");
        log_d("[APP] Free memory: %d bytes", esp_get_free_heap_size());
        /* Retrieve the pointer to scratch buffer for temporary storage */

        char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
        size_t chunksize;
        do {
            log_d("[APP] Free memory: %d bytes", esp_get_free_heap_size());
            /* Read file in chunks into the scratch buffer */
            chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
            if (chunksize > 0) {
                /* Send the buffer contents as HTTP response chunk */
                if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                    log_v("ccc");
                    fclose(fd);
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
        fclose(fd);
        ESP_LOGI(TAG, "File sending complete");

        /* Respond with an empty chunk to signal HTTP response completion */
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    static esp_err_t update_handler(httpd_req_t *req) {
        if (!sdPresent) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start SD reader, aborting update.");
            return ESP_FAIL;
        }
        char *basic_auth_resp = NULL;

        startUpdateDB();

        // FIXME: What should i do here?
        httpd_resp_set_status(req, HTTPD_200);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Connection", "keep-alive");
        asprintf(&basic_auth_resp, "{\"update\": true}", doorID);
        httpd_resp_send(req, basic_auth_resp, strlen(basic_auth_resp));

        free(basic_auth_resp);
    }

    httpd_handle_t start_webserver() {
        // Command to make request:
        // curl -v -GET --key client.key --cert client.crt https://10.0.2.101/open --cacert rootCA.crt
        const char *base_path = "/";

        static struct file_server_data *server_data = NULL;

        server_data = (file_server_data *) calloc(1, sizeof(struct file_server_data));
        if (!server_data) {
            ESP_LOGE(TAG, "Failed to allocate memory for server data");
            return 0;
        }
        strlcpy(server_data->base_path, base_path,
                sizeof(server_data->base_path));

        /* Generate default configuration */
        httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();

        config.httpd.uri_match_fn = httpd_uri_match_wildcard;
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
            return NULL;
        }

        httpd_uri_t open_door = {
                .uri       = "/open",  // Match all URIs of type /path/to/file
                .method    = HTTP_GET,
                .handler   = open_door_handler,
            };

        httpd_register_uri_handler(server, &open_door);

        httpd_uri_t teste = {
            .uri       = "/",  // Match all URIs of type /path/to/file
            .method    = HTTP_GET,
            .handler   = logs_handler,
        };

        httpd_register_uri_handler(server, &teste);

        httpd_uri_t download = {
            .uri       = "/download/*",   // Match all URIs of type /delete/path/to/file
            .method    = HTTP_POST,
            .handler   = download_handler,
            .user_ctx  = server_data    // Pass server data as context
        };

        httpd_register_uri_handler(server, &download);

        httpd_uri_t update = {
            .uri       = "/update",
            .method    = HTTP_POST,
            .handler   = update_handler,
        };

        httpd_register_uri_handler(server, &update);

        log_d("Finished setting up server");

        return server;
    }

    void disconnect_webserver (httpd_handle_t server) {
        httpd_ssl_stop(server);
    }
}

httpd_handle_t initServer() { return webServer::start_webserver(); }

void disconnectServer(httpd_handle_t server) { webServer::disconnect_webserver(server); }
