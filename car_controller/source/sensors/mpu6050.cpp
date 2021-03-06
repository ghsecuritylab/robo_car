#include <math.h>
#include <assert.h>

#include "mpu6050.h"

/* Seven-bit device address is 110100 for ADO = 0 and 110101 for ADO = 1 */
#define ADO 0
#if ADO
#define MPU6050_ADDRESS 0x69  // Device address when ADO = 1
#else
#define MPU6050_ADDRESS 0x68  // Device address when ADO = 0
#endif
#define SELFTEST_PASS_THRESHOLD (1.0f) /* (%) */
#define G (9.81f)
#define TEMP_SENSITIVITY (340.0f)
#define TEMP_OFFSET (36.53)

#define X 0
#define Y 1
#define Z 2

bool MPU6050::Test_Basic_I2C(void)
{
   bool test_passed = false;
   uint8_t actual_addr = 0;

   actual_addr = Read_Byte(MPU6050_ADDRESS, WHO_AM_I_MPU6050);

   if (actual_addr == (uint8_t) MPU6050_ADDRESS)
   {
      test_passed = true;
   }

   return (test_passed);
}

// Configure the motion detection control for low power accelerometer mode
void MPU6050::Low_Power_Accel_Only(void)
{
   // The sensor has a high-pass filter necessary to invoke to allow the sensor motion detection algorithms work properly
   // Motion detection occurs on free-fall (acceleration below a threshold for some time for all axes), motion (acceleration
   // above a threshold for some time on at least one axis), and zero-motion toggle (acceleration on each axis less than a
   // threshold for some time sets this flag, motion above the threshold turns it off). The high-pass filter takes gravity out
   // consideration for these threshold evaluations; otherwise, the flags would be set all the time!

   uint8_t c = Read_Byte(MPU6050_ADDRESS, PWR_MGMT_1);
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_1, c & ~0x30); // Clear sleep and cycle bits [5:6]
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_1, c |  0x30); // Set sleep and cycle bits [5:6] to zero to make sure accelerometer is running

   c = Read_Byte(MPU6050_ADDRESS, PWR_MGMT_2);
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_2, c & ~0x38); // Clear standby XA, YA, and ZA bits [3:5]
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_2, c |  0x00); // Set XA, YA, and ZA bits [3:5] to zero to make sure accelerometer is running

   c = Read_Byte(MPU6050_ADDRESS, ACCEL_CONFIG);
   Write_Byte(MPU6050_ADDRESS, ACCEL_CONFIG, c & ~0x07); // Clear high-pass filter bits [2:0]
   // Set high-pass filter to 0) reset (disable), 1) 5 Hz, 2) 2.5 Hz, 3) 1.25 Hz, 4) 0.63 Hz, or 7) Hold
   Write_Byte(MPU6050_ADDRESS, ACCEL_CONFIG,  c | 0x00);  // Set ACCEL_HPF to 0; reset mode disbaling high-pass filter

   c = Read_Byte(MPU6050_ADDRESS, CONFIG);
   Write_Byte(MPU6050_ADDRESS, CONFIG, c & ~0x07); // Clear low-pass filter bits [2:0]
   Write_Byte(MPU6050_ADDRESS, CONFIG, c |  0x00);  // Set DLPD_CFG to 0; 260 Hz bandwidth, 1 kHz rate

   c = Read_Byte(MPU6050_ADDRESS, INT_ENABLE);
   Write_Byte(MPU6050_ADDRESS, INT_ENABLE, c & ~0xFF);  // Clear all interrupts
   Write_Byte(MPU6050_ADDRESS, INT_ENABLE, 0x40);  // Enable motion threshold (bits 5) interrupt only

   // Motion detection interrupt requires the absolute value of any axis to lie above the detection threshold
   // for at least the counter duration
   Write_Byte(MPU6050_ADDRESS, MOT_THR, 0x80); // Set motion detection to 0.256 g; LSB = 2 mg
   Write_Byte(MPU6050_ADDRESS, MOT_DUR, 0x01); // Set motion detect duration to 1  ms; LSB is 1 ms @ 1 kHz rate

   Delay(100);  // Add Delay for accumulation of samples

   c = Read_Byte(MPU6050_ADDRESS, ACCEL_CONFIG);
   Write_Byte(MPU6050_ADDRESS, ACCEL_CONFIG, c & ~0x07); // Clear high-pass filter bits [2:0]
   Write_Byte(MPU6050_ADDRESS, ACCEL_CONFIG, c |  0x07);  // Set ACCEL_HPF to 7; hold the initial accleration value as a referance

   c = Read_Byte(MPU6050_ADDRESS, PWR_MGMT_2);
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_2, c & ~0xC7); // Clear standby XA, YA, and ZA bits [3:5] and LP_WAKE_CTRL bits [6:7]
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_2, c |  0x47); // Set wakeup frequency to 5 Hz, and disable XG, YG, and ZG gyros (bits [0:2])

   c = Read_Byte(MPU6050_ADDRESS, PWR_MGMT_1);
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_1, c & ~0x20); // Clear sleep and cycle bit 5
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_1, c |  0x20); // Set cycle bit 5 to begin low power accelerometer motion interrupts
}

