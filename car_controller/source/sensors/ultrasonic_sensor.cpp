/*
 * ultrasonic_sensor.cpp
 *
 *  Created on: Jul 4, 2019
 *      Author: Devin
 */

#include "ultrasonic_sensor.h"
#include "clock_config.h"
#include "assert.h"
#include "fsl_common.h"
#include "interrupt_prios.h"

extern "C"
{
#include "ftm_isr_router.h"
#include "port_isr_router.h"
}

#define ISR_Flag_Is_Set(input) ((Pin_Cfgs[input].pbase->PCR[Pin_Cfgs[input].pin] >> PORT_PCR_ISF_SHIFT) && (uint32_t) 0x01)
#define Clear_ISR_Flag(input)  Pin_Cfgs[input].pbase->PCR[Pin_Cfgs[input].pin] |= PORT_PCR_ISF(1)

#define FTM_SOURCE_CLOCK (CLOCK_GetFreq(kCLOCK_BusClk)/16)
#define TRIG_PULSE_TIME (15) /* microseconds */

#define SPEED_OF_SOUND (343.0f) /* m/s */

static float Pulse_Width_2_Dist_Factor;
static volatile USS_Working_Info_T * USS_Working_Info[NUM_FTMS];

extern "C"
{
static void USS_FTM_IRQHandler(uint8_t ftm_num);
static void USS_PORT_IRQHandler(uint32_t port_base);
}

void UltrasonicSensor::Init(FTM_Type *ftm_base_ptr, IO_Map_T trig_pin, IO_Map_T echo_pin)
{
   ftm_config_t ftm_info;
   uint8_t ftm_num = 0;
   port_interrupt_t p_int_cfg;

   /* Check for null pointer */
   assert(ftm_base_ptr);

   /* Save the parameters in the instance variables */
   working_info.ftm_ptr = ftm_base_ptr;
   working_info.trig    = trig_pin;
   working_info.echo    = echo_pin;

   /* Determine which FTM will be used */
   switch((uint32_t)ftm_base_ptr)
   {
      case FTM0_BASE:
         EnableIRQ(FTM0_IRQn);
         ftm_num = 0;
         break;
      case FTM1_BASE:
         EnableIRQ(FTM1_IRQn);
         ftm_num = 1;
         break;
      case FTM2_BASE:
         EnableIRQ(FTM2_IRQn);
         ftm_num = 2;
         break;
      case FTM3_BASE:
         EnableIRQ(FTM3_IRQn);
         ftm_num = 3;
         break;
      default:
         /* Invalid ftm_ptr */
         assert(false);
   }

   /* Assign a pointer to the working info for this instance */
   USS_Working_Info[ftm_num] = &working_info;

   /* Initialize the FTM */
   FTM_GetDefaultConfig(&ftm_info);

   /* Divide FTM clock by 16 */
   ftm_info.prescale = kFTM_Prescale_Divide_16;

   FTM_Init(working_info.ftm_ptr, &ftm_info);

   Reroute_FTM_ISR(ftm_num, &USS_FTM_IRQHandler);

   /* Configure the port interrupt for the echo pin */
   switch((uint32_t)Pin_Cfgs[working_info.echo].pbase)
   {
      case PORTA_BASE:
         Reroute_Port_ISR(PORTA_BASE, &USS_PORT_IRQHandler);
         working_info.echo_irq = PORTA_IRQn;
         break;
      case PORTB_BASE:
         Reroute_Port_ISR(PORTB_BASE, &USS_PORT_IRQHandler);
         working_info.echo_irq = PORTB_IRQn;
         break;
      case PORTC_BASE:
         Reroute_Port_ISR(PORTC_BASE, &USS_PORT_IRQHandler);
         working_info.echo_irq = PORTC_IRQn;
         break;
      case PORTD_BASE:
         Reroute_Port_ISR(PORTD_BASE, &USS_PORT_IRQHandler);
         working_info.echo_irq = PORTD_IRQn;
         break;
      case PORTE_BASE:
         Reroute_Port_ISR(PORTE_BASE, &USS_PORT_IRQHandler);
         working_info.echo_irq = PORTE_IRQn;
         break;
      default:
         assert(false);
   }

   NVIC_SetPriority(working_info.echo_irq, USS_ECHO_PRIO);

   p_int_cfg = kPORT_InterruptEitherEdge;
   PORT_SetPinInterruptConfig(Pin_Cfgs[USS_Working_Info[ftm_num]->echo].pbase, \
                              Pin_Cfgs[USS_Working_Info[ftm_num]->echo].pin, p_int_cfg);

   PORT_ClearPinsInterruptFlags(Pin_Cfgs[working_info.echo].pbase, 0xFFFFFFFF);
   EnableIRQ(USS_Working_Info[ftm_num]->echo_irq);

   /* Pre-calculate this factor to reduce future computations */
   Pulse_Width_2_Dist_Factor = (1.0f/FTM_SOURCE_CLOCK)*SPEED_OF_SOUND/2;
}

