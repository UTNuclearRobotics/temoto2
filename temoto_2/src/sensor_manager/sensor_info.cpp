#include "sensor_manager/sensor_info.h"
#include "ros/ros.h"
#include "common/tools.h"

namespace sensor_manager
{
SensorInfo::SensorInfo(std::string sensor_name)
{
  setReliability(0.8);

  //set the sensor to current namespace
  msg_.temoto_namespace = ::common::getTemotoNamespace();
  msg_.sensor_name = sensor_name;
}

// Delegate the constructor to initialize reliability filter and
// then override everything else from msg
SensorInfo::SensorInfo(const temoto_2::SensorInfoSync& msg)
{
  setReliability(msg_.reliability);
  msg_ = msg;
}

// Adds a reliability contribution to a moving average filter
void SensorInfo::adjustReliability(float reliability)
{
  reliability = std::max(std::min(reliability, 1.0f), 0.0f);  // clamp to [0-1]
  ++reliability_idx %= reliabilities_.size();                 // rolling index

  // take out the last reliability
  msg_.reliability -= reliabilities_[reliability_idx] / (float)reliabilities_.size();

  // insert new reliability value
  reliabilities_[reliability_idx] = reliability / (float)reliabilities_.size();
  msg_.reliability += reliability / (float)reliabilities_.size();
}

void SensorInfo::setReliability(float reliability)
{
  // Fill array with initial reliability values;
  reliabilities_.fill(reliability);  // buffer for instantaneous reliability values
  msg_.reliability = 0.8;    // Filtered reliability is kept here
  reliability_idx = 0;
}

std::string SensorInfo::toString() const
{
  std::string ret;
  ret += "SENSOR: " + getName() + "\n";
  ret += "  type        : " + getType() + "\n";
  ret += "  package name: " + getPackageName() + "\n";
  ret += "  executable  : " + getExecutable() + "\n";
  ret += "  topic       : " + getTopic() + "\n";
  ret += "  description : " + getDescription() + "\n";
  ret += "  reliability : " + std::to_string(getReliability()) + "\n";
  return ret;
}

//const temoto_2::SensorInfoSync& SensorInfo::getSyncMsg(const std::string& action)
//{
//  std::string prefix = common::generateLogPrefix(log_subsys_, log_class_, __func__);
//  if (action == rmp::sync_action::GET_SENSORS || action == rmp::sync_action::UPDATE)
//  {
//    msg_.sync_action = action;
//  }
//  else
//  {
//    TEMOTO_ERROR("%s Unknown sync_action %s", prefix.c_str(), action.c_str());
//  }
//  return msg_;
//}

}  // SensorManager namespace
