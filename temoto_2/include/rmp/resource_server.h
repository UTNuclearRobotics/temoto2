#ifndef RESOURCE_SERVER_H
#define RESOURCE_SERVER_H
#include "ros/ros.h"
#include "common/temoto_id.h"
#include "rmp/base_resource_server.h"
#include "rmp/resource_query.h"

namespace rmp
{
template <class ServiceType, class Owner>
class ResourceServer : public BaseResourceServer<Owner>
{
public:
  typedef void (Owner::*LoadCbFuncType)(typename ServiceType::Request&,
                                        typename ServiceType::Response&);
  typedef void (Owner::*UnloadCbFuncType)(typename ServiceType::Request&,
                                          typename ServiceType::Response&);

  ResourceServer(std::string name, LoadCbFuncType load_cb, UnloadCbFuncType unload_cb, Owner* owner,
                 ResourceManager<Owner>& resource_manager)
    : BaseResourceServer<Owner>(name, resource_manager)
    , load_callback_(load_cb)
    , unload_callback_(unload_cb)
    , owner_(owner)
  {
    std::string rm_name = this->resource_manager_.getName();
    load_server_ =
        nh_.advertiseService(rm_name + "/" + this->name_,
                             &ResourceServer<ServiceType, Owner>::wrappedLoadCallback, this);
    ROS_INFO("ResourceServer constructed, listening on %s",
             this->load_server_.getService().c_str());
  }

  ~ResourceServer()
  {
    ROS_INFO("ResourceServer[%s] destroyed.", this->name_.c_str());
  }

  void registerInternalClient(std::string client_name, temoto_id::ID resource_id)
  {
    ROS_INFO("[ResourceServer::registerInternalClient] [%s] id %d", this->name_.c_str(),
             resource_id);
    if (!queries_.size())
    {
      ROS_ERROR("registerInternalClient called, but queries_ is empty");
      return;
    }
    queries_.back().addInternalClient(client_name, resource_id);
  }

  bool wrappedLoadCallback(typename ServiceType::Request& req, typename ServiceType::Response& res)
  {
    std::string prefix = "ResourceServer::wrappedLoadCallback() [" + this->name_ + "]:";
    ROS_INFO("%s Got query. Will send status to: '%s'", prefix.c_str(),
             req.rmp.status_topic.c_str());

    if (!owner_)
    {
      ROS_ERROR("%s ResourceServer Owner is NULL. Query aborted.", prefix.c_str());
      return true;
    }

    // generate new external id for the resource
    temoto_id::ID ext_resource_id = res_id_manager_.generateID();

    ROS_INFO("%s Generated external id:%d", prefix.c_str(), ext_resource_id);

    // New or existing query? Check it out with this hi-tec lambda function :)
    auto found_query = std::find_if(queries_.begin(), queries_.end(),
                                    [&req](const ResourceQuery<ServiceType>& query) -> bool {
                                      return query.getMsg().request == req;
                                    });

    if (found_query == queries_.end())
    {
      // generate new internal id, and give it to owners callback.
      // with this owner can send status messages later when necessary
      temoto_id::ID int_resource_id = this->resource_manager_.generateInternalID();
      res.rmp.resource_id = int_resource_id;
      ROS_INFO("%s New query, internal id:%d", prefix.c_str(), int_resource_id);

      // equal message not found from queries_, add new query
      queries_.emplace_back(req);
      queries_.back().addExternalClient(ext_resource_id, req.rmp.status_topic);

      // register owner as a nonamed client
      std::string client_name = "";
      queries_.back().addInternalClient(client_name, int_resource_id);

      // set this server active in resource manager
      // when a client call is made from callback, the binding between active server
      // and the new loaded clients can be made automatically
      this->activateServer();

      // call owner's registered callback
      (owner_->*load_callback_)(req, res);

      ROS_INFO("%s Resumed from owners callback, returning response from last query",
               prefix.c_str());

      // update the query with the response message filled in the callback
      queries_.back().setMsgResponse(res);

      // restore active server to NULL in resource manager
      this->deactivateServer();
    }
    else
    {
      // found equal request, simply reqister this in the query
      // and respond with unique resoure_id.
      ROS_INFO("%s Existing query, linking to the found query.", prefix.c_str());
      queries_.back().addExternalClient(ext_resource_id, req.rmp.status_topic);
    }

    ROS_INFO("%s Returning true.", prefix.c_str());
    return true;
  }