void UltrasonicSensor::Trigger(void)
{
   FTM_StopTimer(working_info.ftm_ptr);
   working_info.ftm_ptr->CNT = 0;
   FTM_SetTimerPeriod(working_info.ftm_ptr, USEC_TO_COUNT(TRIG_PULSE_TIME, FTM_SOURCE_CLOCK));

   Set_GPIO(working_info.trig, HIGH);
   working_info.state = TRIG;

   FTM_StartTimer(working_info.ftm_ptr, kFTM_SystemClock);

   FTM_EnableInterrupts(working_info.ftm_ptr, kFTM_TimeOverflowInterruptEnable);
}

float UltrasonicSensor::Get_Obj_Dist(void)
{
   float dist = 0;

   if ((working_info.end_cnt > working_info.start_cnt) && (working_info.start_cnt != 0))
   {
      dist = (working_info.end_cnt - working_info.start_cnt)*Pulse_Width_2_Dist_Factor;
   }
   else
   {
      dist = ((UINT16_MAX - working_info.start_cnt) + working_info.end_cnt)*Pulse_Width_2_Dist_Factor;
   }

   return (dist);
}

extern "C"
{
void USS_FTM_IRQHandler(uint8_t ftm_num)
{
   FTM_StopTimer(USS_Working_Info[ftm_num]->ftm_ptr);
   USS_Working_Info[ftm_num]->ftm_ptr->CNT = 0;
   FTM_SetTimerPeriod(USS_Working_Info[ftm_num]->ftm_ptr, UINT16_MAX);

   if (TRIG == USS_Working_Info[ftm_num]->state)
   {
      Set_GPIO(USS_Working_Info[ftm_num]->trig, LOW);
      USS_Working_Info[ftm_num]->state = WAIT_ECHO_START;
      EnableIRQ(USS_Working_Info[ftm_num]->echo_irq);
   }
   else
   {
      assert(false);
   }

   FTM_StartTimer(USS_Working_Info[ftm_num]->ftm_ptr, kFTM_SystemClock);
   FTM_DisableInterrupts(USS_Working_Info[ftm_num]->ftm_ptr, kFTM_TimeOverflowInterruptEnable);

   /* Clear interrupt flag.*/
    FTM_ClearStatusFlags(USS_Working_Info[ftm_num]->ftm_ptr, kFTM_TimeOverflowFlag);
    __DSB();
}

void USS_PORT_IRQHandler(uint32_t port_base)
{
   uint8_t inst_num;
   uint32_t cnt;

   /* Find the class instance that is using this port interrupt */
   for (inst_num=0; inst_num<NUM_FTMS; inst_num++)
   {
      if (NULL != USS_Working_Info[inst_num])
      {
         if (port_base == (uint32_t)Pin_Cfgs[USS_Working_Info[inst_num]->echo].pbase)
         {
            break;
         }
      }
   }

   if (ISR_Flag_Is_Set(USS_Working_Info[inst_num]->echo))
   {
      Clear_ISR_Flag(USS_Working_Info[inst_num]->echo);

      cnt = FTM_GetCurrentTimerCount(USS_Working_Info[inst_num]->ftm_ptr);

      if (WAIT_ECHO_START == USS_Working_Info[inst_num]->state)
      {
         USS_Working_Info[inst_num]->start_cnt = cnt;
         USS_Working_Info[inst_num]->state = WAIT_ECHO_END;
      }
      else if (WAIT_ECHO_END == USS_Working_Info[inst_num]->state)
      {
         USS_Working_Info[inst_num]->end_cnt = cnt;
         USS_Working_Info[inst_num]->state = IDLE;
         DisableIRQ(USS_Working_Info[inst_num]->echo_irq);
      }
   }
}
}

