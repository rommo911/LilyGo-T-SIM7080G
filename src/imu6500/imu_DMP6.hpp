
#include "pins.hpp"
#include <Arduino.h>
namespace imu6500_dmp
{
  typedef struct MotionDtect
  {
    bool motion = false;
    bool x = false;
    bool y = false;
    bool z = false;
    bool yaw = false;
    bool pitch = false;
    bool roll = false;
    uint32_t ts = 0;
    operator bool() { return motion || x || y || z || yaw || pitch || roll; }
    void reset()
    {
      motion = false;
      x = false;
      y = false;
      z = false;
      roll = false;
      pitch = false;
      yaw = false;
      ts = 0;
    }
  } MotionDtect_t;

  bool imu_setup();
  bool imu_WakeOnMotion_LowPwer_setup();
  MotionDtect_t imu_get_moved();
  uint64_t getLastMovedTimestamp();
  void resetBaseline();
  uint64_t get_last_baseline_reset();
  bool setupLowPowerMode();
  bool shutdown();
  bool SetWakeOnMotionThresh(uint8_t motion_thresh_mg);

}