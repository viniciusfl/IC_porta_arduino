#ifndef APISERVER_H
#define APISERVER_H

#include <esp_https_server.h>

httpd_handle_t initServer();

void disconnectServer(httpd_handle_t server);

#endif