  // This function is called from resource manager when /unload request arrives
  // (e.g. when some external client is being destroyed)
  // We look up the query that contains given external resource id and send unload to all internal
  // clients in the same query

  void unloadResource(temoto_2::UnloadResource::Request& req,
                      temoto_2::UnloadResource::Response& res)
  {
    std::string prefix = "[ResourceServer::unloadResource] [" + this->name_ + "]:";
    ROS_INFO_STREAM(prefix);
    // find first query that contains resource that should be unloaded
    const temoto_id::ID resource_id = req.resource_id;
    const auto found_query_it =
        std::find_if(queries_.begin(), queries_.end(),
                     [resource_id](const ResourceQuery<ServiceType>& query) -> bool {
                     ROS_INFO_STREAM(query.getMsg().request);
                     ROS_INFO_STREAM(query.getMsg().response);
                     ROS_INFO("%d", query.externalClientExists(resource_id));
                       return query.externalClientExists(resource_id);
                     });
    if (found_query_it != queries_.end())
    {
      ROS_INFO("%s Query with ext id %d was found", prefix.c_str(), resource_id);
      ROS_INFO("%s internal client count: %lu",prefix.c_str(), 
               found_query_it->getInternalClients().size());
      // Query found, try to remove client from it.
      size_t clients_remaining = found_query_it->removeExternalClient(req.resource_id);
      ROS_INFO("%s internal clients remaining %lu", prefix.c_str(),
               clients_remaining);
      if (clients_remaining <= 0)
      {
        // last resource removed, execute owner's unload callback and remove the query from our list
        typename ServiceType::Request orig_req = found_query_it->getMsg().request;
        typename ServiceType::Response orig_res = found_query_it->getMsg().response;
        (owner_->*unload_callback_)(orig_req, orig_res);

        /// TODO: Do or do not something with the response part?

        // Send unload command to all internal clients...
        ROS_INFO("%s internal clients %lu", prefix.c_str(),
                 found_query_it->getInternalClients().size());
        for (auto& map_el : found_query_it->getInternalClients())
        {
          //
          if (map_el.first == "")
          {
            // do not unload owner's resources automatically. 
            // resources that were loaded outside of load callback are not RMP responsibility
            continue;  
          }
          for (auto& set_el : map_el.second)
          {
            this->resource_manager_.unloadClientResource(set_el);
          }
        }

        // Finally, remove the found query.
        queries_.erase(found_query_it);
      }
    }
  }

  bool internalResourceExists(temoto_id::ID resource_id) const
  {
    auto found_q =
        find_if(queries_.begin(), queries_.end(), [&](const ResourceQuery<ServiceType>& q) -> bool {
          return q.internalResourceExists(resource_id);
        });
    return found_q != queries_.end();
  }

  // checks which query contains connection with resource_id given in msg
  // overwrite resource id to external id and send the msg out to status server.
  void notifyClients(temoto_2::RMPStatus& status_msg)
  {
    ROS_INFO("ResourceServer::NotifyClients()[%s]",this->name_.c_str());
    auto q_it = std::find_if(queries_.begin(), queries_.end(),
                             [&](const ResourceQuery<ServiceType>& q) -> bool {
                               return q.internalResourceExists(status_msg.resource_id);
                             });

    if (q_it != queries_.end())
    {
      const auto ext_clients = q_it->getExternalClients();
      for (const auto ext_client : ext_clients)
      {
        ros::Publisher pub =
            nh_.advertise<temoto_2::RMPStatus>(ext_client.second,1000);
        ROS_INFO("SENDING ResourceStatus to %s", ext_client.second.c_str());
        status_msg.resource_id = ext_client.first;
        status_msg.server_name = this->name_;
        status_msg.manager_name = this->resource_manager_.getName();
        if(pub)
        {
          int i=0;
          while(true)
          {
            ROS_INFO("publishing %d", i++);
            pub.publish(status_msg);
            ros::spinOnce();  // send status immediately
            ros::Duration(5).sleep();
          }
        }
      }
    }
  }

private:
  Owner* owner_;
  LoadCbFuncType load_callback_;
  UnloadCbFuncType unload_callback_;

  ros::ServiceServer load_server_;
  ros::NodeHandle nh_;
  temoto_id::IDManager res_id_manager_;

  std::vector<ResourceQuery<ServiceType>> queries_;
};
}

#endif
