//=================================================================================================
// Copyright (c) 2016, Alexander Stumpf, TU Darmstadt
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the Simulation, Systems Optimization and Robotics
//       group, TU Darmstadt nor the names of its contributors may be used to
//       endorse or promote products derived from this software without
//       specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//=================================================================================================

#ifndef WALK_CONTROLLER_H__
#define WALK_CONTROLLER_H__

#include <ros/ros.h>

#include <actionlib/server/simple_action_server.h>

#include <vigir_pluginlib/plugin_manager.h>

#include <vigir_footstep_planning_msgs/footstep_planning_msgs.h>
#include <vigir_footstep_planning_plugins/plugins/step_plan_msg_plugin.h>

#include <vigir_walk_control/walk_controller_plugin.h>



namespace vigir_walk_control
{
using namespace vigir_footstep_planning_msgs;

typedef actionlib::SimpleActionServer<msgs::ExecuteStepPlanAction> ExecuteStepPlanActionServer;
typedef boost::shared_ptr<ExecuteStepPlanActionServer> ExecuteStepPlanActionServerPtr;

class WalkController
{
public:
  // typedefs
  typedef boost::shared_ptr<WalkController> Ptr;
  typedef boost::shared_ptr<const WalkController> ConstPtr;

  /**
   * @brief WalkController
   * @param nh Nodehandle living in correct namespace for all services
   * @param spin When true, the controller sets up it's own ros timer for calling update(...) continously.
   */
  WalkController(ros::NodeHandle& nh, bool auto_spin = true);
  virtual ~WalkController();

  /**
   * @brief Loads plugin with specific name to be used by the controller. The name should be configured
   * in the plugin config file and loaded to the rosparam server. The call can only succeed when currentl
   * no exection is runnning.
   * @param plugin_name Name of plugin
   */
  template<typename T>
  void loadPlugin(const std::string& plugin_name, boost::shared_ptr<T>& plugin)
  {
    boost::unique_lock<boost::shared_mutex> lock(controller_mutex_);

    if (walk_controller_plugin_ && walk_controller_plugin_->getState() == ACTIVE)
    {
      ROS_ERROR("[WalkController] Cannot replace plugin due to active footstep execution!");
      return;
    }

    if (!vigir_pluginlib::PluginManager::addPluginByName(plugin_name))
    {
      ROS_ERROR("[WalkController] Could not load plugin '%s'!", plugin_name.c_str());
      return;
    }
    else if (!vigir_pluginlib::PluginManager::getPlugin(plugin))
    {
      ROS_ERROR("[WalkController] Could not obtain plugin '%s' from plugin manager!", plugin_name.c_str());
      return;
    }
    else
      ROS_INFO("[WalkController] Loaded plugin '%s'.", plugin_name.c_str());
  }

  /**
   * @brief Instruct the controller to execute the given step plan. If execution is already in progress,
   * the step plan will be merged into current execution queue.
   * @param Step plan to be executed
   */
  void executeStepPlan(const msgs::StepPlan& step_plan);

  /**
   * @brief Main update loop to be called in regular intervals.
   */
  void update(const ros::TimerEvent& event = ros::TimerEvent());

protected:
  /**
   * @brief Publishes feedback messages of current state of execution.
   */
  void publishFeedback() const;

  vigir_footstep_planning::StepPlanMsgPlugin::Ptr step_plan_msg_plugin_;
  WalkControllerPlugin::Ptr walk_controller_plugin_;

  // mutex to ensure thread safeness
  boost::shared_mutex controller_mutex_;

  /// ROS API

  // subscriber
  void loadStepPlanMsgPlugin(const std_msgs::StringConstPtr& plugin_name);
  void loadWalkControllerPlugin(const std_msgs::StringConstPtr& plugin_name);
  void executeStepPlan(const msgs::StepPlanConstPtr& step_plan);

  // action server calls
  void executeStepPlanAction(ExecuteStepPlanActionServerPtr& as);
  void executePreemptionAction(ExecuteStepPlanActionServerPtr& as);

  // subscriber
  ros::Subscriber load_step_plan_msg_plugin_sub_;
  ros::Subscriber load_walk_controller_plugin_sub_;
  ros::Subscriber execute_step_plan_sub_;

  // publisher
  ros::Publisher planning_feedback_pub_;

  // action servers
  boost::shared_ptr<ExecuteStepPlanActionServer> execute_step_plan_as_;

  // timer for updating periodically
  ros::Timer update_timer_;
};
}

#endif
