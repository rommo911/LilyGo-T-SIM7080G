/*
  MPU6050 DMP6

  Digital Motion Processor or DMP performs complex motion processing tasks.
  - Fuses the data from the accel, gyro, and external magnetometer if applied,
  compensating individual sensor noise and errors.
  - Detect specific types of motion without the need to continuously monitor
  raw sensor data with a microcontroller.
  - Reduce workload on the microprocessor.
  - Output processed data such as quaternions, Euler angles, and gravity vectors.

  The code includes an auto-calibration and offsets generator tasks. Different
  output formats available.

  This code is compatible with the teapot project by using the teapot output format.

  Circuit: In addition to connection 3.3v, GND, SDA, and SCL, this sketch
  depends on the MPU6050's INT pin being connected to the Arduino's
  external interrupt #0 pin.

  The teapot processing example may be broken due FIFO structure change if using DMP
  6.12 firmware version.

  Find the full MPU6050 library documentation here:
  https://github.com/ElectronicCats/mpu6050/wiki

*/

#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "imu_DMP6.hpp"
#include "pins.hpp"
#include "wifi/wifi.hpp"
namespace imu_dmp
{

  // #include "MPU6050_6Axis_MotionApps612.h" // Uncomment this library to work with DMP 6.12 and comment on the above library.

  /* MPU6050 default I2C address is 0x68*/
  MPU6050 mpu;
  // MPU6050 mpu(0x69); //Use for AD0 high
  // MPU6050 mpu(0x68, &Wire1); //Use for AD0 low, but 2nd Wire (TWI/I2C) object.

  /*---MPU6050 Control/Status Variables---*/
  bool DMPReady = false;  // Set true if DMP init was successful
  uint8_t devStatus;      // Return status after each device operation (0 = success, !0 = error)
  uint16_t packetSize;    // Expected DMP packet size (default is 42 bytes)
  uint8_t FIFOBuffer[64]; // FIFO storage buffer
  uint64_t lastMoved_timestamp = 0;
  /*---Orientation/Motion Variables---*/
  Quaternion q;        // [w, x, y, z]         Quaternion container
  VectorInt16 aa;      // [x, y, z]            Accel sensor measurements
  VectorInt16 gy;      // [x, y, z]            Gyro sensor measurements
  VectorInt16 aaReal;  // [x, y, z]            Gravity-free accel sensor measurements
  VectorInt16 aaWorld; // [x, y, z]            World-frame accel sensor measurements
  VectorFloat gravity; // [x, y, z]            Gravity vector
  float euler[3];      // [psi, theta, phi]    Euler angle container
  float ypr[3];        // [yaw, pitch, roll]   Yaw/Pitch/Roll container and gravity vector

  float dax = 0;
  float day = 0;
  float daz = 0;

  float min_dax = 1.f;
  float min_day = 1.f;
  float min_daz = 1.f;

  float max_dax = 0;
  float max_day = 0;
  float max_daz = 0;

  MotionDtect_t globalMotion = {};
  // Motion detection state
  // Baseline linear acceleration (gravity removed) in g's
  float baseline_ax = 0.0f;
  float baseline_ay = 0.0f;
  float baseline_az = 0.0f;

  bool baseline_ready = false;
  uint64_t last_baseline_reset = 0;

  float MOTION_THRESHOLD_GX = 0.0022f; // sensitivity: ~0.03 g (~0.3 m/s^2)
  float MOTION_THRESHOLD_GY = 0.0028f; // sensitivity: ~0.03 g (~0.3 m/s^2)
  float MOTION_THRESHOLD_GZ = 0.0500f; // sensitivity: ~0.03 g (~0.3 m/s^2)
  const uint16_t BASELINE_SAMPLES = 150;
  static bool imu_dmp_loop = false;
  SemaphoreHandle_t imuSemaphore;
  void imu_loop(void *arg);

  /*------Interrupt detection routine------*/
  volatile bool MPUInterrupt = false; // Indicates whether MPU6050 interrupt pin has gone high

  void IRAM_ATTR DMPDataReady()
  {
    MPUInterrupt = true;
  }

  bool imu_setup()
  {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000); // Start I2C bus
    vSemaphoreCreateBinary(imuSemaphore);         // 400kHz I2C clock. Comment on this line if having compilation difficulties
    xSemaphoreGive(imuSemaphore);
    uint8_t counter = 0;
    /*Initialize device*/
    mpu.reset();
    delay(250);
    mpu.initialize();
    delay(150);

