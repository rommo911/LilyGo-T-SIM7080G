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
#include "imu6500/imu_DMP6.hpp"
#include "imu6500/driver_mpu6500_dmp.h"
#include "pins.hpp"
#include "wifi/wifi.hpp"
#include <atomic>
namespace imu6500_dmp
{

  /*---MPU6050 Control/Status Variables---*/
  uint64_t lastMoved_timestamp = 0;
  /*---Orientation/Motion Variables---*/

  float pitch = 0, roll = 0, yaw = 0;

  float euler[3]; // [psi, theta, phi]    Euler angle container
  float ypr[3];   // [yaw, pitch, roll]   Yaw/Pitch/Roll container and gravity vector

  float dax = 0;
  float day = 0;
  float daz = 0;

  float min_dax = 1.f;
  float min_day = 1.f;
  float min_daz = 1.f;

  MotionDtect_t globalMotion = {};
  motion_t globalmotiondata = {};
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
  std::atomic<bool> MPUInterrupt;            // Indicates whether MPU6050 interrupt pin has gone high
  volatile bool MPU_DMP_DATA_READY = false;   // Indicates whether MPU6050 interrupt pin has gone high
  volatile bool MPU_MTION_Interrupt = false; // Indicates whether MPU6050 interrupt pin has gone high

  void IRAM_ATTR DMPDataReady()
  {
    MPUInterrupt = true;
  }

  void imu_Interrupt_loop(void *arg)
  {
    while (1)
    {
      if (MPUInterrupt)
      {
        MPUInterrupt = false;
        mpu6500_dmp_irq_handler();
      }
      delay(25);
    }
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

  void MPU_InterruptCallback(uint8_t type)
  {
    switch (type)
    {
    case MPU6500_INTERRUPT_FIFO_OVERFLOW:
    {
      // mqttLogger.println("mpu6500: irq fifo overflow.");
      break;
    }
    case MPU6500_INTERRUPT_MOTION:
    {
      mqttLogger.println("mpu6500: irq motion.");
      MPU_MTION_Interrupt = true;
      break;
    }
    case MPU6500_INTERRUPT_FSYNC_INT:
    {
      // mqttLogger.println("mpu6500: irq fsync int.");
      break;
    }
    case MPU6500_INTERRUPT_DMP:
    {
      // Serial.println("mpu6500: irq DMP_READY");
      motion_t motion = {};
      motion.l = 5;
      if (mpu6500_dmp_read_motion(&motion) == 0)
      {
        xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(5));
        globalmotiondata = motion;
        MPU_DMP_DATA_READY = true;
        xSemaphoreGive(imuSemaphore);
        //Serial.printf("got data len=%d\n", motion.l);
      }
      break;
    }
    case MPU6500_INTERRUPT_DATA_READY:
    {
      // mqttLogger.println("mpu6500: irq DATA_READY");
      break;
    }
    default:
      break;
    }
  }

  void tapCallback(uint8_t count, uint8_t direction)
  {
    mqttLogger.printf("mpu6500: tap event detected. count: %d direction: %d.\n", count, direction);
  }

  bool imu_setup()
  {
    vSemaphoreCreateBinary(imuSemaphore); // 400kHz I2C clock. Comment on this line if having compilation difficulties
    xSemaphoreGive(imuSemaphore);
    uint8_t counter = 0;
    /*Verify connection*/
    Serial.println(F("starting MPU6050 connection..."));
    if (mpu6500_dmp_init(MPU6500_INTERFACE_IIC,
                         MPU6500_ADDRESS_AD0_LOW,
                         MPU_InterruptCallback,
                         tapCallback) != 0)
    {
      Serial.println("MPU6050 connection failed");
      return false;
    }
    else
    {
      Serial.println("MPU6050 connection successful");
    }
    detachInterrupt(MOTION_INTRRUPT_PIN);
    pinMode(MOTION_INTRRUPT_PIN, INPUT_PULLUP);
    attachInterrupt(MOTION_INTRRUPT_PIN, DMPDataReady, FALLING);
    /* Initializate and configure the DMP*/
    imu_dmp_loop = true;
    xTaskCreate(imu_Interrupt_loop, "IMU", 4096, NULL, 1, NULL);
    // xTaskCreate(imu_loop, "IMU", 4096, NULL, 10, NULL);
    return true;
  }

