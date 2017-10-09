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

//template <class Owner>
//class ResourceManager;

template<class ServiceType, class Owner>
	class ResourceClient : public BaseResourceClient<Owner>
{
	public:
		ResourceClient(
				std::string ext_resource_manager_name,
				std::string ext_server_name,
				Owner* owner,
				ResourceManager<Owner>& resource_manager)
			: 
                BaseResourceClient<Owner>(resource_manager),
				ext_resource_manager_name_(ext_resource_manager_name),
				ext_server_name_(ext_server_name), 
				owner_(owner)
		{
			
			// Set client name according to the server, where it is connecting to.
			name_ = ext_resource_manager_name + "/" + ext_server_name;
            std::string unload_name = ext_resource_manager_name+"/unload";
            
			// Setup ROS service clients for loading and unloading the resource
			service_client_ = nh_.serviceClient<ServiceType>(name_);
			service_client_unload_ = nh_.serviceClient<temoto_2::UnloadResource>(unload_name);
			ROS_INFO("Created ResourceClient %s", name_.c_str());
		}

		~ResourceClient()
		{
			ROS_INFO("Destroyed ResourceClient %s", name_.c_str());
		}


		bool call(ServiceType& msg)
		{
			std::string prefix = "ResourceClient::call() [" + name_ + "]:";
			ROS_INFO("%s", prefix.c_str());

			// search for given service request from previous queries 
			auto q_it = std::find_if(queries_.begin(), queries_.end(), 
					[&](const ClientQuery<ServiceType>& q) -> bool 
                    {return q.getMsg().request == msg.request;}
                    ); 
			if(q_it == queries_.end())
			{
				// New request
                // Prepare return topic
                std::string topic = this->resource_manager_.getName()+"/status";
                msg.request.rmp.status_topic = topic;
				ROS_INFO("%s New query, performing external call to %s", prefix.c_str(), service_client_.getService().c_str());
				if(service_client_.call(msg))
				{
					ROS_INFO("%s Service call was sucessful.", prefix.c_str()); 
					queries_.push_back(msg);
					q_it  = std::prev(queries_.end()); // set iterator to the added query 
				}
				else
				{
					ROS_ERROR("%s Service call returned false.", prefix.c_str()); 
					return false; // something went wrong, return immediately
				}
			}
			else
			{
				// Equal request was found from stored list
				ROS_INFO("%s Existing request, using stored response", prefix.c_str());

				// Fill the response part with the one that was found from queries_
				msg.response = q_it->getMsg().response;
			}

            // Generate id for the resource and add it to the query.
            // Also determine the caller's name, which is server name, when the
            // call came from some server callback, or empty string when it did not.
            
            temoto_id::ID generated_id = id_manager_.generateID();
            std::string server_name = this->resource_manager_.getActiveServerName();
            q_it->addInternalResource(generated_id, server_name);

            // Update id in response part
            msg.response.rmp.resource_id = generated_id;

			return true; 
		}

        
        void removeResource(temoto_id::ID resource_id)
        {
			// search for given resource id to unload 
			auto q_it = std::find_if(queries_.begin(), queries_.end(), 
					[&](const ClientQuery<ServiceType>& q) -> bool {return q.internalResourceExists(resource_id);}
                    ); 
            if(q_it != queries_.end())
            {
				size_t caller_cnt = q_it->removeInternalResource(resource_id);
				if(caller_cnt <= 0)
				{
					queries_.erase(q_it);
				}
            }
        }


        /// Remove all queries and send unload requests to external servers
        void unloadResources()
        {
            ROS_INFO("[ResourceClient::unloadResources] [%s] queries:%lu", name_.c_str(), queries_.size());
            for(auto& q : queries_)
            {
                sendUnloadRequest(q.getExternalId());
            }
            queries_.clear();
        }
            

        void unloadResource(temoto_id::ID resource_id)
        {
			// search for given resource id to unload 
			auto q_it = std::find_if(queries_.begin(), queries_.end(), 
					[&](const ClientQuery<ServiceType>& q) -> bool {return q.internalResourceExists(resource_id);}
                    ); 
            if(q_it != queries_.end())
            {
				size_t caller_cnt = q_it->removeInternalResource(resource_id);
				if(caller_cnt <= 0)
				{
					sendUnloadRequest(q_it->getExternalId());
					queries_.erase(q_it);
				}
            }
        }
        

        /// Send unload service request to the server 
        void sendUnloadRequest(temoto_id::ID ext_resource_id){
            temoto_2::UnloadResource unload_msg;
            unload_msg.request.server_name = ext_server_name_;
            unload_msg.request.resource_id = ext_resource_id;
            ROS_INFO("[ResourceClient::sendUnloadRequest] sending unload to %s",
                        service_client_unload_.getService().c_str());
            if(!service_client_unload_.call(unload_msg))
            {
                ROS_ERROR("[ResourceClient::sendUnloadRequest] Call to %s failed.",
                        service_client_unload_.getService().c_str());
            }
        }


        /// Returns internal resources of all queries in this client.
        std::map<temoto_id::ID, std::string> getInternalResources() const
        {
            std::map<temoto_id::ID, std::string> ret;
            for(auto& q : queries_){
                std::map<temoto_id::ID, std::string> r = q.getInternalResources();
                ret.insert(r.begin(), r.end());
            }

            return ret;
        }

        /// Search for the query that matches given external resource_id,
        // and return all internal connections that this query has.
        const std::map<temoto_id::ID, std::string> getInternalResources(temoto_id::ID ext_resource_id) const
        {
            const auto q_it = std::find_if(queries_.begin(), queries_.end(), 
                    [&](const ClientQuery<ServiceType>& q) -> bool {return q.getMsg().response.rmp.resource_id == ext_resource_id;}
                    );
            if(q_it != queries_.end())
            {
                return q_it->getInternalResources();
            }

            std::string err_str = "[ResourceClient::getInternalResources] No query with given resource_id was not found!"; 
            throw (err_str);
        }


        size_t getQueryCount() const {return queries_.size();}

        const std::string& getName() const {return name_;} 
        
        const std::string& getExtServerName() const {return ext_server_name_;} 


	private:

		std::string name_; /// The unique name of a resource client.
		std::string ext_server_name_; /// The name of the server client calls.
		std::string ext_resource_manager_name_; /// Name of resource manager where the server is located at.
		std::vector<ClientQuery<ServiceType>> queries_;
		Owner* owner_;
		temoto_id::IDManager id_manager_;
		
		ros::ServiceClient service_client_;
		ros::ServiceClient service_client_unload_;
		ros::NodeHandle nh_;

};

}

#endif