void MPU6050::Init(MPU6050_Ascale_T ascale, MPU6050_Gscale_T gscale)
{
   uint8_t c;

   a_scale = ascale;
   g_scale = gscale;

   Init_I2C_If();

   if (true == Test_Basic_I2C())
   {
      // Reset the device
      Write_Byte(MPU6050_ADDRESS, PWR_MGMT_1, 0x80);
      Delay(100);
   }

   if (true == Test_Basic_I2C())
   {
      Run_Self_Test();
      Calibrate();
      Set_Accel_Res(ascale);
      Set_Gyro_Res(gscale);
   }
   else
   {
      assert(0);
   }

   // get stable time source
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_1, 0x01);  // Set clock source to be PLL with x-axis gyroscope reference, bits 2:0 = 001

   // Configure Gyro and Accelerometer
   // Disable FSYNC and set accelerometer and gyro bandwidth to 44 and 42 Hz, respectively;
   // DLPF_CFG = bits 2:0 = 010; this sets the sample rate at 1 kHz for both
   // Maximum Delay time is 4.9 ms corresponding to just over 200 Hz sample rate
   Write_Byte(MPU6050_ADDRESS, CONFIG, 0x03);
   c = Read_Byte(MPU6050_ADDRESS, CONFIG);

   // Set sample rate = gyroscope output rate/(1 + SMPLRT_DIV)
   Write_Byte(MPU6050_ADDRESS, SMPLRT_DIV, 0x04);  // Use a 200 Hz rate; the same rate set in CONFIG above

   // Set gyroscope full scale range
   // Range selects FS_SEL and AFS_SEL are 0 - 3, so 2-bit values are left-shifted into positions 4:3
   c = Read_Byte(MPU6050_ADDRESS, GYRO_CONFIG);
   Write_Byte(MPU6050_ADDRESS, GYRO_CONFIG, c & ~0xE0); // Clear self-test bits [7:5]
   Write_Byte(MPU6050_ADDRESS, GYRO_CONFIG, c & ~0x18); // Clear AFS bits [4:3]
   Write_Byte(MPU6050_ADDRESS, GYRO_CONFIG, c | g_scale << 3); // Set full scale range for the gyro

   // Set accelerometer configuration
   c = Read_Byte(MPU6050_ADDRESS, ACCEL_CONFIG);
   Write_Byte(MPU6050_ADDRESS, ACCEL_CONFIG, c & ~0xE0); // Clear self-test bits [7:5]
   Write_Byte(MPU6050_ADDRESS, ACCEL_CONFIG, c & ~0x18); // Clear AFS bits [4:3]
   Write_Byte(MPU6050_ADDRESS, ACCEL_CONFIG, c | a_scale << 3); // Set full scale range for the accelerometer

   // Configure Interrupts and Bypass Enable
   // Set interrupt pin active high, push-pull, and clear on read of INT_STATUS, enable I2C_BYPASS_EN so additional chips
   // can join the I2C bus and all can be controlled by the Arduino as master
   Write_Byte(MPU6050_ADDRESS, INT_PIN_CFG, 0x22);
   Write_Byte(MPU6050_ADDRESS, INT_ENABLE, 0x01);  // Enable data ready (bit 0) interrupt

   Delay(50);

   // Wait for data to be available
   while(!(Read_Byte(MPU6050_ADDRESS, INT_STATUS) & 0x01));
}

