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
#include "power/power.hpp"

namespace imu6500_dmp
{

  /*---MPU6050 Control/Status Variables---*/
  uint64_t lastMoved_timestamp = 0;
  /*---Orientation/Motion Variables---*/

  MotionDtect_t globalMotion = {};
  motion_t globalmotiondata = {};
  motion_t InterruptMotion = {};

  float MOTION_THRESHOLD_GX = 0.0045f; // sensitivity: ~0.03 g (~0.3 m/s^2)
  float MOTION_THRESHOLD_GY = 0.0045f; // sensitivity: ~0.03 g (~0.3 m/s^2)
  float MOTION_THRESHOLD_GZ = 0.0045f; // sensitivity: ~0.03 g (~0.3 m/s^2)

  float MOTION_THRESHOLD_ROLL = 0.3f;  // sensitivity: ~0.03 g (~0.3 m/s^2)
  float MOTION_THRESHOLD_YAW = 0.3f;   // sensitivity: ~0.03 g (~0.3 m/s^2)
  float MOTION_THRESHOLD_PITCH = 0.3f; // sensitivity: ~0.03 g (~0.3 m/s^2)

  // Motion detection state
  // Baseline linear acceleration (gravity removed) in g's
  float baseline_ax = 0.0f;
  float baseline_ay = 0.0f;
  float baseline_az = 0.0f;
  float baseline_yaw = 0.0f;
  float baseline_pitch = 0.0f;
  float baseline_roll = 0.0f;
  float dax = 0.0f, day = 0.0f, daz = 0.0f, droll = 0.0f, dyaw = 0.0f, dpitch = 0.0f;
  uint16_t motionAfterBaselineCounter = 0;
  bool baseline_ready = false;
  uint64_t last_baseline_reset = 0;

  const uint16_t BASELINE_SAMPLES = 100;

  static bool imu_dmp_loop = false;
  SemaphoreHandle_t imuSemaphore;
  uint16_t calibrationDebounce = 0;

  void imu_loop(void *arg);

