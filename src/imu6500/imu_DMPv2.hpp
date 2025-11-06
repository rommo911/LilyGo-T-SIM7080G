
#include "pins.hpp"
#include <Arduino.h>
namespace imu6500_dmpv2
{
  // If MotionDtect_t not declared in header, include here (user provided definition)
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
  
  bool imu_setup();
  MotionDtect_t imu_get_moved();
  uint64_t getLastMovedTimestamp();
}