// Function which accumulates gyro and accelerometer data after device initialization. It calculates the average
// of the at-rest readings and then loads the resulting offsets into accelerometer and gyro bias registers.
void MPU6050::Calibrate(void)
{
   uint8_t data[12]; // data array to hold accelerometer and gyro x, y, z, data
   uint16_t ii, packet_count, fifo_count;
   int32_t gyro_bias[3] = {0, 0, 0}, accel_bias[3] = {0, 0, 0};

   // reset device, reset all registers, clear gyro and accelerometer bias registers
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_1, 0x80); // Write a one to bit 7 reset bit; toggle reset device
   Delay(100);

   // get stable time source
   // Set clock source to be PLL with x-axis gyroscope reference, bits 2:0 = 001
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_1, 0x01);
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_2, 0x00);
   Delay(200);

   // Configure device for bias calculation
   Write_Byte(MPU6050_ADDRESS, INT_ENABLE, 0x00);   // Disable all interrupts
   Write_Byte(MPU6050_ADDRESS, FIFO_EN, 0x00);      // Disable FIFO
   Write_Byte(MPU6050_ADDRESS, PWR_MGMT_1, 0x00);   // Turn on internal clock source
   Write_Byte(MPU6050_ADDRESS, I2C_MST_CTRL, 0x00); // Disable I2C master
   Write_Byte(MPU6050_ADDRESS, USER_CTRL, 0x00);    // Disable FIFO and I2C master modes
   Write_Byte(MPU6050_ADDRESS, USER_CTRL, 0x0C);    // Reset FIFO and DMP
   Delay(15);

   // Configure MPU6050 gyro and accelerometer for bias calculation
   Write_Byte(MPU6050_ADDRESS, CONFIG, 0x01);      // Set low-pass filter to 188 Hz
   Write_Byte(MPU6050_ADDRESS, SMPLRT_DIV, 0x00);  // Set sample rate to 1 kHz
   Write_Byte(MPU6050_ADDRESS, GYRO_CONFIG, 0x00);  // Set gyro full-scale to 250 degrees per second, maximum sensitivity
   Write_Byte(MPU6050_ADDRESS, ACCEL_CONFIG, 0x00); // Set accelerometer full-scale to 2 g, maximum sensitivity

   uint16_t  gyrosensitivity  = 131;   // = 131 LSB/degrees/sec
   uint16_t  accelsensitivity = 16384;  // = 16384 LSB/g

   // Configure FIFO to capture accelerometer and gyro data for bias calculation
   Write_Byte(MPU6050_ADDRESS, USER_CTRL, 0x40);   // Enable FIFO
   Write_Byte(MPU6050_ADDRESS, FIFO_EN, 0x78);     // Enable gyro and accelerometer sensors for FIFO  (max size 1024 bytes in MPU-6050)
   Delay(80); // accumulate 80 samples in 80 milliseconds = 960 bytes

   // At end of sample accumulation, turn off FIFO sensor read
   Write_Byte(MPU6050_ADDRESS, FIFO_EN, 0x00);        // Disable gyro and accelerometer sensors for FIFO
   Read_Bytes(MPU6050_ADDRESS, FIFO_COUNTH, 2, &data[0]); // read FIFO sample count
   fifo_count = ((uint16_t)data[0] << 8) | data[1];
   packet_count = fifo_count / 12; // How many sets of full gyro and accelerometer data for averaging

   for (ii = 0; ii < packet_count; ii++)
   {
      int16_t accel_temp[3] = {0, 0, 0}, gyro_temp[3] = {0, 0, 0};
      Read_Bytes(MPU6050_ADDRESS, FIFO_R_W, 12, &data[0]); // read data for averaging
      accel_temp[0] = (int16_t) (((int16_t)data[0] << 8) | data[1]  ) ;  // Form signed 16-bit integer for each sample in FIFO
      accel_temp[1] = (int16_t) (((int16_t)data[2] << 8) | data[3]  ) ;
      accel_temp[2] = (int16_t) (((int16_t)data[4] << 8) | data[5]  ) ;
      gyro_temp[0]  = (int16_t) (((int16_t)data[6] << 8) | data[7]  ) ;
      gyro_temp[1]  = (int16_t) (((int16_t)data[8] << 8) | data[9]  ) ;
      gyro_temp[2]  = (int16_t) (((int16_t)data[10] << 8) | data[11]) ;

      accel_bias[0] += (int32_t) accel_temp[0]; // Sum individual signed 16-bit biases to get accumulated signed 32-bit biases
      accel_bias[1] += (int32_t) accel_temp[1];
      accel_bias[2] += (int32_t) accel_temp[2];
      gyro_bias[0]  += (int32_t) gyro_temp[0];
      gyro_bias[1]  += (int32_t) gyro_temp[1];
      gyro_bias[2]  += (int32_t) gyro_temp[2];
   }

   accel_bias[0] /= (int32_t) packet_count; // Normalize sums to get average count biases
   accel_bias[1] /= (int32_t) packet_count;
   accel_bias[2] /= (int32_t) packet_count;
   gyro_bias[0]  /= (int32_t) packet_count;
   gyro_bias[1]  /= (int32_t) packet_count;
   gyro_bias[2]  /= (int32_t) packet_count;

   if (accel_bias[2] > 0L)
   {
      accel_bias[2] -= (int32_t) accelsensitivity; // Remove gravity from the z-axis accelerometer bias calculation
   }
   else
   {
      accel_bias[2] += (int32_t) accelsensitivity;
   }

   // Construct the gyro biases for push to the hardware gyro bias registers, which are reset to zero upon device startup
   data[0] = (-gyro_bias[0] / 4  >> 8) & 0xFF; // Divide by 4 to get 32.9 LSB per deg/s to conform to expected bias input format
   data[1] = (-gyro_bias[0] / 4)       & 0xFF; // Biases are additive, so change sign on calculated average gyro biases
   data[2] = (-gyro_bias[1] / 4  >> 8) & 0xFF;
   data[3] = (-gyro_bias[1] / 4)       & 0xFF;
   data[4] = (-gyro_bias[2] / 4  >> 8) & 0xFF;
   data[5] = (-gyro_bias[2] / 4)       & 0xFF;

   // Push gyro biases to hardware registers
   Write_Byte(MPU6050_ADDRESS, XG_OFFS_USRH, data[0]);
   Write_Byte(MPU6050_ADDRESS, XG_OFFS_USRL, data[1]);
   Write_Byte(MPU6050_ADDRESS, YG_OFFS_USRH, data[2]);
   Write_Byte(MPU6050_ADDRESS, YG_OFFS_USRL, data[3]);
   Write_Byte(MPU6050_ADDRESS, ZG_OFFS_USRH, data[4]);
   Write_Byte(MPU6050_ADDRESS, ZG_OFFS_USRL, data[5]);

   biases.gyro[X] = (float) gyro_bias[0] / (float) gyrosensitivity; // construct gyro bias in deg/s for later manual subtraction
   biases.gyro[Y] = (float) gyro_bias[1] / (float) gyrosensitivity;
   biases.gyro[Z] = (float) gyro_bias[2] / (float) gyrosensitivity;

   // Construct the accelerometer biases for push to the hardware accelerometer bias registers. These registers contain
   // factory trim values which must be added to the calculated accelerometer biases; on boot up these registers will hold
   // non-zero values. In addition, bit 0 of the lower byte must be preserved since it is used for temperature
   // compensation calculations. Accelerometer bias registers expect bias input as 2048 LSB per g, so that
   // the accelerometer biases calculated above must be divided by 8.

   int32_t accel_bias_reg[3] = {0, 0, 0}; // A place to hold the factory accelerometer trim biases
   Read_Bytes(MPU6050_ADDRESS, XA_OFFSET_H, 2, &data[0]); // Read factory accelerometer trim values
   accel_bias_reg[0] = (int16_t) ((int16_t)data[0] << 8) | data[1];
   Read_Bytes(MPU6050_ADDRESS, YA_OFFSET_H, 2, &data[0]);
   accel_bias_reg[1] = (int16_t) ((int16_t)data[0] << 8) | data[1];
   Read_Bytes(MPU6050_ADDRESS, ZA_OFFSET_H, 2, &data[0]);
   accel_bias_reg[2] = (int16_t) ((int16_t)data[0] << 8) | data[1];

   uint32_t mask = 1uL; // Define mask for temperature compensation bit 0 of lower byte of accelerometer bias registers
   uint8_t mask_bit[3] = {0, 0, 0}; // Define array to hold mask bit for each accelerometer bias axis

   for (ii = 0; ii < 3; ii++)
   {
      if (accel_bias_reg[ii] & mask)
      {
         mask_bit[ii] = 0x01; // If temperature compensation bit is set, record that fact in mask_bit
      }
   }

   // Construct total accelerometer bias, including calculated average accelerometer bias from above
   accel_bias_reg[0] -= (accel_bias[0] / 8); // Subtract calculated averaged accelerometer bias scaled to 2048 LSB/g (16 g full scale)
   accel_bias_reg[1] -= (accel_bias[1] / 8);
   accel_bias_reg[2] -= (accel_bias[2] / 8);

   data[0] = (accel_bias_reg[0] >> 8) & 0xFF;
   data[1] = (accel_bias_reg[0])      & 0xFF;
   data[1] = data[1] | mask_bit[0]; // preserve temperature compensation bit when writing back to accelerometer bias registers
   data[2] = (accel_bias_reg[1] >> 8) & 0xFF;
   data[3] = (accel_bias_reg[1])      & 0xFF;
   data[3] = data[3] | mask_bit[1]; // preserve temperature compensation bit when writing back to accelerometer bias registers
   data[4] = (accel_bias_reg[2] >> 8) & 0xFF;
   data[5] = (accel_bias_reg[2])      & 0xFF;
   data[5] = data[5] | mask_bit[2]; // preserve temperature compensation bit when writing back to accelerometer bias registers

   // Push accelerometer biases to hardware registers
   Write_Byte(MPU6050_ADDRESS, XA_OFFSET_H, data[0]);
   Write_Byte(MPU6050_ADDRESS, XA_OFFSET_L_TC, data[1]);
   Write_Byte(MPU6050_ADDRESS, YA_OFFSET_H, data[2]);
   Write_Byte(MPU6050_ADDRESS, YA_OFFSET_L_TC, data[3]);
   Write_Byte(MPU6050_ADDRESS, ZA_OFFSET_H, data[4]);
   Write_Byte(MPU6050_ADDRESS, ZA_OFFSET_L_TC, data[5]);

   // Output scaled accelerometer biases for manual subtraction in the main program
   biases.accel[X] = (float)accel_bias[0] / (float)accelsensitivity;
   biases.accel[Y] = (float)accel_bias[1] / (float)accelsensitivity;
   biases.accel[Z] = (float)accel_bias[2] / (float)accelsensitivity;
}

