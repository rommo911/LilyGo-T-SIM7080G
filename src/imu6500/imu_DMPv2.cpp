// imu_DMP6.cpp
// Replacement motion-detection implementation using MPU6500 DMP FIFO
// - Quaternion-based gravity removal
// - Bandpass (biquad) -> STA/LTA -> jerk -> gyro corroboration
// - Updates MotionDtect_t globalMotion only
//
// Notes:
// - Assumes DMP quaternion returned in int32_t quat[4] in Q30 format (scale 2^30).
//   If your driver uses a different scale, change Q30_SCALE below.
// - Call imu_setup(mpu6500_handle_t *h) once, then call imu_process() regularly (e.g., from a FreeRTOS task).
// - Read/adjust thresholds empirically using Serial logs.

#include <Arduino.h>
#include <math.h>
#include "I2Cdev.h"
#include "imu6500/imu_DMPv2.hpp"
#include "imu6500/motionCalc.hpp"
#include "imu6500/driver_mpu6500_dmp.h"
#include "pins.hpp"
#include "wifi/wifi.hpp"
#include <atomic>
#include "power/power.hpp"
#include <Preferences.h>

namespace imu6500_dmpv2
{
  static uint32_t last_event_ts = 0;
  // store last magnitude for jerk calculation
  static float last_mag = 0.0f;
  void imu_loop(void *arg);

  /*------Interrupt detection routine------*/
  std::atomic<bool> IMUInterrupt{false};
  std::atomic<bool> MPU_DMP_DATA_READY{false};
  std::atomic<bool> MPU_MTION_Interrupt{false};
  std::atomic<uint64_t> MPU_MTION_Interrupt_ts{0};
  MotionDtect_t globalMotion = {};
  motion_t globalmotiondata = {};
  motion_t InterruptMotion = {};
  static bool imu_dmp_loop = false;
  SemaphoreHandle_t imuDataSemaphore, wireMutex;
  uint16_t calibrationDebounce = 0;
  uint64_t lastMoved_timestamp = 0;

// ----------------- Configuration / Tunables -----------------
#define SAMPLE_RATE_HZ 80.0f // effective FIFO feed rate (adjust to actual)
#define NYQUIST (SAMPLE_RATE_HZ / 2.0f)

// Bandpass design (for SAMPLE_RATE_HZ = 60 Hz we use 3-20 Hz)
#define BP_LOW_HZ 3.0f
#define BP_HIGH_HZ 20.0f

// STA / LTA windows (in seconds)
#define STA_SEC 0.12f // short-term (~0.12s)
#define LTA_SEC 1.2f  // long-term (~1.2s)

  // Derived sample counts
static const int STA_SAMPLES = (int)max(1, (int)roundf(STA_SEC *SAMPLE_RATE_HZ));
static const int LTA_SAMPLES = (int)max(STA_SAMPLES + 1, (int)roundf(LTA_SEC *SAMPLE_RATE_HZ));

// STA/LTA threshold (ratio)
#define STA_LTA_THRESHOLD 1.5f
#define STA_LTA_THRESHOLD_BORDER 0.5f // borderline when corroborated by gyro

// Jerk threshold (g/s) - approximate: derivative magnitude * sampleRate
#define JERK_THRESHOLD 0.6f
#define JERK_THRESHOLD_BORDER 0.4f

// Gyro corroboration threshold (degrees/sec)
#define GYRO_CORR_DPS 1.0f

// Debounce / deadtime after event (ms)
#define EVENT_DEBOUNCE_MS 500

// Peak classification (g)
#define PEAK_MINOR 0.25f
#define PEAK_MODERATE 0.5f
#define PEAK_SEVERE 0.8f
  float WOM_DET_THRESH = 15.0f;

// Logging
#define IMU_LOG(...) Serial.printf(__VA_ARGS__)

  // ----------------- Globals -----------------
  void IRAM_ATTR IMUDataInterrupt()
  {
    IMUInterrupt = true;
  }

  float angleDiff(float a, float b)
  {
    float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f; // normalize to [-180,180)
    return fabsf(d);
  }

