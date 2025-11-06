
#include "pins.hpp"
#include <Arduino.h>
namespace imu6500_dmp
{
  typedef struct MotionDtect
  {
    bool motionInterrupt = false;
    bool x = false;
    bool y = false;
    bool z = false;
    bool yaw = false;
    bool pitch = false;
    bool roll = false;
    uint32_t ts = 0;
    operator bool() { return motionInterrupt || x || y || z || yaw || pitch || roll; }
    void reset()
    {
      motionInterrupt = false;
      x = false;
      y = false;
      z = false;
      roll = false;
      pitch = false;
      yaw = false;
      ts = 0;
    }
  } MotionDtect_t;

   typedef struct baseline_t
  {
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    bool ready = false;
    uint64_t last_reset = 0;
  }baseline_t;
  
  bool LoadImuPreferences();

  bool imu_setup();
  bool imu_WakeOnMotion_LowPwer_setup();
  MotionDtect_t imu_get_moved();
  uint64_t getLastMovedTimestamp();
  void resetBaseline();
  baseline_t getbaseline();
  bool setupLowPowerMode();
  bool shutdown();
  bool SetWakeOnMotionThresh();

}