// Accelerometer and gyroscope self test; check calibration wrt factory settings
void MPU6050::Run_Self_Test(void) // Should return percent deviation from factory trim values, +/- 14 or less deviation is a pass
{
   uint8_t rawData[4];
   uint8_t selfTest[6];
   float factoryTrim[6], results[6];

   // Configure the accelerometer for self-test
   Write_Byte(MPU6050_ADDRESS, ACCEL_CONFIG, 0xF0); // Enable self test on all three axes and set accelerometer range to +/- 8 g
   Write_Byte(MPU6050_ADDRESS, GYRO_CONFIG,  0xE0); // Enable self test on all three axes and set gyro range to +/- 250 degrees/s
   Delay(250);  // Delay a while to let the device execute the self-test

   rawData[0] = Read_Byte(MPU6050_ADDRESS, SELF_TEST_X); // X-axis self-test results
   rawData[1] = Read_Byte(MPU6050_ADDRESS, SELF_TEST_Y); // Y-axis self-test results
   rawData[2] = Read_Byte(MPU6050_ADDRESS, SELF_TEST_Z); // Z-axis self-test results
   rawData[3] = Read_Byte(MPU6050_ADDRESS, SELF_TEST_A); // Mixed-axis self-test results

   // Extract the acceleration test results first
   selfTest[0] = (rawData[0] >> 3) | (rawData[3] & 0x30) >> 4 ; // XA_TEST result is a five-bit unsigned integer
   selfTest[1] = (rawData[1] >> 3) | (rawData[3] & 0x0C) >> 2 ; // YA_TEST result is a five-bit unsigned integer
   selfTest[2] = (rawData[2] >> 3) | (rawData[3] & 0x03) ; // ZA_TEST result is a five-bit unsigned integer

   // Extract the gyration test results first
   selfTest[3] = rawData[0]  & 0x1F ; // XG_TEST result is a five-bit unsigned integer
   selfTest[4] = rawData[1]  & 0x1F ; // YG_TEST result is a five-bit unsigned integer
   selfTest[5] = rawData[2]  & 0x1F ; // ZG_TEST result is a five-bit unsigned integer

   // Process results to allow final comparison with factory set values
   factoryTrim[0] = (4096.0 * 0.34) * (pow( (0.92 / 0.34) , (((float)selfTest[0] - 1.0) / 30.0))); // FT[Xa] factory trim calculation
   factoryTrim[1] = (4096.0 * 0.34) * (pow( (0.92 / 0.34) , (((float)selfTest[1] - 1.0) / 30.0))); // FT[Ya] factory trim calculation
   factoryTrim[2] = (4096.0 * 0.34) * (pow( (0.92 / 0.34) , (((float)selfTest[2] - 1.0) / 30.0))); // FT[Za] factory trim calculation
   factoryTrim[3] =  ( 25.0 * 131.0) * (pow( 1.046 , ((float)selfTest[3] - 1.0) ));         // FT[Xg] factory trim calculation
   factoryTrim[4] =  (-25.0 * 131.0) * (pow( 1.046 , ((float)selfTest[4] - 1.0) ));         // FT[Yg] factory trim calculation
   factoryTrim[5] =  ( 25.0 * 131.0) * (pow( 1.046 , ((float)selfTest[5] - 1.0) ));         // FT[Zg] factory trim calculation

   // Report results as a ratio of (STR - FT)/FT; the change from Factory Trim of the Self-Test Response
   // To get to percent, must multiply by 100 and subtract result from 100
   for (int i = 0; i < 6; i++)
   {
      results[i] = 100.0 + 100.0 * ((float)selfTest[i] - factoryTrim[i]) / factoryTrim[i]; // Report percent differences

      if (results[i] > SELFTEST_PASS_THRESHOLD)
      {
         assert(false);
      }

   }
}