  bool waitforBaseline()
  {
    if (!baseline_ready)
    {
      float sx = 0, sy = 0, sz = 0;
      uint16_t collected = 0;
      Serial.printf("Baseline calibrate started \n\r");
      for (uint16_t i = 0; i < BASELINE_SAMPLES; ++i)
      {
        motion_t data;
        while (!MPU_DMP_DATA_READY)
        {
          delay(4);
        };
        xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(10));
        MPU_DMP_DATA_READY = false;
        sx += globalmotiondata.accel_g[0][0];
        sy += globalmotiondata.accel_g[0][1];
        sz += globalmotiondata.accel_g[0][2];
        xSemaphoreGive(imuSemaphore);
        collected++;
      }
      if (collected > 0)
      {
        baseline_ax = sx / collected;
        baseline_ay = sy / collected;
        baseline_az = sz / collected;
        baseline_ready = true;
        Serial.println("Motion detected baseline ready ");
      }
      xSemaphoreGive(imuSemaphore);
    }

    return baseline_ready;
  }

  void imu_loop(void *arg)
  {
    Serial.println("Starting IMU DMP loop...");
    while (imu_dmp_loop)
    {
      // Serial.println("mpu loop interrupt ");
      if (!baseline_ready)
      {
        waitforBaseline();
      }
      if (!baseline_ready)
      {
        continue;
      }
      /* Read a packet from FIFO */
      if (MPU_DMP_DATA_READY)
      { // Get the Latest packet
        xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(10));
        MPU_DMP_DATA_READY = false;
        // Convert to g's using default sensitivity (LSB per g)

        // // Compute delta from baseline
        dax = fabs(globalmotiondata.accel_g[0][0] - baseline_ax);
        day = fabs(globalmotiondata.accel_g[0][1] - baseline_ay);
        daz = fabs(globalmotiondata.accel_g[0][2] - baseline_az);

        if (dax > MOTION_THRESHOLD_GX)
        {
          globalMotion.motion = true;
          globalMotion.x = true;
          lastMoved_timestamp = millis();
          Serial.printf("Motion  diff x= %.4f \n", dax);
        }
        if (day > MOTION_THRESHOLD_GY)
        {
          globalMotion.motion = true;
          globalMotion.y = true;
          lastMoved_timestamp = millis();
          Serial.printf("Motion diff y = %.4f \n", day);
        }
        if (daz > MOTION_THRESHOLD_GZ)
        {
          globalMotion.motion = true;
          globalMotion.z = true;
          lastMoved_timestamp = millis();
          Serial.printf("Motion diff z = %.4f \n", daz);
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
    baseline_ready = true;
    vTaskDelay(pdMS_TO_TICKS(100));
    xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(100));
    // mpu.reset();
    // mpu.setSleepEnabled(true);
    xSemaphoreGive(imuSemaphore);
    return true;
  }

  bool setupLowPowerMode()
  {
    Serial.println("Setting up IMU low power mode...");
    imu_dmp_loop = false;
    baseline_ready = true;
    xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(100));
    // mpu.reset();
    // delay(250);
    // mpu.initialize();
    // delay(250);
    // mpu.setIntMotionEnabled(true);
    // mpu.setMotionDetectionCounterDecrement(1); // Count units for motion detection
    // mpu.setMotionDetectionThreshold(1);        // Motion threshold (LSB) for
    // mpu.setWakeCycleEnabled(true);             // Enable wake on motion detection
    // mpu.setWakeFrequency(40);
    xSemaphoreGive(imuSemaphore);
    return true;
  }
}
