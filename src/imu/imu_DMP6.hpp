
#include "pins.hpp"
#include <Arduino.h>
namespace imu6050_dmp
{
  typedef struct MotionDtect
  {
    bool motion = false;
    bool x = false;
    bool y = false;
    bool z = false;
    operator bool() { return motion || x || y || z; }
    void reset()
    {
      motion = false;
      x = false;
      y = false;
      z = false;
    }
  } MotionDtect_t;

  bool imu_setup();
  MotionDtect_t getMotion();
  uint64_t getLastMovedTimestamp();
  void resetBaseline();
  uint64_t get_last_baseline_reset();
  bool setupLowPowerMode();
  bool shutdown();

}