void MPU6050::Set_Gyro_Res(MPU6050_Gscale_T gscale)
{
   switch (gscale)
   {
      // Possible gyro scales (and their register bit settings) are:
      // 250 DPS (00), 500 DPS (01), 1000 DPS (10), and 2000 DPS  (11).
      // Here's a bit of an algorithm to calculate DPS/(ADC tick) based on that 2-bit value:
      case GFS_250DPS:
         scalings.gyro = 250.0 / 32768.0 * (M_PI/180);
         break;
      case GFS_500DPS:
         scalings.gyro = 500.0 / 32768.0 * (M_PI/180);
         break;
      case GFS_1000DPS:
         scalings.gyro = 1000.0 / 32768.0 * (M_PI/180);
         break;
      case GFS_2000DPS:
         scalings.gyro = 2000.0 / 32768.0 * (M_PI/180);
         break;
      default:
         assert(0);
         break;
   }
}

void MPU6050::Set_Accel_Res(MPU6050_Ascale_T ascale)
{
   switch (ascale)
   {
      // Possible accelerometer scales (and their register bit settings) are:
      // 2 Gs (00), 4 Gs (01), 8 Gs (10), and 16 Gs  (11).
      // Here's a bit of an algorithm to calculate DPS/(ADC tick) based on that 2-bit value:
      case AFS_2G:
         scalings.accel = 2.0f/32768.0f*G;
         break;
      case AFS_4G:
         scalings.accel = 4.0f/32768.0f*G;
         break;
      case AFS_8G:
         scalings.accel = 8.0f/32768.0f*G;
         break;
      case AFS_16G:
         scalings.accel = 16.0f/32768.0f*G;
         break;
      default:
         assert(0);
         break;
   }
}