  /*------Interrupt detection routine------*/
  std::atomic<bool> MPUInterrupt;               // Indicates whether MPU6050 interrupt pin has gone high
  volatile bool MPU_DMP_DATA_READY = false;     // Indicates whether MPU6050 interrupt pin has gone high
  volatile uint8_t MPU_DMP_DATA_LEN = 0;        // Indicates whether MPU6050 interrupt pin has gone high
  volatile bool MPU_MTION_Interrupt = false;    // Indicates whether MPU6050 interrupt pin has gone high
  volatile uint64_t MPU_MTION_Interrupt_ts = 0; // Indicates whether MPU6050 interrupt pin has gone high

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
      delay(15);
    }
  }

  uint64_t get_last_baseline_reset()
  {
    return last_baseline_reset;
  }

  void resetBaseline()
  {
    baseline_ready = false;
    motionAfterBaselineCounter = 0;
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
      MPU_MTION_Interrupt_ts = millis();
      globalMotion.motion = true;
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
      InterruptMotion.l = 10;
      if (mpu6500_dmp_read_motion(&InterruptMotion) == 0)
      {
        xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(10));
        globalmotiondata = InterruptMotion;
        MPU_DMP_DATA_READY = true;
        xSemaphoreGive(imuSemaphore);
        // Serial.printf("got data len=%d\n", motion.l);
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

  bool imu_setup()
  {
    vSemaphoreCreateBinary(imuSemaphore); // 400kHz I2C clock. Comment on this line if having compilation difficulties
    xSemaphoreGive(imuSemaphore);
    uint8_t counter = 0;
    /*Verify connection*/
    Serial.println(F("starting MPU6050 connection..."));
    if (mpu6500_dmp_init(MPU6500_INTERFACE_IIC,
                         MPU6500_ADDRESS_AD0_LOW,
                         MPU_InterruptCallback) != 0)
    {
      Serial.println("MPU6050 connection failed");
      // External row needle, 1400~3700mV // external supply from pmu to header
      auto &PMU = power::getPMU();
      PMU.disableDC5();
      delay(1000);
      PMU.setDC5Voltage(3400);
      PMU.enableDC5();
      if (mpu6500_dmp_init(MPU6500_INTERFACE_IIC,
                           MPU6500_ADDRESS_AD0_LOW,
                           MPU_InterruptCallback) != 0)
      {
        Serial.println("MPU6050 connection failed again ");
        return false;
      }
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
    xTaskCreate(imu_loop, "IMU", 4096, NULL, 10, NULL);
    return true;
  }

  bool waitforBaseline()
  {
    if (!baseline_ready)
    {
      calibrationDebounce = 0;
      MPU_DMP_DATA_READY = false;
      MPU_MTION_Interrupt = false;
      float sx = 0.f, sy = 0.f, sz = 0.f, sroll = 0.0f, syaw = 0.0f, spitch = 0.0f;
      float syaw_sin = 0.0f, syaw_cos = 0.0f;
      float sroll_sin = 0.0f, sroll_cos = 0.0f;
      float spitch_sin = 0.0f, spitch_cos = 0.0f;
      uint16_t collected = 0;
      mpu6500_dmp_resetFIFO();
      Serial.printf("Baseline calibrate started \n\r");
      for (uint16_t i = 0; i < BASELINE_SAMPLES; ++i)
      {
        delay(20);
        if (MPU_MTION_Interrupt)
        {
          Serial.printf("Baseline calibrate FAILED \n\r");
          return false;
        }
        motion_t data;
        while (!MPU_DMP_DATA_READY)
        {
          delay(10);
        };
        MPU_DMP_DATA_READY = false;
        xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(15));
        auto motionData = globalmotiondata;
        xSemaphoreGive(imuSemaphore);
        for (int i = 0; i < motionData.l; i++)
        {
          sx += motionData.accel_g[i][0];
          sy += motionData.accel_g[i][1];
          sz += motionData.accel_g[i][2];

          float yaw_rad = radians(motionData.yaw[i]);
          float roll_rad = radians(motionData.roll[i]);
          float pitch_rad = radians(motionData.pitch[i]);

          syaw_sin += sin(yaw_rad);
          syaw_cos += cos(yaw_rad);
          sroll_sin += sin(roll_rad);
          sroll_cos += cos(roll_rad);
          spitch_sin += sin(pitch_rad);
          spitch_cos += cos(pitch_rad);
          collected++;
        }
      }
      if (collected > 0)
      {
        baseline_ax = sx / collected;
        baseline_ay = sy / collected;
        baseline_az = sz / collected;
        baseline_yaw = degrees(atan2(syaw_sin / collected, syaw_cos / collected));
        baseline_pitch = degrees(atan2(spitch_sin / collected, spitch_cos / collected));
        baseline_roll = degrees(atan2(sroll_sin / collected, sroll_cos / collected));

        if (baseline_yaw < 0)
          baseline_yaw += 360.0f;
        if (baseline_pitch < 0)
          baseline_pitch += 360.0f;
        if (baseline_roll < 0)
          baseline_roll += 360.0f;
        baseline_ready = true;
        Serial.println("Motion detected baseline ready ");
        Serial.printf(" ax %.5f, ay %.5f, az %.5f, yaw %.5f, pitch %.5f, roll %.5f \n", baseline_ax, baseline_ay, baseline_az, baseline_yaw, baseline_pitch, baseline_roll);
        last_baseline_reset = millis();
      }
    }

    return baseline_ready;
  }
  
  float angleDiff(float a, float b)
  {
    float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f; // normalize to [-180,180)
    return fabsf(d);
  }

  void imu_loop(void *arg)
  {
    MPU_MTION_Interrupt_ts = millis();
    last_baseline_reset = millis();
    Serial.println("Starting IMU DMP loop...");
    delay(5000);
    while (imu_dmp_loop)
    {
      if (((millis()) > (MPU_MTION_Interrupt_ts + 15000)) && ((millis()) > (last_baseline_reset + 15000)) && (motionAfterBaselineCounter > 20))
      {
        Serial.println("Periodic baseline calibration");
        resetBaseline();
      }
      if (!waitforBaseline())
      {
        delay(500);
        continue;
      }
      /* Read a packet from FIFO */
      if (MPU_DMP_DATA_READY)
      { // Get the Latest packet
        MPU_DMP_DATA_READY = false;
        // // Compute delta from baseline
        uint16_t collected = 0;
        xSemaphoreTake(imuSemaphore, pdMS_TO_TICKS(15));
        float sroll = 0.f, syaw = 0.f, spitch = 0.f;
        for (uint8_t i = 0; i < globalmotiondata.l; i++)
        {
          dax = fabs(globalmotiondata.accel_g[i][0] - baseline_ax);
          day = fabs(globalmotiondata.accel_g[i][1] - baseline_ay);
          daz = fabs(globalmotiondata.accel_g[i][2] - baseline_az);

          sroll += angleDiff(globalmotiondata.roll[i], baseline_roll);
          syaw += angleDiff(globalmotiondata.yaw[i], baseline_yaw);
          spitch += angleDiff(globalmotiondata.pitch[i], baseline_pitch);

          collected++;
        }
        dax /= collected;
        day /= collected;
        daz /= collected;
        droll = sroll / collected;
        dyaw = syaw / collected;
        dpitch = spitch / collected;
        if (dax > MOTION_THRESHOLD_GX)
        {
          if (dax > 0.1f && (calibrationDebounce++ > 10))
          {
            Serial.println("dax baseline calibration");
            resetBaseline();
          }
          globalMotion.x = true;
          lastMoved_timestamp = millis();
          Serial.printf("Motion  diff x= %.4f \n", dax);
        }
        if (day > MOTION_THRESHOLD_GY)
        {
          if (day > 0.1f && (calibrationDebounce++ > 10))
          {
            Serial.println("day baseline calibration");
            resetBaseline();
          }
          globalMotion.y = true;
          lastMoved_timestamp = millis();
          Serial.printf("Motion diff y = %.4f \n", day);
        }
        if (daz > MOTION_THRESHOLD_GZ)
        {
          if (daz > 0.1f && (calibrationDebounce++ > 10))
          {
            Serial.println("daz baseline calibration");
            resetBaseline();
          }
          globalMotion.z = true;
          lastMoved_timestamp = millis();
          Serial.printf("Motion diff z = %.4f \n", daz);
        }
        if (droll > MOTION_THRESHOLD_ROLL)
        {
          if (droll > 1 && (calibrationDebounce++ > 10))
          {
            Serial.println("droll baseline calibration");
            resetBaseline();
          }
          globalMotion.roll = true;
          lastMoved_timestamp = millis();
          Serial.printf("Motion droll = %.4f \n", droll);
        }
        if (dyaw > MOTION_THRESHOLD_YAW)
        {
          if (dyaw > 1 && (calibrationDebounce++ > 10))
          {
            Serial.println("dyaw baseline calibration");
            resetBaseline();
          }
          globalMotion.yaw = true;
          lastMoved_timestamp = millis();
          Serial.printf("Motion dyaw = %.4f \n", dyaw);
        }
        if (dpitch > MOTION_THRESHOLD_PITCH)
        {
          if (dpitch > 1 && (calibrationDebounce++ > 10))
          {
            Serial.println("dpitch baseline calibration");
            resetBaseline();
          }
          globalMotion.pitch = true;
          lastMoved_timestamp = millis();
          Serial.printf("Motion dpitch = %.4f \n", dpitch);
        }
        if (globalMotion)
        {
          motionAfterBaselineCounter++;
        }
        /*Serial.printf(" ax=%.4f, ay=%.4f, az=%.4f, yaw=%.4f, pit=%.4f, rol=%.4f ",
                      globalmotiondata.accel_g[0][0], globalmotiondata.accel_g[0][1], globalmotiondata.accel_g[0][2],
                      globalmotiondata.roll[0], globalmotiondata.pitch[0], globalmotiondata.roll[0]);*/
        xSemaphoreGive(imuSemaphore);
        /*Serial.printf(" dax=%.4f, day=%.4f, daz=%.4f, dyaw=%.4f, dptch=%.4f, drol=%.4f \n",
                      dax, day, daz, droll, dpitch, droll);*/
      }
      delay(30);
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