  void imu_Interrupt_loop(void *arg)
  {
    while (imu_dmp_loop)
    {
      if (IMUInterrupt)
      {
        auto res = xSemaphoreTake(wireMutex, pdMS_TO_TICKS(10));
        if (res == pdTRUE)
        {
          IMUInterrupt = false;
          mpu6500_dmp_irq_handler();
          xSemaphoreGive(wireMutex);
        }
      }
      delay(25);
    }
  }

  void MPU_InterruptCallback(uint8_t type)
  {
    switch (type)
    {
    case MPU6500_INTERRUPT_MOTION:
    {
      Serial.println("mpu6500: irq motion.");
      MPU_MTION_Interrupt = true;
      MPU_MTION_Interrupt_ts = millis();
      globalMotion.motionInterrupt = true;
      lastMoved_timestamp = millis();
      break;
    }
    case MPU6500_INTERRUPT_DMP:
    {
      // Serial.println("mpu6500: irq DMP_READY");
      InterruptMotion.l = 5;
      if (mpu6500_dmp_read_motion(&InterruptMotion) == 0)
      {
        xSemaphoreTake(imuDataSemaphore, pdMS_TO_TICKS(10));
        globalmotiondata = InterruptMotion;
        xSemaphoreGive(imuDataSemaphore);
        MPU_DMP_DATA_READY = true;
        // Serial.printf("got data len=%d\n", motion.l);
      }
      break;
    }
    default:
      break;
    }
  }