void MPU6050::Read_Accel_Data(Accel_Data_T * destination)
{
   uint8_t raw_data[6];  /* x/y/z accel register data stored here */
   int16_t a[3];

   Read_Bytes(MPU6050_ADDRESS, ACCEL_XOUT_H, 6, &raw_data[0]);  /* Read the six raw data registers into data array */

   a[X] = (int16_t)((raw_data[0] << 8) | raw_data[1]) ;  /* Turn the MSB and LSB into a signed 16-bit value */
   a[Y] = (int16_t)((raw_data[2] << 8) | raw_data[3]) ;
   a[Z] = (int16_t)((raw_data[4] << 8) | raw_data[5]) ;

   destination->ax = (a[X] * scalings.accel);
   destination->ay = (a[Y] * scalings.accel);
   destination->az = (a[Z] * scalings.accel);
}

void MPU6050::Read_Gyro_Data(Gyro_Data_T * destination)
{
   uint8_t rawData[6];  // x/y/z gyro register data stored here
   int16_t g[3];

   Read_Bytes(MPU6050_ADDRESS, GYRO_XOUT_H, 6, &rawData[0]);  /* Read the six raw data registers sequentially into data array */

   g[X] = (int16_t)((rawData[0] << 8) | rawData[1]);  /* Turn the MSB and LSB into a signed 16-bit value */
   g[Y] = (int16_t)((rawData[2] << 8) | rawData[3]);
   g[Z] = (int16_t)((rawData[4] << 8) | rawData[5]);

   destination->gx = (g[X] * scalings.gyro);
   destination->gy = (g[Y] * scalings.gyro);
   destination->gz = (g[Z] * scalings.gyro);
}

float MPU6050::Read_Die_Temp(void)
{
   uint8_t rawData[2];  // x/y/z gyro register data stored here
   float ret_val;

   Read_Bytes(MPU6050_ADDRESS, TEMP_OUT_H, 2, &rawData[0]);  // Read the two raw data registers sequentially into data array

   ret_val = (((int16_t)((rawData[0] << 8) | rawData[1]))/TEMP_SENSITIVITY) + TEMP_OFFSET; /* Convert to deg C */

   return(ret_val);
}
