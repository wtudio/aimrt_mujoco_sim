// Copyright (c) 2023, AgiBot Inc.
// All rights reserved.
#include "aimrt_module_ros2_interface/channel/ros2_channel.h"
#include "mujoco_sim_module/global.h"
#include "mujoco_sim_module/publisher/imu_sensor_ros2_publisher.h"
#include "mujoco_sim_module/publisher/utils.h"
namespace YAML {
template <>
struct convert<aimrt_mujoco_sim::mujoco_sim_module::publisher::ImuSensorRos2Publisher::Options> {
  using Options = aimrt_mujoco_sim::mujoco_sim_module::publisher::ImuSensorRos2Publisher::Options;
  static Node encode(const Options& rhs) {
    Node node;
    node["bind_site"] = rhs.bind_site;
    node["bind_framequat"] = rhs.bind_framequat;
    node["bind_gyro"] = rhs.bind_gyro;
    node["bind_accelerometer"] = rhs.bind_accelerometer;
    return node;
  }
  static bool decode(const Node& node, Options& rhs) {
    if (!node.IsMap()) return false;
    rhs.bind_site = node["bind_site"].as<std::string>();
    if (node["bind_framequat"]) {
      rhs.bind_framequat = node["bind_framequat"].as<std::string>();
    }
    if (node["bind_gyro"]) {
      rhs.bind_gyro = node["bind_gyro"].as<std::string>();
    }
    if (node["bind_accelerometer"]) {
      rhs.bind_accelerometer = node["bind_accelerometer"].as<std::string>();
    }
    return true;
  }
};
}  // namespace YAML
namespace aimrt_mujoco_sim::mujoco_sim_module::publisher {
void ImuSensorRos2Publisher::Initialize(YAML::Node options_node) {
  if (options_node && !options_node.IsNull())
    options_ = options_node.as<Options>();
  avg_interval_base_ = GetAvgIntervalBase(channel_frq_);
  RegisterSensorAddr();
  options_node = options_;
  bool ret = aimrt::channel::RegisterPublishType<sensor_msgs::msg::Imu>(publisher_);
  AIMRT_CHECK_ERROR_THROW(ret, "Register publish type failed.");
}
void ImuSensorRos2Publisher::Start() {
}
void ImuSensorRos2Publisher::Shutdown() {
}
void ImuSensorRos2Publisher::PublishSensorData() {
  static constexpr uint32_t ONE_MB = 1024 * 1024;
  if (counter_++ < avg_interval_) return;
  auto state_array = std::make_unique<SensorStateGroup>();

  CopySensorData(sensor_addr_group_.framequat_addr, state_array->orientation, 4);
  CopySensorData(sensor_addr_group_.gyro_addr, state_array->angular_velocity, 3);
  CopySensorData(sensor_addr_group_.accelerometer_addr, state_array->linear_acceleration, 3);

  executor_.Execute([this, state_array = std::move(state_array)]() {
    sensor_msgs::msg::Imu state;
    state.header.stamp = rclcpp::Clock().now();
    state.orientation.x = state_array->orientation.x;
    state.orientation.y = state_array->orientation.y;
    state.orientation.z = state_array->orientation.z;
    state.orientation.w = state_array->orientation.w;
    state.angular_velocity.x = state_array->angular_velocity.x;
    state.angular_velocity.y = state_array->angular_velocity.y;
    state.angular_velocity.z = state_array->angular_velocity.z;
    state.linear_acceleration.x = state_array->linear_acceleration.x;
    state.linear_acceleration.y = state_array->linear_acceleration.y;
    state.linear_acceleration.z = state_array->linear_acceleration.z;
    aimrt::channel::Publish(publisher_, state);
  });
  avg_interval_ += avg_interval_base_;
  if (counter_ > ONE_MB) {
    avg_interval_ -= ONE_MB;
    counter_ -= ONE_MB;
  }
}
void ImuSensorRos2Publisher::RegisterSensorAddr() {
  sensor_addr_group_.framequat_addr = GetSensorAddr(m_, options_.bind_framequat),
  sensor_addr_group_.gyro_addr = GetSensorAddr(m_, options_.bind_gyro),
  sensor_addr_group_.accelerometer_addr = GetSensorAddr(m_, options_.bind_accelerometer);
}
void ImuSensorRos2Publisher::CopySensorData(int addr, auto& dest, size_t n) {
  if (addr >= 0) {
    std::memcpy(&dest, &d_->sensordata[addr], n * sizeof(double));
  }
};
}  // namespace aimrt_mujoco_sim::mujoco_sim_module::publisher