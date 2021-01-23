/*
  serial.cpp - Header for system level commands and real-time processes
  Part of Grbl
  Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC

    2018 -	Bart Dring This file was modified for use on the ESP32
                    CPU. Do not use this with Grbl for atMega328P

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "grbl.h"
//#include "esp-i queue.c"
#include "commands.h"
#include "serial2socket.h"
#include "hardwareserial.h"
#include "config.h"
#define RX_RING_BUFFER (RX_BUFFER_SIZE+1)
#define TX_RING_BUFFER (TX_BUFFER_SIZE+1)
char mess[20];
bool state_one =false;
bool state_two =false;
bool state_three =false;
bool state_four =false;
int numberOfSpacesInGcodeQueue = 0;
extern int gline;
extern QueueHandle_t websocket_queue;
extern QueueHandle_t gcode_queue;
void protocol_process_gcode(void *pvParameters);
portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;

uint8_t serial_rx_buffer[CLIENT_COUNT][RX_RING_BUFFER];
uint8_t serial_rx_buffer_head[CLIENT_COUNT] = {0};
volatile uint8_t serial_rx_buffer_tail[CLIENT_COUNT] = {0};
static TaskHandle_t serialCheckTaskHandle = 0;
static TaskHandle_t protocolProcessTaskHandle = 0;
extern char line_buffer[][20];

typedef struct QueueDefinition
{
    int8_t *pcHead;					/*< Points to the beginning of the queue storage area. */
    int8_t *pcTail;					/*< Points to the byte at the end of the queue storage area.  Once more byte is allocated than necessary to store the queue items, this is used as a marker. */
    int8_t *pcWriteTo;				/*< Points to the free next place in the storage area. */

    union							/* Use of a union is an exception to the coding standard to ensure two mutually exclusive structure members don't appear simultaneously (wasting RAM). */
    {
        int8_t *pcReadFrom;			/*< Points to the last place that a queued item was read from when the structure is used as a queue. */
        UBaseType_t uxRecursiveCallCount;/*< Maintains a count of the number of times a recursive mutex has been recursively 'taken' when the structure is used as a mutex. */
    } u;

    List_t xTasksWaitingToSend;		/*< List of tasks that are blocked waiting to post onto this queue.  Stored in priority order. */
    List_t xTasksWaitingToReceive;	/*< List of tasks that are blocked waiting to read from this queue.  Stored in priority order. */

    volatile UBaseType_t uxMessagesWaiting;/*< The number of items currently in the queue. */
    UBaseType_t uxLength;			/*< The length of the queue defined as the number of items it will hold, not the number of bytes. */
    UBaseType_t uxItemSize;			/*< The size of each items that the queue will hold. */

    #if( ( configSUPPORT_STATIC_ALLOCATION == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
        uint8_t ucStaticallyAllocated;	/*< Set to pdTRUE if the memory used by the queue was statically allocated to ensure no attempt is made to free the memory. */
    #endif

    #if ( configUSE_QUEUE_SETS == 1 )
        struct QueueDefinition *pxQueueSetContainer;
    #endif

    #if ( configUSE_TRACE_FACILITY == 1 )
        UBaseType_t uxQueueNumber;
        uint8_t ucQueueType;
    #endif

    portMUX_TYPE mux;		//Mutex required due to SMP

} xQUEUE;

void serial_init()
{
    Serial.begin(BAUD_RATE);
    serialCheckTaskHandle = 0;
    // create a task to check for incoming data
    xTaskCreatePinnedToCore(serialCheckTask,    // task
                            "serialCheckTask", // name for task
                            8192,   // size of task stack
                            NULL,   // parameters
                            1, // priority
                            &serialCheckTaskHandle,
                            0 // core
                            );
    xTaskCreatePinnedToCore(protocol_process_gcode, // task
                            "protocol_process_gcode", // name for task
                            8192,   // size of task stack
                            NULL,   // parameters
                            1, // priority
                            &protocolProcessTaskHandle,
                            1 // core
                            );

}

