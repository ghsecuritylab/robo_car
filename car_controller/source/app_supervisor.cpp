/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

/* Application */
#include "app_supervisor.h"
#include "data_logger.h"

#include "motor_controls.h"
#include "behaviors.h"
#include "servo.h"
#include "wheel_speeds.h"
#include "bluetooth_control.h"
#include "battery_monitor.h"
#include "logging_streams.h"
#include "object_detection.h"
#include "constants.h"
#include "ip_app_iface.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#if USE_ETHERNET
static void Supervisor_Task(void *pvParameters);
#endif
static void Create_App_Tasks(void);

/*******************************************************************************
* Variables
******************************************************************************/


/*******************************************************************************
 * Function Definitions
 ******************************************************************************/
void Init_App(void)
{
   Init_Battery_Monitor();
   Init_Wheel_Speed_Sensors();
   Init_Motor_Controls();
   Init_Object_Detection();
   Bluetooth_Serial_Open();
   Create_Streams();

   NVIC_SetPriorityGrouping(0U);

#if USE_ETHERNET
   Init_Network_If();
   xTaskCreate(Supervisor_Task,   "Supervisor",  512,  NULL, SUPERVISOR_PRIO,  NULL);
#else
   Create_App_Tasks();
#endif
   vTaskStartScheduler();
}

static void Create_App_Tasks(void)
{
   xTaskCreate(SD_Card_Init_Task,    "SD_Card_Init_Task",   1024, NULL, SD_CARD_INIT_TASK_PRIO,     NULL);
   xTaskCreate(Init_Behaviors_Task,  "Behaviors_Init_Task", 512,  NULL, BEHAVIORS_TASK_PRIO,        NULL);
   xTaskCreate(Bluetooth_Cmd_Task,   "Bluetooth_Control",   512,  NULL, BLUETOOTH_CMD_TASK_PRIO,    NULL);
   xTaskCreate(Log_MC_Stream_Task,   "MC_Logging_Task",     1024, NULL, MC_DATA_LOGGING_TASK_PRIO,  NULL);
}

#if USE_ETHERNET
static void Supervisor_Task(void *pvParameters)
{
   bool app_tasks_created = false;

   while(1)
   {
      Print_DHCP_State();
      if (CONNECTED == Get_Network_Status())
      {
         if (!app_tasks_created)
         {
            Create_App_Tasks();
            app_tasks_created = true;
         }
      }

      vTaskDelay(pdMS_TO_TICKS(50));
   }
}
#endif
