#include <vigir_step_control/step_controller.h>

#include <vigir_generic_params/parameter_manager.h>



namespace vigir_step_control
{
StepController::StepController(ros::NodeHandle& nh, bool auto_spin)
{
  vigir_pluginlib::PluginManager::addPluginClassLoader<vigir_footstep_planning::StepPlanMsgPlugin>("vigir_footstep_planning_plugins", "vigir_footstep_planning::StepPlanMsgPlugin");
  vigir_pluginlib::PluginManager::addPluginClassLoader<StepControllerPlugin>("vigir_step_control", "vigir_step_control::StepControllerPlugin");

  // init step plan msg plugin
  loadPlugin(nh.param("step_plan_msg_plugin", std::string("step_plan_msg_plugin")), step_plan_msg_plugin_);

  // init walk controller plugin
  loadPlugin(nh.param("step_controller_plugin", std::string("step_controller_test_plugin")), step_controller_plugin_);
  if (step_controller_plugin_)
    step_controller_plugin_->setStepPlanMsgPlugin(step_plan_msg_plugin_);

  // subscribe topics
  load_step_plan_msg_plugin_sub_ = nh.subscribe("load_step_plan_msg_plugin", 1, &StepController::loadStepPlanMsgPlugin, this);
  load_step_controller_plugin_sub_ = nh.subscribe("load_step_controller_plugin", 1, &StepController::loadStepControllerPlugin, this);
  execute_step_plan_sub_ = nh.subscribe("execute_step_plan", 1, &StepController::executeStepPlan, this);

  // publish topics
  planning_feedback_pub_ = nh.advertise<msgs::ExecuteStepPlanFeedback>("execute_feedback", 1, true);

  // init action servers
  execute_step_plan_as_.reset(new ExecuteStepPlanActionServer(nh, "execute_step_plan", false));
  execute_step_plan_as_->registerGoalCallback(boost::bind(&StepController::executeStepPlanAction, this, boost::ref(execute_step_plan_as_)));
  execute_step_plan_as_->registerPreemptCallback(boost::bind(&StepController::executePreemptionAction, this, boost::ref(execute_step_plan_as_)));

  // start action servers
  execute_step_plan_as_->start();

  // schedule main update loop
  if (auto_spin)
    update_timer_ = nh.createTimer(nh.param("rate", 10.0), &StepController::update, this);
}

StepController::~StepController()
{
}

void StepController::executeStepPlan(const msgs::StepPlan& step_plan)
{
  boost::unique_lock<boost::shared_mutex> lock(controller_mutex_);

  if (!step_controller_plugin_)
  {
    ROS_ERROR("[StepController] executeStepPlan: No step_controller_plugin available!");
    return;
  }

  // An empty step plan will always trigger a soft stop
  if (step_plan.steps.empty())
    step_controller_plugin_->stop();
  else
    step_controller_plugin_->updateStepPlan(step_plan);
}

void StepController::update(const ros::TimerEvent& event)
{
  boost::unique_lock<boost::shared_mutex> lock(controller_mutex_);

  if (!step_controller_plugin_)
  {
    ROS_ERROR_THROTTLE(5.0, "[StepController] update: No step_controller_plugin available!");
    return;
  }

  // Save current state to be able to handle action server correctly;
  // We must not send setSucceeded/setAborted state while sending the
  // final feedback message in the same update cycle!
  StepControllerState state = step_controller_plugin_->getState();

  // pre process
  step_controller_plugin_->preProcess(event);

  // process
  step_controller_plugin_->process(event);

  // publish feedback
  publishFeedback();

  // update action server
  switch (state)
  {
    case FINISHED:
      if (execute_step_plan_as_->isActive())
        execute_step_plan_as_->setSucceeded(msgs::ExecuteStepPlanResult());
      break;

    case FAILED:
      if (execute_step_plan_as_->isActive())
        execute_step_plan_as_->setAborted(msgs::ExecuteStepPlanResult());
      break;

    default:
      break;
  }

  // post process
  step_controller_plugin_->postProcess(event);
}

void StepController::publishFeedback() const
{
  if (step_controller_plugin_->getState() != READY)
  {
    const msgs::ExecuteStepPlanFeedback& feedback = step_controller_plugin_->getFeedbackState();

    // publish feedback
    planning_feedback_pub_.publish(feedback);

    if (execute_step_plan_as_->isActive())
      execute_step_plan_as_->publishFeedback(feedback);
  }
}

// --- Subscriber calls ---

void StepController::loadStepPlanMsgPlugin(const std_msgs::StringConstPtr& plugin_name)
{
  loadPlugin(plugin_name->data, step_plan_msg_plugin_);

  if (step_controller_plugin_)
    step_controller_plugin_->setStepPlanMsgPlugin(step_plan_msg_plugin_);
}

void StepController::loadStepControllerPlugin(const std_msgs::StringConstPtr& plugin_name)
{
  loadPlugin(plugin_name->data, step_controller_plugin_);

  if (step_controller_plugin_)
    step_controller_plugin_->setStepPlanMsgPlugin(step_plan_msg_plugin_);
}

void StepController::executeStepPlan(const msgs::StepPlanConstPtr& step_plan)
{
  executeStepPlan(*step_plan);
}

//--- action server calls ---

void StepController::executeStepPlanAction(ExecuteStepPlanActionServerPtr& as)
{
  const msgs::ExecuteStepPlanGoalConstPtr& goal(as->acceptNewGoal());

  // check if new goal was preempted in the meantime
  if (as->isPreemptRequested())
  {
    as->setPreempted();
    return;
  }

  executeStepPlan(goal->step_plan);
}

void StepController::executePreemptionAction(ExecuteStepPlanActionServerPtr& as)
{
  if (as->isActive())
    as->setPreempted();

  //step_controller_plugin->stop();
}
} // namespace