    /*Verify connection*/
    Serial.println(F("Testing MPU6050 connection..."));
    if (mpu.testConnection() == false)
    {
      mpu.reset();
      delay(250);
      mpu.initialize();
      delay(150);
      mpu.setWakeCycleEnabled(false); // Enable wake on motion detection
      if (mpu.testConnection() == false)
      {
        mqttLogger.println("MPU6050 connection failed");
        return false;
      }
    }
    else
    {
      Serial.println("MPU6050 connection successful");
    }

    /* Initializate and configure the DMP*/
    mqttLogger.println("Initializing DMP...");
    devStatus = mpu.dmpInitialize();

    /* Supply your gyro offsets here, scaled for min sensitivity */
    mpu.setXGyroOffset(0);
    mpu.setYGyroOffset(0);
    mpu.setZGyroOffset(0);
    mpu.setXAccelOffset(0);
    mpu.setYAccelOffset(0);
    mpu.setZAccelOffset(0);
    // set interrupt to active low
    mpu.setInterruptMode(1);

    /* Making sure it worked (returns 0 if so) */
    if (devStatus == 0)
    {
      mpu.CalibrateAccel(15); // Calibration Time: generate offsets and calibrate our MPU6050
      mpu.CalibrateGyro(15);
      Serial.println("These are the Active offsets: ");
      mqttLogger.println("Enabling DMP..."); // Turning ON DMP
      mpu.setDMPEnabled(true);
      /*Enable Arduino interrupt detection*/
      Serial.print(F("Enabling interrupt detection (Arduino external interrupt "));
      Serial.print(digitalPinToInterrupt(MOTION_INTRRUPT_PIN));
      Serial.println(F(")..."));
      detachInterrupt(MOTION_INTRRUPT_PIN);
      pinMode(MOTION_INTRRUPT_PIN, INPUT_PULLUP);
      attachInterrupt(MOTION_INTRRUPT_PIN, DMPDataReady, FALLING);

      /* Set the DMP Ready flag so the main loop() function knows it is okay to use it */
      mqttLogger.println("DMP ready! Waiting for first interrupt...");
      DMPReady = true;
      packetSize = mpu.dmpGetFIFOPacketSize(); // Get expected DMP packet size for later comparison
    }
    else
    {
      mqttLogger.println("DMP Initialization failed (code "); // Print the error code
      Serial.print(devStatus);
      Serial.println();
      // 1 = initial memory load failed
      // 2 = DMP configuration updates failed
      return false;
    }
    xTaskCreate(imu_loop, "IMU", 4096, NULL, 1, NULL);
    return true;
  }

  uint64_t get_last_baseline_reset()
  {
    return last_baseline_reset;
  }
  void resetBaseline()
  {
    last_baseline_reset = millis();
    baseline_ready = false;
  }

  bool waitforBaseline()
  {
    if (!baseline_ready)
    {
      xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(10));

      float sx = 0, sy = 0, sz = 0;
      float sx_r = 0, sy_r = 0, sz_r = 0;

      uint16_t collected = 0;
      Serial.printf("Baseline calibrate started \n\r");
      for (uint16_t i = 0; i < BASELINE_SAMPLES; ++i)
      {
        MPUInterrupt = false;
        if (mpu.dmpGetCurrentFIFOPacket(FIFOBuffer))
        {
          mpu.dmpGetQuaternion(&q, FIFOBuffer);
          mpu.dmpGetAccel(&aa, FIFOBuffer);
          mpu.dmpGetGravity(&gravity, &q);
          mpu.dmpGetLinearAccel(&aaReal, &aa, &gravity);
          // aaReal is in device counts; convert to g's. MPU6050 default accel sensitivity is 16384 LSB/g for +/-2g
          const float LSB_PER_G = 16384.0f;
          sx += (float)aaReal.x / LSB_PER_G;
          sy += (float)aaReal.y / LSB_PER_G;
          sz += (float)aaReal.z / LSB_PER_G;

          collected++;
        }
        do
        {
          delay(10);
        } while (!MPUInterrupt);
      }
      if (collected > 0)
      {
        baseline_ax = sx / collected;
        baseline_ay = sy / collected;
        baseline_az = sz / collected;

        baseline_ready = true;
        Serial.println("Motion detected ready ");
      }
      xSemaphoreGive(imuSemaphore);
    }

    return baseline_ready;
  }

  void imu_loop(void *arg)
  {
    while (imu_dmp_loop)
    {
      if (!DMPReady || !MPUInterrupt)
      {
        delay(1);
        continue;
      }
      if (!baseline_ready)
      {
        waitforBaseline();
      }
      if (!baseline_ready)
      {
        continue;
      }
      MPUInterrupt = false;
      // If baseline not ready, collect a few samples to establish quiet baseline

      /* Read a packet from FIFO */
      if (mpu.dmpGetCurrentFIFOPacket(FIFOBuffer))
      { // Get the Latest packet
        /* Display Euler angles in degrees */
        // Serial.print(F("DMP Got data ")); // Print the error code
        mpu.dmpGetQuaternion(&q, FIFOBuffer);
        mpu.dmpGetGravity(&gravity, &q);
        mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
        mpu.dmpGetEuler(euler, &q);
        mpu.dmpGetAccel(&aa, FIFOBuffer);
        mpu.dmpGetLinearAccel(&aaReal, &aa, &gravity);
        // mpu.dmpGetLinearAccelInWorld(&aaWorld, &aaReal, &q);

        // Convert to g's using default sensitivity (LSB per g)
        const float LSB_PER_G = 16384.0f;
        float ax_g = (float)aaReal.x / LSB_PER_G;
        float ay_g = (float)aaReal.y / LSB_PER_G;
        float az_g = (float)aaReal.z / LSB_PER_G;

        // Compute delta from baseline
        dax = fabs(ax_g - baseline_ax);
        day = fabs(ay_g - baseline_ay);
        daz = fabs(az_g - baseline_az);
        if (dax > max_dax)
          max_dax = dax;
        if (dax < min_dax && dax > 0.f)
          min_dax = dax;

        if (day > max_day)
          max_day = day;
        if (day < min_day && day > 0.f)
          min_day = day;

        if (daz > max_daz)
          max_daz = daz;
        if (daz < min_daz && daz > 0.f)
          min_daz = daz;

        xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(10));

        if (dax > MOTION_THRESHOLD_GX)
        {
          globalMotion.motion = true;
          globalMotion.x = true;
          lastMoved_timestamp = millis();
          // Serial.printf("Motion  diff x= %.4f , max=%.4f , min=%.4f \n", dax, max_dax, min_dax);
        }
        if (day > MOTION_THRESHOLD_GY)
        {
          globalMotion.motion = true;
          globalMotion.y = true;
          lastMoved_timestamp = millis();
          // Serial.printf("Motion diff y = %.4f , max=%.4f , min=%.4f \n", day, max_day, min_day);
        }
        if (daz > MOTION_THRESHOLD_GZ)
        {
          globalMotion.motion = true;
          globalMotion.z = true;
          lastMoved_timestamp = millis();
          // Serial.printf("Motion diff z = %.4f , max=%.4f , min=%.4f \n", daz, max_daz, min_daz);
        }
        xSemaphoreGive(imuSemaphore);
      }
    }
    vTaskDelete(NULL);
  }

  uint64_t getLastMovedTimestamp()
  {
    xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(10));
    uint64_t ts = lastMoved_timestamp;
    xSemaphoreGive(imuSemaphore);
    return ts;
  }
  // Exposed function to report motion. Returns true once if motion detected since last call.

  MotionDtect_t imu_get_moved()
  { // Atomically consume the flag
    xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(10));
    MotionDtect_t str = globalMotion;
    globalMotion.reset();
    xSemaphoreGive(imuSemaphore);
    return str;
  }

  bool shutdown()
  {
    Serial.println("Setting up sleep mode...");
    imu_dmp_loop = false;
    MPUInterrupt = true;
    baseline_ready = true;
    vTaskDelay(pdMS_TO_TICKS(100));
    xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(100));
    mpu.reset();
    mpu.setSleepEnabled(true);
    xSemaphoreGive(imuSemaphore);
    return true;
  }

  bool setupLowPowerMode()
  {
    Serial.println("Setting up IMU low power mode...");
    imu_dmp_loop = false;
    MPUInterrupt = true;
    baseline_ready = true;
    xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(100));
    mpu.reset();
    delay(250);
    mpu.initialize();
    delay(250);
    mpu.setIntMotionEnabled(true);
    mpu.setMotionDetectionCounterDecrement(1); // Count units for motion detection
    mpu.setMotionDetectionThreshold(1);        // Motion threshold (LSB) for
    mpu.setWakeCycleEnabled(true);             // Enable wake on motion detection
    mpu.setWakeFrequency(40);
    xSemaphoreGive(imuSemaphore);
    return true;
  }
}
