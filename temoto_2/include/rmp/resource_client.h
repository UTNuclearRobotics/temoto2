#ifndef RESOURCE_CLIENT_H
#define RESOURCE_CLIENT_H
#include "ros/ros.h"
#include "common/temoto_id.h"
#include "rmp/base_resource_client.h"
#include "rmp/client_query.h"
#include <string>
#include <map>

namespace rmp
{
// template <class Owner>
// class ResourceManager;

template <class ServiceType, class Owner>
class ResourceClient : public BaseResourceClient<Owner>
{
public:
  ResourceClient(std::string ext_temoto_namespace, std::string ext_resource_manager_name,
                 std::string ext_server_name, Owner* owner,
                 ResourceManager<Owner>& resource_manager)
    : BaseResourceClient<Owner>(resource_manager)
    , ext_resource_manager_name_(ext_resource_manager_name)
    , ext_server_name_(ext_server_name)
    , ext_temoto_namespace_(ext_temoto_namespace)
    , owner_(owner)
  {
    this->class_name_ = __func__;

    // Set client name where it is connecting to.
    name_ = '/' + ext_temoto_namespace + '/' + ext_resource_manager_name + "/" + ext_server_name;
    std::string load_srv_name = name_;
    std::string unload_name = '/' + ext_temoto_namespace + '/' + ext_resource_manager_name + "/unload";

    // Setup ROS service clients for loading and unloading the resource
    service_client_ = nh_.serviceClient<ServiceType>(load_srv_name);
    service_client_unload_ = nh_.serviceClient<temoto_2::UnloadResource>(unload_name);
    TEMOTO_DEBUG("Created ResourceClient %s", name_.c_str());
  }

  ~ResourceClient()
  {
    TEMOTO_DEBUG("Destroyed ResourceClient %s", name_.c_str());
  }

  bool call(ServiceType& msg, FailureBehavior failure_behavior)
  {
    // store the internal id that is generated by resource manager
    // this client's internal id is automatically added to corresponding server query, when the 
    // client was called due to server callback. Otherwise it is owners responsibility to store it. 
    temoto_id::ID internal_resource_id = msg.response.rmp.resource_id;

    // search for given service request from previous queries
    auto q_it = std::find_if(queries_.begin(), queries_.end(),
                             [&](const ClientQuery<ServiceType, Owner>& q) -> bool {
                               return q.getMsg().request == msg.request && !q.failed_;
                             });
    if (q_it == queries_.end())
    {
      // New request
      // Prepare return topic
      std::string temoto_namespace = common::getTemotoNamespace();
      std::string topic = '/' + temoto_namespace + '/' + this->resource_manager_.getName() + "/status";
      msg.request.rmp.status_topic = topic;
      msg.request.rmp.temoto_namespace = temoto_namespace;

      TEMOTO_DEBUG("New query, performing external call to %s", service_client_.getService().c_str());

      if (service_client_.call(msg))
      {
        if (msg.response.rmp.code == status_codes::FAILED)
        {
          throw FORWARD_ERROR(msg.response.rmp.error_stack);
        }
        TEMOTO_DEBUG("Service call was sucessful. ext id: %ld", msg.response.rmp.resource_id);
        queries_.emplace_back(msg, owner_);
        q_it = std::prev(queries_.end());  // set iterator to the added query
      }
      else
      {
        throw CREATE_ERROR(error::Code::RMP_FAIL, "Service call to %s returned false.",
                           service_client_.getService().c_str());
      }
    }
    else
    {
      // Equal request was found from stored list
      TEMOTO_DEBUG("Existing request, using stored response");

      // Fill the response part with the already existing one from previous call.
      msg.response = q_it->getMsg().response;
    }

    // Update id in response part
    msg.response.rmp.resource_id = internal_resource_id;

    q_it->addInternalResource(internal_resource_id, failure_behavior);

    return true;
  }

  void removeResource(temoto_id::ID resource_id)
  {
    // search for given resource id to unload
    auto q_it = std::find_if(queries_.begin(), queries_.end(),
                             [&](const ClientQuery<ServiceType, Owner>& q) -> bool {
                               return q.internalResourceExists(resource_id);
                             });
    if (q_it != queries_.end())
    {
      size_t caller_cnt = q_it->removeInternalResource(resource_id);
      if (caller_cnt <= 0)
      {
        queries_.erase(q_it);
      }
    }
  }

  /// Remove all queries and send unload requests to external servers
  void unloadResources()
  {
    TEMOTO_DEBUG("ResourceClient %s is unloading %lu queries.", name_.c_str(), queries_.size());
    for (auto& q : queries_)
    {
      sendUnloadRequest(q.getExternalId());
    }
    queries_.clear();
  }