char G_code[LINE_BUFFER_SIZE];
// this task runs and checks for data on all interfaces
// REaltime stuff is acted upon, then characters are added to the appropriate buffer
void serialCheckTask(void *pvParameters)
{

    while(true) // run continuously
    {
        memset(G_code,0x0,80);
        printf("serialCheckTask1 \n");
        xQueueReceive ( websocket_queue, G_code, portMAX_DELAY );
        printf("serialCheckTask2 g_code = %s %d\n", G_code, G_code[0]);
        // Pick off realtime command characters directly from the serial stream. These characters are
        // not passed into the main buffer, but these set system state flag bits for realtime execution.
        switch (G_code[0])
        {
            case CMD_RESET:
                mc_reset();   // Call motion control reset routine.
                //report_init_message(client); // fool senders into thinking a reset happened.
                break;
            case CMD_STATUS_REPORT:
                report_realtime_status();
                //grbl_send(0, "message: Command status report\n");
                break; // direct call instead of setting flag
            case CMD_CYCLE_START:
                system_set_exec_state_flag(EXEC_CYCLE_START);
                break; // Set as true
            case CMD_FEED_HOLD:
                system_set_exec_state_flag(EXEC_FEED_HOLD);
                break; // Set as true
            default :
                numberOfSpacesInGcodeQueue = uxQueueSpacesAvailable(gcode_queue);
                //if(numberOfSpacesInGcodeQueue > 18)
                //printf("number of spaces in gcode_queue = %d\n",numberOfSpacesInGcodeQueue);
                printf("serialCheckTask default gcode_queue len:%d, numberOfSpacesInGcodeQueue:%d\n", ((xQUEUE*)gcode_queue)->uxMessagesWaiting, numberOfSpacesInGcodeQueue);
                if( xQueueSendToBack( gcode_queue, G_code, ( TickType_t ) 50 ) != pdPASS )
                {
                    printf("Failed to post the message on gcode Queue after 50 ticks\n");
                }


        }
        vTaskDelay(1 / portTICK_RATE_MS);  // Yield to other tasks
    }
}

// ==================== call this in main protocol loop if you want it in the main task =========
// be sure to stop task.
// Realtime stuff is acted upon, then characters are added to the appropriate buffer
// void serialCheck()
// {

// 	int i =-1;
// 	//printf("serial check core %d\n", xPortGetCoreID());
// 	char data[20];
// 	uint8_t next_head;
// 	uint8_t client = CLIENT_SERIAL; // who send the data

// 	uint8_t client_idx = 0;  // index of data buffer


// 		while (strlen(mess) >0)

// 		{			
// 			printf("line buff len1  = %d",strlen(mess));
// 			//for (int i=0;i !=strlen(line_bufferA);i++)
// 			while (strlen(mess) > 0)
// 			{  i ++;
// 			//printf("data = %s", line_bufferA);

