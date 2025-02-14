// Copyright (c) 2023, AgiBot Inc.
// All rights reserved.

#include "mujoco_sim_module/subscriber/joint_pd_actuator_ros2_subscriber.h"
#include "aimrt_module_ros2_interface/channel/ros2_channel.h"

namespace YAML {
template <>
struct convert<aimrt_mujoco_sim::mujoco_sim_module::subscriber::JointPdActuatorRos2Subscriber::Options> {
  using Options = aimrt_mujoco_sim::mujoco_sim_module::subscriber::JointPdActuatorRos2Subscriber::Options;

  static Node encode(const Options& rhs) {
    Node node;

    node["joints"] = YAML::Node();
    for (const auto& joint : rhs.joints) {
      Node joint_node;
      joint_node["name"] = joint.name;
      joint_node["bind_joint"] = joint.bind_joint;
      joint_node["bind_actuator_type"] = joint.bind_actuator_type;
      joint_node["bind_actuator_name"] = joint.bind_actuator_name;
      node["joints"].push_back(joint_node);
    }

    return node;
  }

  static bool decode(const Node& node, Options& rhs) {
    if (node["joints"] && node["joints"].IsSequence()) {
      for (const auto& joint_node : node["joints"]) {
        auto joint_node_options = Options::Joint{
            .name = joint_node["name"].as<std::string>(),
            .bind_joint = joint_node["bind_joint"].as<std::string>(),
            .bind_actuator_type = joint_node["bind_actuator_type"].as<std::string>(),
            .bind_actuator_name = joint_node["bind_actuator_name"].as<std::string>()};

        AIMRT_ASSERT(joint_node_options.bind_actuator_type == "position" ||
                         joint_node_options.bind_actuator_type == "velocity" ||
                         joint_node_options.bind_actuator_type == "motor",
                     "Invalid bind_actuator_type '{}'.", joint_node_options.bind_actuator_type);

        rhs.joints.emplace_back(std::move(joint_node_options));
      }
    }
    return true;
  }
};
}  // namespace YAML

namespace aimrt_mujoco_sim::mujoco_sim_module::subscriber {

void JointPdActuatorRos2Subscriber::Initialize(YAML::Node options_node) {
  if (options_node && !options_node.IsNull())
    options_ = options_node.as<Options>();

  RegisterActuatorAddr();

  options_node = options_;

  bool ret = aimrt::channel::Subscribe<sensor_ros2::msg::JointPdState>(
      subscriber_,
      std::bind(&JointPdActuatorRos2Subscriber::EventHandle, this, std::placeholders::_1));

  AIMRT_CHECK_ERROR_THROW(ret, "Subscribe failed.");
}

void JointPdActuatorRos2Subscriber::ApplyCtrlData() {
  auto* current_command_array = command_array_.exchange(nullptr);
  if (current_command_array != nullptr) {
    // new data, update control value
    for (size_t i = 0; i < joint_num_; ++i) {
      d_->ctrl[actuator_addr_vec_[i]] = current_command_array[i];
    }

    delete[] current_command_array;
  }
}

void JointPdActuatorRos2Subscriber::EventHandle(const std::shared_ptr<const sensor_ros2::msg::JointPdState>& commands) {
  if (stop_flag_) [[unlikely]]
    return;

  auto* new_command_array = new double[joint_num_];

  for (size_t ii = 0; ii < joint_num_; ++ii) {
    const auto& joint_options = options_.joints[ii];
    const auto command = commands->joints[ii];

    if (std::ranges::find(joint_names_vec_, command.name) == joint_names_vec_.end()) [[unlikely]] {
      AIMRT_WARN("Invalid msg for topic '{}', msg: {}, Joint name '{}' is not matched.",
                 subscriber_.GetTopic(), sensor_ros2::msg::to_yaml(*commands), command.name);

      delete[] new_command_array;
      return;
    }

    if (joint_options.bind_actuator_type == "position") {
      new_command_array[ii] = command.position;
    } else if (joint_options.bind_actuator_type == "velocity") {
      new_command_array[ii] = command.velocity;
    } else {
      // motor
      int32_t actuator_addr = actuator_addr_vec_[ii];
      double state_posiotin = d_->qpos[actuator_addr];
      double state_velocity = d_->qvel[actuator_addr];
      new_command_array[ii] = command.effort +
                              command.stiffness * (command.position - state_posiotin) +
                              command.damping * (command.velocity - state_velocity);
    }
  }

  auto* old_command_array = command_array_.exchange(new_command_array);
  delete[] old_command_array;
}

void JointPdActuatorRos2Subscriber::RegisterActuatorAddr() {
  uint32_t idx = 0;
  for (auto const& joint : options_.joints) {
    uint32_t actuator_id = m_->sensor_adr[mj_name2id(m_, mjOBJ_ACTUATOR, joint.bind_actuator_name.c_str())];
    AIMRT_CHECK_ERROR_THROW(actuator_id >= 0, "Invalid bind_actuator_name '{}'.", joint.bind_actuator_name);

    actuator_addr_vec_.emplace_back(actuator_id);
    joint_names_vec_.emplace_back(joint.name);
  }

  joint_num_ = actuator_addr_vec_.size();
}

}  // namespace aimrt_mujoco_sim::mujoco_sim_module::subscriber
