/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *          MASSIVE TODO: * CATCH ALL EXCEPTIONS !!!
 *                        * implement interprocess piping service
 *                          that starts streaming the std::out of
 *                          a requested process.
 *                        * organize your sh*t
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "core/common.h"
#include "process_manager/process_manager.h"

#include <stdio.h>
#include <csignal>
#include <sys/wait.h>
#include <algorithm>

namespace process_manager
{
ProcessManager::ProcessManager() : resource_manager_(srv_name::MANAGER, this)
{
  resource_manager_.addServer<temoto_2::LoadProcess>(srv_name::SERVER, &ProcessManager::loadCb,
                                                     &ProcessManager::unloadCb);
}

ProcessManager::~ProcessManager()
{
}

// Timer callback where running proceses are checked if they are operational
void ProcessManager::update(const ros::TimerEvent&)
{
  // auto find_it = std::find(running_processes_.begin(), running_processes_.end(), res.resource_id);
  // if(find_it == running_processes_.end())
  //{
  //	ROS_ERROR("%s unable to obtain PID for resource with id %d", res.resource_id);
  //	return false;
  //}

  std::string prefix = node_name_ + "::" + __func__;

  // Run through the list of running processes
  auto proc_it = running_processes_.begin();
  while (proc_it != running_processes_.end())
  {
    int status;
    int kill_response = waitpid(proc_it->first, &status, WNOHANG);

    ROS_INFO("Resource_id '%d'(PID = %d) waitpid response = %d, status = %d\n", proc_it->second, proc_it->first,
             kill_response, status);

    // If the child process has stopped running,
    if (kill_response != 0)
    {
      ROS_ERROR("%s Process '%d'(PID = %d) has stopped, removing from process list and reporting", prefix.c_str(),
                proc_it->second, proc_it->first);

      // TODO: send error information to all related connections
      temoto_2::ResourceStatus status_msg;
      status_msg.request.resource_id = proc_it->second;
      status_msg.request.status_code = rmp::status_codes::FAILED;
      std::stringstream ss("The process with PID = ");
      ss << proc_it->first << " was stopped.";
      status_msg.request.message = ss.str();

      resource_manager_.sendStatus(proc_it->second, status_msg);

      // remove stopped process from the map
      running_processes_.erase(proc_it++);
    }
    else
    {
      proc_it++;
    }
  }
}

void ProcessManager::unloadCb(temoto_2::LoadProcess::Request& req, temoto_2::LoadProcess::Response& res)
{
  std::string prefix = node_name_ + "::" + __func__;
  ROS_INFO("%s Unload resource requested ...", prefix.c_str());

  // Lookup the requested process by its resource id.
  pid_t active_pid = 0;
  bool pid_found = false;
  for (auto& proc : running_processes_)
  {
    if (proc.second == res.rmp.resource_id)
    {
      active_pid = proc.first;
      pid_found = true;
      break;
    }
  }
  if (!pid_found)
  {
    ROS_ERROR("%s unable to obtain PID for resource with id %ld", prefix.c_str(), res.rmp.resource_id);
    // TODO: throw exception
    return;
  }

  // Kill the process
  ROS_INFO("%s killing the child process (PID = %d)", prefix.c_str(), active_pid);

  // TODO: Check the returned value
  kill(active_pid, SIGINT);

  // Remove the process from the map
  running_processes_.erase(active_pid);
}

void ProcessManager::loadCb(temoto_2::LoadProcess::Request& req, temoto_2::LoadProcess::Response& res)
{
  ROS_INFO("loadCb reached");
  // Name of the method, used for making debugging a bit simpler
  std::string prefix = node_name_ + "::" + __func__;

  // Get the service parameters
  const std::string& action = req.action;
  const std::string& package_name = req.package_name;
  const std::string& executable = req.executable;

  ROS_INFO("%s Received a 'LoadProcess' service request: %s ...", prefix.c_str(), executable.c_str());

  // Validate the action command.
  if (std::find(validActions.begin(), validActions.end(), action) == validActions.end())
  {
    ROS_INFO("%s Action '%s' is not supported ...", prefix.c_str(), action.c_str());
    // TODO THROW EXCEPTION
  }

  // TODO: Check if the package and node/launchfile exists
  // If everything is fine, run the commands

  // Fork the parent process
  std::cout << "forking the process ..." << std::endl;
  pid_t PID = fork();

  // Child process
  if (PID == 0)
  {
    // Execute the requested process
    execlp(action.c_str(), action.c_str(), package_name.c_str(), executable.c_str(), (char*)NULL);
  }

  // Only parent gets here
  std::cout << "Process forked. Child PID: " << PID << std::endl;

  // Insert the pid to map of running processes
  // use the response resource_id that is generated by rmp
  running_processes_.insert({ PID, res.rmp.resource_id });

  // Fill response
  res.rmp.code = 0;
  res.rmp.message = "Command executed successfully.";
}

}  // namespace process_manager