// 			// Pick off realtime command characters directly from the serial stream. These characters are
// 			// not passed into the main buffer, but these set system state flag bits for realtime execution.
// 			switch (line_bufferA[i]) {
// 				case CMD_RESET:         mc_reset(); break; // Call motion control reset routine.
// 				case CMD_STATUS_REPORT: 
// 					mess[0] = 0;	
// 					mess[0] = '\0';
// 					printf("line buff len2  = %d",strlen(mess));
// 				//	data = Serial2Socket.read();  //bob
// 					report_realtime_status(client);
// 					break; // direct call instead of setting flag
// 				case CMD_CYCLE_START:   system_set_exec_state_flag(EXEC_CYCLE_START); break; // Set as true
// 				case CMD_FEED_HOLD:     system_set_exec_state_flag(EXEC_FEED_HOLD); break; // Set as true
// 				default :
// 					if (line_bufferA[i] > 0x7F) { // Real-time control characters are extended ACSII only.
// 						switch(mess[0]) {
// 							case CMD_SAFETY_DOOR:   system_set_exec_state_flag(EXEC_SAFETY_DOOR); break; // Set as true
// 							case CMD_JOG_CANCEL:   
// 								if (sys.state & STATE_JOG) { // Block all other states from invoking motion cancel.
// 									system_set_exec_state_flag(EXEC_MOTION_CANCEL); 
// 								}
// 								break; 
// 							#ifdef DEBUG
// 								case CMD_DEBUG_REPORT: {uint8_t sreg = SREG; cli(); bit_true(sys_rt_exec_debug,EXEC_DEBUG_REPORT); SREG = sreg;} break;
// 							#endif
// 							case CMD_FEED_OVR_RESET: system_set_exec_motion_override_flag(EXEC_FEED_OVR_RESET); break;
// 							case CMD_FEED_OVR_COARSE_PLUS: system_set_exec_motion_override_flag(EXEC_FEED_OVR_COARSE_PLUS); break;
// 							case CMD_FEED_OVR_COARSE_MINUS: system_set_exec_motion_override_flag(EXEC_FEED_OVR_COARSE_MINUS); break;
// 							case CMD_FEED_OVR_FINE_PLUS: system_set_exec_motion_override_flag(EXEC_FEED_OVR_FINE_PLUS); break;
// 							case CMD_FEED_OVR_FINE_MINUS: system_set_exec_motion_override_flag(EXEC_FEED_OVR_FINE_MINUS); break;
// 							case CMD_RAPID_OVR_RESET: system_set_exec_motion_override_flag(EXEC_RAPID_OVR_RESET); break;
// 							case CMD_RAPID_OVR_MEDIUM: system_set_exec_motion_override_flag(EXEC_RAPID_OVR_MEDIUM); break;
// 							case CMD_RAPID_OVR_LOW: system_set_exec_motion_override_flag(EXEC_RAPID_OVR_LOW); break;
// 							case CMD_SPINDLE_OVR_RESET: system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_RESET); break;
// 							case CMD_SPINDLE_OVR_COARSE_PLUS: system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_COARSE_PLUS); break;
// 							case CMD_SPINDLE_OVR_COARSE_MINUS: system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_COARSE_MINUS); break;
// 							case CMD_SPINDLE_OVR_FINE_PLUS: system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_FINE_PLUS); break;
// 							case CMD_SPINDLE_OVR_FINE_MINUS: system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_FINE_MINUS); break;
// 							case CMD_SPINDLE_OVR_STOP: system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_STOP); break;
// 							#ifdef COOLANT_FLOOD_PIN
// 							case CMD_COOLANT_FLOOD_OVR_TOGGLE: system_set_exec_accessory_override_flag(EXEC_COOLANT_FLOOD_OVR_TOGGLE); break;
// 							#endif
// 							#ifdef COOLANT_MIST_PIN
// 								case CMD_COOLANT_MIST_OVR_TOGGLE: system_set_exec_accessory_override_flag(EXEC_COOLANT_MIST_OVR_TOGGLE); break;
// 							#endif
// 						}
// 						// Throw away any unfound extended-ASCII character by not passing it to the serial buffer.
// 					} else { // Write character to buffer

// 						vTaskEnterCritical(&myMutex);
// 						next_head = serial_rx_buffer_head[client_idx] + 1;
// 						if (next_head == RX_RING_BUFFER) { next_head = 0; }

// 						// Write data to buffer unless it is full.
// 						if (next_head != serial_rx_buffer_tail[client_idx]) {
// 							serial_rx_buffer[client_idx][serial_rx_buffer_head[client_idx]] = mess[i];

// 							serial_rx_buffer_head[client_idx] = next_head;
// 						}	
// 						vTaskExitCritical(&myMutex);
// 						printf("serial buff = %c",mess[i]);							
// 					}
// 			}  // switch data	
// 			}	mess[0] = '\0';	
// 				i=-1;		
// 		}  // if something available	


// #ifdef ENABLE_BLUETOOTH
// 		//bt_config.handle();
// #endif
// #if defined (ENABLE_WIFI) && defined(ENABLE_HTTP) && defined(ENABLE_SERIAL2SOCKET_IN)
// 		Serial2Socket.handle_flush();
// #endif




// }


// Writes one byte to the TX serial buffer. Called by main program.
void serial_write(uint8_t data) {
    Serial.write((char)data);
}



void serial_store( char abA[])
{
    printf("store-len aba  = %d\n",strlen(abA));

    memcpy(mess,abA,strlen(abA));
    mess[strlen(abA)] = '\0';
    printf("store = %s",mess);
    printf("store-len = %d",strlen(mess));

}


