/*
 * wheel_speeds.h
 *
 *  Created on: Jun 14, 2019
 *      Author: Devin
 */

#ifndef WHEEL_SPEEDS_H_
#define WHEEL_SPEEDS_H_

typedef enum
{
   R=0,
   L,
   NUM_WHEELS
} Wheel_Sensor_T;

typedef struct
{
   float r;
   float l;
} Wheel_Speeds_T;

extern void Init_Wheel_Speed_Sensors(void);
extern void Measure_Wheel_Speeds(void);
extern void Get_Wheel_Ang_Velocities(Wheel_Speeds_T * ang_velocities);
extern void Zero_Wheel_Speed(Wheel_Sensor_T sensor);

#endif /* WHEEL_SPEEDS_H_ */