  bool imu_setup()
  {
    if (imuDataSemaphore == NULL)
    {
      vSemaphoreCreateBinary(wireMutex);
      xSemaphoreGive(wireMutex);
      vSemaphoreCreateBinary(imuDataSemaphore);
      xSemaphoreGive(imuDataSemaphore);
    }

    uint8_t counter = 0;
    /*Verify connection*/
    Serial.println(F("starting MPU6050 connection..."));
    if (mpu6500_dmp_init(MPU6500_INTERFACE_IIC,
                         MPU6500_ADDRESS_AD0_LOW,
                         MPU_InterruptCallback, WOM_DET_THRESH, false) != 0)
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
                           MPU_InterruptCallback, WOM_DET_THRESH, false) != 0)
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
    attachInterrupt(MOTION_INTRRUPT_PIN, IMUDataInterrupt, FALLING);
    imu_dmp_loop = true;
    xTaskCreate(imu_Interrupt_loop, "IMU", 8192, NULL, 1, NULL);
    xTaskCreate(imu_loop, "IMUloop", 8192, NULL, 1, NULL);
    return true;
  }

  // set globalMotion flag helper
  static void set_motion_flag(MotionDtect_t &m, uint32_t now_ms, bool isAccelEvent, float peak_g, float gyro_mag)
  {
    // simplistic mapping:
    m.motionInterrupt = true;
    // treat axis flags as generic translational flags (x,y,z) if accel dominant
    if (isAccelEvent)
    {
      // classify severity by peak_g
      if (peak_g >= PEAK_SEVERE)
      {
        m.x = m.y = m.z = true; // severe -> mark all
      }
      else if (peak_g >= PEAK_MODERATE)
      {
        m.x = true; // moderate -> mark some axis (user doesn't need per-axis accuracy)
      }
      else
      {
        m.y = true; // minor -> different flag to show minor
      }
    }
    // rotation flags if gyro corroboration significant
    if (gyro_mag > GYRO_CORR_DPS)
    {
      m.yaw = true;
      m.pitch = true;
      m.roll = true;
    }
    m.ts = now_ms;
  }

  // ----------------- Public API -----------------

  // Main processing function: read DMP FIFO and run detection.
  // Call as often as possible (e.g., main loop or RTOS task).
  void imu_loop(void *arg)
  {
    Serial.println("Starting IMU DMP loop...");
    delay(5000);
    while (imu_dmp_loop)
    {
      /* Read a packet from FIFO */
      if (MPU_DMP_DATA_READY)
      { // Get the Latest packet
        MPU_DMP_DATA_READY = false;
        xSemaphoreTake(imuDataSemaphore, pdMS_TO_TICKS(10));

        uint32_t now = millis();

        // Process each frame returned
        for (int f = 0; f < globalmotiondata.l; ++f)
        {
          // convert quat
          float qf[4];
          quat_q30_to_float(globalmotiondata.quat[f], qf);

          // accel sensor in g (driver already gives accel_g). Use those values.
          float a_sensor[3] = {globalmotiondata.accel_g[f][0], globalmotiondata.accel_g[f][1], globalmotiondata.accel_g[f][2]};

          // rotate to world
          float a_world[3];
          rotate_accel_world(qf, a_sensor, a_world);

          // remove gravity (assume world-frame Z points up and gravity = +1g)
          float a_lin[3] = {a_world[0], a_world[1], a_world[2] - 1.0f};

          // magnitude of linear accel (g)
          float mag = sqrtf((a_lin[0] * a_lin[0] )+ (a_lin[1] * a_lin[1] )+ (a_lin[2] * a_lin[2]));

          // bandpass the magnitude (we process magnitude to be orientation invariant)
          float bp_out = biquad_process(bp_filter, mag);

          // push absolute bandpassed magnitude to energy buffer
          energy_push(fabsf(bp_out));

          // compute STA and LTA (RMS)
          float sta = energy_rms_last(STA_SAMPLES);
          float lta = energy_rms_last(LTA_SAMPLES);
          if (lta < 1e-6f)
            lta = 1e-6f; // avoid divide by zero

          float ratio = sta / lta;

          // jerk approx: (mag - last_mag) * sampleRate (g/s)
          float jerk = fabsf(mag - last_mag) * SAMPLE_RATE_HZ;
          last_mag = mag;
          float peak_g = mag;

          // gyro magnitude
          float gx = globalmotiondata.gyro_dps[f][0], gy = globalmotiondata.gyro_dps[f][1], gz = globalmotiondata.gyro_dps[f][2];
          float gyro_mag = sqrtf(gx * gx + gy * gy + gz * gz);

          bool impact = false;
          bool isAccelEvent = false;

          // Primary rule: STA/LTA + jerk
          if (ratio >= STA_LTA_THRESHOLD && jerk >= JERK_THRESHOLD)
          {
            impact = true;
            isAccelEvent = true;
          }
          else if (ratio >= STA_LTA_THRESHOLD_BORDER && jerk >= JERK_THRESHOLD_BORDER && gyro_mag >= GYRO_CORR_DPS)
          {
            // borderline ratio but gyro corroborates
            impact = true;
            isAccelEvent = true;
          }
          else
          {
            // fallback: large instantaneous peak (very sudden)
            if (mag >= PEAK_MODERATE && jerk > (JERK_THRESHOLD * 0.5f))
            {
              impact = true;
              isAccelEvent = true;
            }
          }

          // Debounce: ignore if within deadtime from last_event_ts
          if (impact && (now - last_event_ts) < EVENT_DEBOUNCE_MS)
          {
            impact = false;
          }

          if (impact)
          {
            // update globalMotion
            globalMotion.reset();
            set_motion_flag(globalMotion, now, isAccelEvent, peak_g, gyro_mag);
            last_event_ts = now;
            // debug log
            IMU_LOG("IMU Impact @%u ms: STA/LTA=%.2f STA=%.4f LTA=%.4f peak_g=%.3f jerk=%.3f gyro=%.1f\n",now, ratio, sta, lta, peak_g, jerk, gyro_mag);
            // we continue processing frames but we have already registered this event
          }
          xSemaphoreGive(imuDataSemaphore);
        }
      }
      delay(10);
    } // frames
  }

  // Return a copy of globalMotion (thread-safe enough for simple use). Caller expected to handle reset if desired.
  uint64_t getLastMovedTimestamp()
  {
    xSemaphoreTake(imuDataSemaphore, pdMS_TO_TICKS(10));
    uint64_t ts = globalMotion.ts;
    xSemaphoreGive(imuDataSemaphore);
    return ts;
  }
  // Exposed function to report motion. Returns true once if motion detected since last call.
  MotionDtect_t imu_get_moved()
  { // Atomically consume the flag
    xSemaphoreTake(imuDataSemaphore, pdMS_TO_TICKS(10));
    MotionDtect_t str = globalMotion;
    globalMotion.reset();
    xSemaphoreGive(imuDataSemaphore);
    return str;
  }
}