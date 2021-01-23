/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "tcp_server.h"
static const char *TAG = "TCPServer";

void tcp_server_task(void *pvParameters)
{

    char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;

    inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);

    // IPV6
//    struct sockaddr_in6 dest_addr;
//    bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
//    dest_addr.sin6_family = AF_INET6;
//    dest_addr.sin6_port = htons(PORT);
//    addr_family = AF_INET6;
//    ip_protocol = IPPROTO_IPV6;
//    inet6_ntoa_r(dest_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);

    tcpClientConnected = false;
    while(1)
    {
        listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (listen_sock < 0)
        {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

        int err = bind(listen_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (err != 0)
        {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket bound, port %d", PORT);

        err = listen(listen_sock, 1);
        if (err != 0)
        {
            ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
        uint addr_len = sizeof(source_addr);
        tcpClientConnected = false;
        while (1)
        {
            currentClient = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
            if (currentClient < 0)
            {
                ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
                break;
            }
            ESP_LOGI(TAG, "Socket accepted");
            tcpClientConnected = true;
            while (1)
            {
                int len = recv(currentClient, rx_buffer, sizeof(rx_buffer) - 1, 0);
                // Error occurred during receiving
                if (len < 0)
                {
                    ESP_LOGE(TAG, "Recv failed: errno %d", errno);
                    break;
                }
                // Connection closed
                else if (len == 0)
                {
                    ESP_LOGI(TAG, "Connection closed");
                    break;
                }
                // Data received
                else
                {
                    // Get the sender's ip address as string
                    if (source_addr.sin6_family == PF_INET)
                    {
                        inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                    } else if (source_addr.sin6_family == PF_INET6)
                    {
                        inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
                    }

                    rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                    ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                    ESP_LOGI(TAG, "%s", rx_buffer);

                    if(len)
                    {
                        //   printf("main.cpp websocket_callback msg = %s\n",msg[i]);
                        if( xQueueSendToBack( websocket_queue, rx_buffer, ( TickType_t ) 500 ) != pdPASS )
                            printf("Failed to post the message on websocket Queue after 50 ticks\n");
                    }

                }
            }

            if (currentClient != -1)
            {
                ESP_LOGE(TAG, "Shutting down socket and restarting...");
                shutdown(currentClient, 0);
                close(currentClient);
            }
        }
    }
    vTaskDelete(NULL);
}

int tcpServerSendTextClient(char* msg, uint64_t len)
{
    //xSemaphoreTake(xwebsocket_mutex,portMAX_DELAY);

    int ret = send(currentClient, msg, len, 0);
    if (ret < 0)
    {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d \n", errno);
    }
    //xSemaphoreGive(xwebsocket_mutex);
    return ret;
}
