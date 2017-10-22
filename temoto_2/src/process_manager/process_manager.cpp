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
#include <spawn.h>

namespace process_manager
{
ProcessManager::ProcessManager() : resource_manager_(srv_name::MANAGER, this)
{
  resource_manager_.addServer<temoto_2::LoadProcess>(srv_name::SERVER, &ProcessManager::loadCb,
                                                     &ProcessManager::unloadCb);
  ROS_INFO_STREAM(node_name_ << " is good to go.");
}

ProcessManager::~ProcessManager()
{
}

// Timer callback where running proceses are checked if they are operational
void ProcessManager::update(const ros::TimerEvent&)
{
  std::string prefix = node_name_ + "::" + __func__;

  // execute each process in loading_processes vector
  running_mutex_.lock();
  loading_mutex_.lock();
  for (auto& srv : loading_processes_)
  {
    const std::string& action = srv.request.action;
    const std::string& package_name = srv.request.package_name;
    const std::string& executable = srv.request.executable;

    // Fork the parent process
    ROS_DEBUG_NAMED(node_name_,"%s Forking the process.", prefix.c_str());
    pid_t pid = fork();

    // Child process
    if (pid == 0)
    {
      // Execute the requested process
      //  std::cout << "Child is executing a program ..." << std::endl;
      execlp(action.c_str(), action.c_str(), package_name.c_str(), executable.c_str(), (char*)NULL);
      return;
    }

    // Only parent gets here
    ROS_DEBUG_NAMED(node_name_,"%s Child %d forked.", prefix.c_str(), pid);
    running_processes_.insert({ pid, srv });
  }
  loading_processes_.clear();
  loading_mutex_.unlock();

  // unload each process in unload_processes vector
  unloading_mutex_.lock();
  for (auto pid : unloading_processes_)
  {
    // consistency check, ensure that pid is still in running_processes
    auto proc_it = running_processes_.find(pid);
    if (proc_it != running_processes_.end())
    {
      // Kill the process
      ROS_DEBUG_NAMED(node_name_,"%s Sending kill(SIGTERM) to %d)", prefix.c_str(), pid);

      int ret = kill(pid, SIGTERM);
      ROS_DEBUG_NAMED(node_name_,"%s kill(SIGTERM) returned: %d)", prefix.c_str(), ret);
      // TODO: Check the returned value

      // Remove the process from the map
      running_processes_.erase(proc_it);
    }
    else
    {
      ROS_ERROR_NAMED(node_name_,"%s Unable to unload reource with pid: %d. Resource is not running.",
                prefix.c_str(), pid);
    }
  }
  unloading_processes_.clear();
  unloading_mutex_.unlock();

  // Check the status of all running processes
  auto proc_it = running_processes_.begin();
  while (proc_it != running_processes_.end())
  {
    int status;
    int kill_response = waitpid(proc_it->first, &status, WNOHANG);
    // If the child process has stopped running,
    if (kill_response != 0)
    {
      ROS_ERROR_NAMED(node_name_,"%s Process %d ('%s' '%s' '%s') has stopped. Removing and reporting.",
                prefix.c_str(), proc_it->first, proc_it->second.request.action.c_str(),
                proc_it->second.request.package_name.c_str(), proc_it->second.request.executable.c_str());

      // TODO: send error information to all related connections
      temoto_2::ResourceStatus srv;
      srv.request.resource_id = proc_it->second.response.rmp.resource_id;
      srv.request.status_code = rmp::status_codes::FAILED;
      std::stringstream ss;
      ss << "The process with pid = ";
      ss << proc_it->first;
      ss << " was stopped.";
      srv.request.message = ss.str();

      resource_manager_.sendStatus(srv);

      // remove stopped process from the map
      running_processes_.erase(proc_it++);
    }
    else
    {
      proc_it++;
    }
  }
  running_mutex_.unlock();
}

void ProcessManager::loadCb(temoto_2::LoadProcess::Request& req,
                            temoto_2::LoadProcess::Response& res)
{
  // prefix for debugging and info
  std::string prefix = node_name_ + "::" + __func__;

  // Get the service parameters
  const std::string& action = req.action;
  const std::string& package_name = req.package_name;
  const std::string& executable = req.executable;

  // Validate the action command.
  if (std::find(valid_actions.begin(), valid_actions.end(), action) == valid_actions.end())
  {
    ROS_ERROR_NAMED(node_name_,"%s Action '%s' is not supported.", prefix.c_str(), action.c_str());
    return;  // TODO THROW EXCEPTION
  }

  ROS_DEBUG_NAMED(node_name_,"%s adding '%s' '%s' '%s' to loading queue.", prefix.c_str(), action.c_str(),
            package_name.c_str(), executable.c_str());

  temoto_2::LoadProcess srv;
  srv.request = req;
  srv.response = res;

  loading_mutex_.lock();
  loading_processes_.push_back(srv);
  loading_mutex_.unlock();

  // Fill response
  res.rmp.code = 0;
  res.rmp.message = "Command added to loading queue.";
}

void ProcessManager::unloadCb(temoto_2::LoadProcess::Request& req,
                              temoto_2::LoadProcess::Response& res)
{
  std::string prefix = node_name_ + "::" + __func__;
  ROS_DEBUG_NAMED(node_name_,"%s Unload resource requested ...", prefix.c_str());

  // Lookup the requested process by its resource id.
  running_mutex_.lock();
  unloading_mutex_.lock();
  auto proc_it =
      std::find_if(running_processes_.begin(), running_processes_.end(),
                   [&](const std::pair< pid_t, temoto_2::LoadProcess>& p) -> bool { return p.second.request == req; });
  if (proc_it != running_processes_.end())
  {
    unloading_processes_.push_back(proc_it->first);
    res.rmp.code = 0;
    res.rmp.message = "Resource sucessfully unloaded.";
  }
  else
  {
    ROS_ERROR_NAMED(node_name_,"%s Unable to unload reource with pid: %ld. Resource is not running.", prefix.c_str(),
              res.rmp.resource_id);
    // Fill response
    res.rmp.code = rmp::status_codes::FAILED;
    res.rmp.message = "Resource is not running. Unable to unload.";
  }
  running_mutex_.unlock();
  unloading_mutex_.unlock();
}
}  // namespace process_manager
