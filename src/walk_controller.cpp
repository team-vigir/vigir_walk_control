#include <vigir_walk_control/walk_controller_node.h>

#include <vigir_generic_params/parameter_manager.h>
#include <vigir_pluginlib/plugin_manager.h>



namespace vigir_walk_control
{
WalkController::WalkController(ros::NodeHandle& nh, bool auto_spin)
{
  vigir_pluginlib::PluginManager::addPluginClassLoader<WalkControllerPlugin>("vigir_walk_control", "vigir_walk_control::WalkControllerPlugin");

  // init walk controller plugin
  std::string plugin_name = nh.param("plugin", std::string("walk_controller_test_plugin"));
  loadPluginByName(plugin_name);

  // subscribe topics
  load_plugin_by_name_sub = nh.subscribe("load_plugin_by_name", 1, &WalkController::loadPluginByName, this);
  execute_step_plan_sub = nh.subscribe("execute_step_plan", 1, &WalkController::executeStepPlan, this);

  // publish topics
  planning_feedback_pub = nh.advertise<msgs::ExecuteStepPlanFeedback>("execute_feedback", 1, true);

  // init action servers
  execute_step_plan_as.reset(new ExecuteStepPlanActionServer(nh, "execute_step_plan", false));
  execute_step_plan_as->registerGoalCallback(boost::bind(&WalkController::executeStepPlanAction, this, boost::ref(execute_step_plan_as)));
  execute_step_plan_as->registerPreemptCallback(boost::bind(&WalkController::executePreemptionAction, this, boost::ref(execute_step_plan_as)));

  // start action servers
  execute_step_plan_as->start();

  // schedule main update loop
  if (auto_spin)
    update_timer = nh.createTimer(nh.param("rate", 10.0), &WalkController::update, this);
}

WalkController::~WalkController()
{
}

void WalkController::loadPluginByName(const std::string& plugin_name)
{
  if (walk_controller_plugin && walk_controller_plugin->getState() != IDLE)
  {
    ROS_ERROR("[WalkController] Cannot replace plugin due to active footstep execution!");
    return;
  }

  if (!vigir_pluginlib::PluginManager::addPluginByName(plugin_name))
  {
    ROS_ERROR("[WalkController] Could not load plugin '%s'!", plugin_name.c_str());
    return;
  }
  else
    ROS_INFO("[WalkController] Loaded plugin '%s'.", plugin_name.c_str());

  vigir_pluginlib::PluginManager::getPlugin(walk_controller_plugin);
}

void WalkController::executeStepPlan(const msgs::StepPlan& step_plan)
{
  // An empty step plan will always trigger a soft stop
  if (step_plan.steps.empty())
    walk_controller_plugin->stop();
  else
    walk_controller_plugin->updateStepPlan(step_plan);
}

void WalkController::update(const ros::TimerEvent& event)
{
  if (!walk_controller_plugin)
    return;

  // pre process
  walk_controller_plugin->preProcess(event);

  // process
  walk_controller_plugin->process(event);

  // publish feedback
  publishFeedback();

  // update action server
  switch (walk_controller_plugin->getState())
  {
    case FINISHED:
      if (execute_step_plan_as->isActive())
        execute_step_plan_as->setSucceeded(msgs::ExecuteStepPlanResult());
      break;

    case FAILED:
      if (execute_step_plan_as->isActive())
        execute_step_plan_as->setAborted(msgs::ExecuteStepPlanResult());
      break;

    default:
      break;
  }

  // post process
  walk_controller_plugin->postProcess(event);
}

void WalkController::publishFeedback() const
{
  if (walk_controller_plugin->getState() == ACTIVE)
  {
    const msgs::ExecuteStepPlanFeedback& feedback = walk_controller_plugin->getFeedback();

    // publish feedback
    planning_feedback_pub.publish(feedback);

    if (execute_step_plan_as->isActive())
      execute_step_plan_as->publishFeedback(feedback);
  }
}

// --- Subscriber calls ---

void WalkController::loadPluginByName(const std_msgs::StringConstPtr& plugin_name)
{
  loadPluginByName(plugin_name->data);
}

void WalkController::executeStepPlan(const msgs::StepPlanConstPtr& step_plan)
{
  executeStepPlan(*step_plan);
}

//--- action server calls ---

void WalkController::executeStepPlanAction(ExecuteStepPlanActionServerPtr& as)
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

void WalkController::executePreemptionAction(ExecuteStepPlanActionServerPtr& as)
{
  if (as->isActive())
    as->setPreempted();

  //walk_controller_plugin->stop();
}
}