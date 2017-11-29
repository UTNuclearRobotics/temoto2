#include "ros/package.h"
#include "robot_manager/robot_manager.h"
#include "process_manager/process_manager_services.h"
#include <yaml-cpp/yaml.h>
#include <fstream>

//#include "temoto_2/LoadGesture.h"
//#include "rviz/QMap.h"
//#include "rviz/yaml_config_reader.h"
//#include "rviz/config.h"

namespace robot_manager
{
RobotManager::RobotManager() : resource_manager_(srv_name::MANAGER, this)
  , config_syncer_(srv_name::MANAGER, srv_name::SYNC_TOPIC, &RobotManager::syncCb, this)
{
  log_class_ = "";
  log_subsys_ = "robot_manager";
  log_group_ = "robot_manager";
  std::string prefix = common::generateLogPrefix(log_subsys_, log_class_, __func__);

  // Start the server for loading/unloading robots as resources
  resource_manager_.addServer<temoto_2::RobotLoad>(srv_name::SERVER_LOAD, &RobotManager::loadCb,
                                                    &RobotManager::unloadCb);
  resource_manager_.registerStatusCb(&RobotManager::statusInfoCb);

  // Ask remote robot managers to send their robot infos
  config_syncer_.requestRemoteConfigs();

  // Fire up additional servers for executing asynchronous actions.
  server_plan_ =
      nh_.advertiseService(robot_manager::srv_name::SERVER_PLAN, &RobotManager::planCb, this);
  server_exec_ =
      nh_.advertiseService(robot_manager::srv_name::SERVER_EXECUTE, &RobotManager::execCb, this);
  server_get_rviz_cfg_ = nh_.advertiseService(robot_manager::srv_name::SERVER_GET_RVIZ_CONFIG,
                                              &RobotManager::getRvizConfigCb, this);
  server_set_target_ = nh_.advertiseService(robot_manager::srv_name::SERVER_SET_TARGET,
                                            &RobotManager::setTargetCb, this);
  
  // Read the robot information for this manager. 
  std::string yaml_filename = ros::package::getPath(ROS_PACKAGE_NAME) + "/conf/" +
                              common::getTemotoNamespace() + ".yaml";
  std::ifstream in(yaml_filename);
  YAML::Node config = YAML::Load(in);
  if (config["Robots"])
  {
    local_robot_infos_ = parseRobotInfos(config);
    for (auto& info_ptr : local_robot_infos_)
    {
      TEMOTO_DEBUG("%s Added robot: '%s'.", prefix.c_str(), info_ptr->getName().c_str());
    }
    // notify other managers about our robots
    advertiseLocalRobotInfos();
  }
  else
  {
    TEMOTO_WARN("%s Failed to read '%s'. Verify that the file exists and the sequence of robots "
                "is listed under 'Robots' node.",
                prefix.c_str(), yaml_filename.c_str());
  }

  TEMOTO_INFO("Robot manager is ready.");
}

void RobotManager::loadCb(temoto_2::RobotLoad::Request& req, temoto_2::RobotLoad::Response& res)
{
  std::string prefix = common::generateLogPrefix(log_subsys_, log_class_, __func__);
  TEMOTO_INFO("%s Starting to load robot '%s'...", prefix.c_str(), req.robot_name.c_str());


  // Find the suitable robot and fill the process manager service request
  auto info_ptr = findRobot(req.robot_name, local_robot_infos_);
  if (info_ptr)
  {
    // Filled out message for process manager
    temoto_2::LoadProcess load_proc_srvc;
    load_proc_srvc.request.package_name = info_ptr->getPackageName();
    load_proc_srvc.request.action = process_manager::action::ROS_EXECUTE;
    load_proc_srvc.request.executable = info_ptr->getExecutable();

    TEMOTO_INFO("%s Loading a robot: '%s', '%s', '%s', %.3f", prefix.c_str(),
                info_ptr->getName().c_str(), info_ptr->getPackageName().c_str(),
                info_ptr->getExecutable().c_str(), info_ptr->getReliability());

    if (resource_manager_.call<temoto_2::LoadProcess>(
            process_manager::srv_name::MANAGER, process_manager::srv_name::SERVER, load_proc_srvc))
    {
      TEMOTO_DEBUG("%s Call to ProcessManager was sucessful.", prefix.c_str());

      // Wait for robot_description to become available on parameter server...
      while (!nh_.hasParam("robot_description"))
      {
        TEMOTO_DEBUG("%s Waiting for robot_description ...", prefix.c_str());
        ros::Duration(0.2).sleep();
      }
      ros::Duration(0.5).sleep();  // In case there is anythin else to load
      try
      {
        active_robot_ = std::make_shared<Robot>(info_ptr);
        loaded_robots_.emplace(load_proc_srvc.response.rmp.resource_id, active_robot_);
      }
      catch(...)
      {
        TEMOTO_ERROR("%s Exception occured while creating Robot object.", prefix.c_str());
      }
    }
    else
    {
      TEMOTO_ERROR("%s Failed to call to ProcessManager.", prefix.c_str());
      return;
    }

    res.rmp.code = load_proc_srvc.response.rmp.code;
    res.rmp.message = load_proc_srvc.response.rmp.message;

    // Increase or decrease reliability depending on the return code
    (res.rmp.code == 0) ? info_ptr->adjustReliability(1.0) : info_ptr->adjustReliability(0.0);

    TEMOTO_DEBUG("%s Robot '%s' loaded.", prefix.c_str(), req.robot_name.c_str());
    return;
  }

  // Try to find suitable candidate from remote managers
  info_ptr = findRobot(req.robot_name, remote_robot_infos_);
  if (info_ptr)
  {
    temoto_2::RobotLoad load_robot_srvc;
    load_robot_srvc.request.robot_name = req.robot_name;
    TEMOTO_INFO("%s RobotManager is forwarding request: '%s'",prefix.c_str(),req.robot_name.c_str());

    if (resource_manager_.call<temoto_2::RobotLoad>(robot_manager::srv_name::MANAGER, robot_manager::srv_name::SERVER_LOAD, load_robot_srvc, info_ptr->getTemotoNamespace()))
    {
      TEMOTO_DEBUG("%s Call to remote RobotManager was sucessful.", prefix.c_str());
      res.rmp.code = load_robot_srvc.response.rmp.code;
      res.rmp.message = load_robot_srvc.response.rmp.message;
      try
      {
        active_robot_ = std::make_shared<Robot>(info_ptr);
        loaded_robots_.emplace(load_robot_srvc.response.rmp.resource_id, active_robot_);
      }
      catch(...)
      {
        TEMOTO_ERROR("%s Exception occured while creating Robot object.", prefix.c_str());
      }
    }
    else
    {
      TEMOTO_ERROR("%s Failed to call the remote RobotManager.", prefix.c_str());
      return;
    }


    
    return;
  }

  // no local nor remote robot found
    res.rmp.code = 1;
    res.rmp.message = "Robot manager did not find a suitable robot.";
    TEMOTO_ERROR("%s %s", prefix.c_str(), res.rmp.message.c_str());
}

void RobotManager::unloadCb(temoto_2::RobotLoad::Request& req, temoto_2::RobotLoad::Response& res)
{
  std::string prefix = common::generateLogPrefix(log_subsys_, log_class_, __func__);
  TEMOTO_DEBUG("%s ROBOT '%s' unloaded...", prefix.c_str(), req.robot_name.c_str());

  loaded_robots_.erase(res.rmp.resource_id);
}

void RobotManager::syncCb(const temoto_2::ConfigSync& msg)
{
  std::string prefix = common::generateLogPrefix(log_subsys_, log_class_, __func__);

  if (msg.action == rmp::sync_action::REQUEST_CONFIG)
  {
    advertiseLocalRobotInfos();
    return;
  }

  if (msg.action == rmp::sync_action::ADVERTISE_CONFIG)
  {
    // Convert the config string to YAML tree and parse
    YAML::Node config = YAML::Load(msg.config);
    std::vector<RobotInfoPtr> robot_infos = parseRobotInfos(config);

    //TODO hold remote stuff in a map or something keyed by namespace
    for (auto& info_ptr : robot_infos)
    {
      info_ptr->setTemotoNamespace(msg.temoto_namespace);
    }

    for (auto& info_ptr : robot_infos)
    {
      // Check if robot info has to be added or updated
      auto it = std::find_if(remote_robot_infos_.begin(), remote_robot_infos_.end(),
          [&](const RobotInfoPtr& ri) { return *ri == *info_ptr; });
      if (it != remote_robot_infos_.end())
      {
        TEMOTO_DEBUG("%s Updating remote robot '%s' at '%s'.", prefix.c_str(),
            info_ptr->getName().c_str(), info_ptr->getTemotoNamespace().c_str());
        *it = info_ptr; // overwrite found entry
      }
      else
      {
        TEMOTO_DEBUG("%s Adding remote robot '%s' at '%s'.", prefix.c_str(),
            info_ptr->getName().c_str(), info_ptr->getTemotoNamespace().c_str());
        remote_robot_infos_.push_back(info_ptr);
      }
    }
  }
}

void RobotManager::advertiseLocalRobotInfos()
{
    // publish all local robots
    YAML::Node config;
    for(auto& info_ptr : local_robot_infos_) 
    {
        config["Robots"].push_back(*info_ptr);
    }
    
    // send to other managers if there is anything to send
    if(config.size())
    {
      config_syncer_.advertise(config);
    }
}

RobotInfos RobotManager::parseRobotInfos(const YAML::Node& config)
{
  std::string prefix = common::generateLogPrefix(log_subsys_, log_class_, __func__);
  RobotInfos robot_infos;

//  TEMOTO_DEBUG("%s CONFIG NODE:%d %s", prefix.c_str(), config.Type(), Dump(config).c_str());
  if (!config.IsMap())
  {
    // TODO Throw
    TEMOTO_WARN("%s Unable to parse 'Robots' key from config.", prefix.c_str());
    return robot_infos;
  }

  YAML::Node robots_node = config["Robots"];
  if (!robots_node.IsSequence())
  {
    TEMOTO_WARN("%s The given config does not contain sequence of robots.", prefix.c_str());
    // TODO Throw
    return robot_infos;
  }

  TEMOTO_DEBUG("%s Parsing %lu robots.", prefix.c_str(), robots_node.size());

  // go over each robot node in the sequence
  for (YAML::const_iterator node_it = robots_node.begin(); node_it != robots_node.end(); ++node_it)
  {
    if (!node_it->IsMap())
    {
      TEMOTO_ERROR("%s Unable to parse the robot info. Parameters in YAML have to be specified in "
                   "key-value pairs.",
                   prefix.c_str());
      continue;
    }

    try
    {
      RobotInfo robot_info = node_it->as<RobotInfo>();
      if (std::count_if(robot_infos.begin(), robot_infos.end(),
                        [&](const RobotInfoPtr& ri) { return *ri == robot_info; }) == 0)
      {
        // OK, this is unique info, add it to the robot_infos.
        robot_infos.emplace_back(std::make_shared<RobotInfo>(robot_info));
      }
      else
      {
        TEMOTO_WARN("%s Ignoring duplicate of robot '%s'.", prefix.c_str(),
                    robot_info.getName().c_str());
      }
    }
    catch (YAML::TypedBadConversion<RobotInfo> e)
    {
      TEMOTO_WARN("%s Failed to parse RobotInfo from config.", prefix.c_str());
      continue;
    }
  }
  return robot_infos;
}

bool RobotManager::planCb(temoto_2::RobotPlan::Request& req, temoto_2::RobotPlan::Response& res)
{
  std::string prefix = common::generateLogPrefix(log_subsys_, log_class_, __func__);
  TEMOTO_DEBUG("%s PLANNING...", prefix.c_str());
  if(active_robot_)
  {
    if (active_robot_->isLocal())
    {
      if (req.use_default_target)
      {
        geometry_msgs::PoseStamped pose;
        default_pose_mutex_.lock();
        pose = default_target_pose_;
        default_pose_mutex_.unlock();
        active_robot_->plan("manipulator", pose);
      }
      else
      {
        active_robot_->plan("manipulator", req.target_pose);
      }
      TEMOTO_DEBUG("%s DONE PLANNING...", prefix.c_str());
    }
    else
    {
      // Forward the planning command to a remote robot manager
      std::string topic = "/" + active_robot_->getRobotInfo()->getTemotoNamespace() + "/" +
                          robot_manager::srv_name::SERVER_PLAN;
      ros::ServiceClient client_plan = nh_.serviceClient<temoto_2::RobotPlan>(topic);
      temoto_2::RobotPlan fwd_plan_srvc;
      fwd_plan_srvc.request = req;
      fwd_plan_srvc.response = res;
      if(client_plan.call(fwd_plan_srvc))
      {
        TEMOTO_DEBUG("%s Call to remote RobotManager was sucessful.", prefix.c_str());
        res = fwd_plan_srvc.response;
      }
      else
      {
        TEMOTO_ERROR("%s Call to remote RobotManager failed.", prefix.c_str());
      }
    }
  }
  else
  {
    TEMOTO_ERROR("%s Unable to plan, because the robot is not loaded.", prefix.c_str());
  }

  return true;
}

bool RobotManager::execCb(temoto_2::RobotExecute::Request& req,
                          temoto_2::RobotExecute::Response& res)
{
  std::string prefix = common::generateLogPrefix(log_subsys_, log_class_, __func__);
  TEMOTO_INFO("%s EXECUTING...", prefix.c_str());
  if(active_robot_)
  {
    active_robot_->execute("manipulator");
    TEMOTO_DEBUG("%s DONE EXECUTING...", prefix.c_str());
  }
  else
  {
    TEMOTO_ERROR("%s Unable to execute, because the robot is not loaded.", prefix.c_str());
  }
  return true;
}

bool RobotManager::getRvizConfigCb(temoto_2::RobotGetRvizConfig::Request& req,
                                   temoto_2::RobotGetRvizConfig::Response& res)
{
  std::string prefix = common::generateLogPrefix(log_subsys_, log_class_, __func__);
  TEMOTO_INFO("%s GETTING CONFIG...", prefix.c_str());
  if(!active_robot_)
  {
    TEMOTO_ERROR("%s Robot not loaded.", prefix.c_str());
  }
  else
  {
    std::string act_rob_ns = active_robot_->getRobotInfo()->getTemotoNamespace();
    std::string rviz_conf = "{Robot Description: /" + act_rob_ns +
                            "/robot_description, Planning Request: {Planning Group: "
                            "manipulator}}";

    res.config = rviz_conf;
  }
  res.code = 0;
  return true;
}

bool RobotManager::setTargetCb(temoto_2::RobotSetTarget::Request& req,
                               temoto_2::RobotSetTarget::Response& res)
{
  std::string prefix = common::generateLogPrefix(log_subsys_, log_class_, __func__);
  TEMOTO_INFO("%s Looking for target of type '%s'", prefix.c_str(), req.target_type.c_str());


  // Only "hand" type is experimentally implemented
  if (req.target_type == "hand")
  {
    std::vector<temoto_2::GestureSpecifier> gesture_specifiers;
    temoto_2::GestureSpecifier gesture_specifier;
    gesture_specifier.dev = "device";
    gesture_specifier.type = "hand";
//    gesture_specifiers.push_back(gesture_specifier);

    hand_srv_msg_.request.gesture_specifiers.push_back(gesture_specifier);
    if (resource_manager_.call<temoto_2::LoadGesture>(
            context_manager::srv_name::MANAGER, context_manager::srv_name::GESTURE_SERVER, hand_srv_msg_))
    {
      TEMOTO_DEBUG("%s Call to ContextManager was sucessful.", prefix.c_str());
      res.code = 0;
      res.message = "Robot manager got a 'hand' topic from context_manager.";
      TEMOTO_DEBUG("%s Subscribing to '%s'", prefix.c_str(), hand_srv_msg_.response.topic.c_str());
      target_pose_sub_ = nh_.subscribe(hand_srv_msg_.response.topic, 1, &RobotManager::targetPoseCb, this);

    }
    else
    {
      TEMOTO_ERROR("%s Failed to call ContextManager.", prefix.c_str());
      res.code = 1;
      res.message = "Failed to call ContextManager.";
      return true;
    }
  }
return true;
}

// Take palm pose of whichever hand is present, prefer left_hand.
// Store the pose in a class member for later use when planning is requested.
void RobotManager::targetPoseCb(const leap_motion_controller::Set& set)
{
  if (set.left_hand.is_present)
  {
    default_pose_mutex_.lock();
    default_target_pose_ = set.left_hand.palm_pose;
    default_pose_mutex_.unlock();
  }
  else if (set.right_hand.is_present)
  {
    default_pose_mutex_.lock();
    default_target_pose_ = set.right_hand.palm_pose;
    default_pose_mutex_.unlock();
  }
}

void RobotManager::statusInfoCb(temoto_2::ResourceStatus& srv)
  {
    std::string prefix = common::generateLogPrefix(log_subsys_, log_class_, __func__);

    TEMOTO_DEBUG("%s status info was received", prefix.c_str());
    TEMOTO_DEBUG_STREAM(srv.request);
    // if any resource should fail, just unload it and try again
    // there is a chance that sensor manager gives us better sensor this time
    if (srv.request.status_code == rmp::status_codes::FAILED &&
        srv.request.resource_id == hand_srv_msg_.response.rmp.resource_id)
    {
      TEMOTO_WARN("Robot manager detected a hand sensor failure. Unloading and "
                  "trying again");
      resource_manager_.unloadClientResource(hand_srv_msg_.response.rmp.resource_id);

      //retry with previous request
      if (resource_manager_.call<temoto_2::LoadGesture>(context_manager::srv_name::MANAGER,
                                                        context_manager::srv_name::GESTURE_SERVER,
                                                        hand_srv_msg_))
      {
        TEMOTO_DEBUG("%s Call to ContextManager was sucessful.", prefix.c_str());
        TEMOTO_DEBUG("%s Subscribing to '%s'", prefix.c_str(), hand_srv_msg_.response.topic.c_str());
        target_pose_sub_ =
            nh_.subscribe(hand_srv_msg_.response.topic, 1, &RobotManager::targetPoseCb, this);
      }
      else
      {
        TEMOTO_ERROR("%s Failed to call ContextManager.", prefix.c_str());
      }
    }

    // Check if any of the allocated robots has failed
    // Currently we simply remove the loaded robot if it failed
    if (srv.request.status_code == rmp::status_codes::FAILED
        )
    {
      auto it = loaded_robots_.find(srv.request.resource_id);
      if (it != loaded_robots_.end())
      {
        RobotInfoPtr info_ptr = it->second->getRobotInfo();
        info_ptr->adjustReliability(0.0);
        YAML::Node config;
        config["Robots"].push_back(*info_ptr);
        config_syncer_.advertise(config);
      }
    }
  }

RobotInfoPtr RobotManager::findRobot(const std::string& robot_name, const RobotInfos& robot_infos)
{
  std::string prefix = common::generateLogPrefix(log_subsys_, log_class_, __func__);
  
  // Local list of devices that follow the requirements
  RobotInfos candidates;

  // Find the robot that matches the "name" criteria
  auto it = std::copy_if(robot_infos.begin(), robot_infos.end(), std::back_inserter(candidates),
                         [&](const RobotInfoPtr& s) { return s->getName() == robot_name; });

  // If the list is empty, leave the req empty
  if (candidates.empty())
  {
    return NULL;
  }

  std::sort(candidates.begin(), candidates.end(),
            [](RobotInfoPtr& pkg_ptr1, RobotInfoPtr& pkg_ptr2) {
              return pkg_ptr1->getReliability() > pkg_ptr2->getReliability();
            });

  // Get the name of the package and first launchable
  return candidates.front();
}

}  // namespace robot_manager
