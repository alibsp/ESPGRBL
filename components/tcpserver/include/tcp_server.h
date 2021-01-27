#ifndef TCPSERVER_H
#define TCPSERVER_H


/*#ifdef __cplusplus
extern "C"
{
#endif
*/

#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "tcpip_adapter.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#define PORT 1000

extern QueueHandle_t websocket_queue;


void tcp_server_task(void *pvParameters);
int  tcpServerSendTextClient(char* msg, uint64_t len);


/*#ifdef __cplusplus
}
#endif
*/

#endif // ifndef TCPSERVER_H