  // unload by internal resource_id
  void unloadResource(temoto_id::ID resource_id)
  {
    TEMOTO_DEBUG("Unloading resource with id:%d", resource_id);

    // search for given resource id to unload
    auto q_it = std::find_if(queries_.begin(), queries_.end(),
                             [&](const ClientQuery<ServiceType, Owner>& q) -> bool {
                               return q.internalResourceExists(resource_id);
                             });
    if (q_it == queries_.end())
    {
      throw CREATE_ERROR(error::Code::RMP_FAIL, "Unable to unload. Resource '%ld' not found.",
                         resource_id);
    }

    try
    {
      // Remove the found query
      q_it->removeInternalResource(resource_id); 

      // Send out unload request, when the last resource in this query was removed.
      if (q_it->getInternalResources().size() <= 0)
      {
        sendUnloadRequest(q_it->getExternalId());
        queries_.erase(q_it);
      }
    }
    catch (error::ErrorStack& error_stack)
    {
      throw FORWARD_ERROR(error_stack);
    }
  }

  //TODO: Consider creating additional states to a resource
  void setFailedFlag(temoto_id::ID external_resource_id)
  {
    auto q_it = std::find_if(queries_.begin(), queries_.end(),
                             [&](const ClientQuery<ServiceType, Owner>& q) -> bool {
                               return q.getExternalId() == external_resource_id;
                             });
    if (q_it != queries_.end())
    {
      q_it->failed_ = true;
    }
  }

  bool hasFailed(temoto_id::ID internal_resource_id)
  {
    auto q_it = std::find_if(queries_.begin(), queries_.end(),
                             [&](const ClientQuery<ServiceType, Owner>& q) -> bool {
                               return q.internalResourceExists(internal_resource_id);
                             });
    if (q_it != queries_.end())
    {
      return q_it->failed_;
    }
    // Return false if resource was not found from this client
    return false; 
  }

  /// Send unload service request to the server
  void sendUnloadRequest(temoto_id::ID ext_resource_id)
  {
    temoto_2::UnloadResource unload_msg;
    unload_msg.request.server_name = ext_server_name_;
    unload_msg.request.resource_id = ext_resource_id;
    TEMOTO_DEBUG("Sending unload to %s",
             service_client_unload_.getService().c_str());
    if (!service_client_unload_.call(unload_msg))
    {
      throw CREATE_ERROR(error::Code::RMP_FAIL, "Call to %s failed.", service_client_unload_.getService().c_str());
    }
  }

  /// Returns internal resources of all queries in this client.
  std::map<temoto_id::ID, FailureBehavior> getInternalResources() const
  {
    std::map<temoto_id::ID, FailureBehavior> ret;
    for (auto& q : queries_)
    {
      std::map<temoto_id::ID, FailureBehavior> r = q.getInternalResources();
      ret.insert(r.begin(), r.end());
    }
    return ret;
  }

  /// Search for the query that matches given external resource_id,
  // and return all internal connections that this query has.
  const std::map<temoto_id::ID, FailureBehavior>
  getInternalResources(temoto_id::ID ext_resource_id)
  {
    std::map<temoto_id::ID, FailureBehavior> internal_resources;
    const auto q_it = std::find_if(queries_.begin(), queries_.end(),
                                   [&](const ClientQuery<ServiceType, Owner>& q) -> bool {
                                     return q.getMsg().response.rmp.resource_id == ext_resource_id;
                                   });
    if (q_it != queries_.end())
    {
      internal_resources = q_it->getInternalResources();
    }
    else
    {
      throw CREATE_ERROR(error::Code::RMP_FAIL, "Resource_id '%ld' was not found!",
                         ext_resource_id);
    }

    return internal_resources;
  }

  bool internalResourceExists(temoto_id::ID resource_id)
  {
    auto q_it = std::find_if(queries_.begin(), queries_.end(),
                             [&](const ClientQuery<ServiceType, Owner>& q) -> bool {
                               return q.internalResourceExists(resource_id);
                             });
    return q_it != queries_.end();
  }

  size_t getQueryCount() const
  {
    return queries_.size();
  }

  const std::string& getName() const
  {
    return name_;
  }

  const std::string& getExtServerName() const
  {
    return ext_server_name_;
  }

  std::string toString()
  {
    std::string ret;
    ret += " Client name: " + name_ + "\n";
    ret += " Ext server name: " + ext_server_name_ + "\n";
    ret += " Ext manager name: " + ext_resource_manager_name_ + "\n";
    for(auto& q : queries_)
    {
      ret += q.toString() + "\n";
    }
    return ret;
  }

private:
  std::string name_;                       ///< The unique name of a resource client.
  std::string ext_server_name_;            ///< The name of the server client calls.
  std::string ext_resource_manager_name_;  ///< Name of resource manager where the server is located
  std::string ext_temoto_namespace_;       ///< Name of the destination temoto namespace.
  std::vector<ClientQuery<ServiceType, Owner>> queries_;  ///< All the resource queries called from
                                                          /// this resource manager are stored here.

  Owner* owner_;

  ros::ServiceClient service_client_;
  ros::ServiceClient service_client_unload_;
  ros::NodeHandle nh_;
};
}

#endif
