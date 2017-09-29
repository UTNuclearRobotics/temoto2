#ifndef BASE_RESOURCE_SERVER_H
#define BASE_RESOURCE_SERVER_H

//#include "rmp/resource_entry.h"
#include <string>
#include "common/temoto_id.h"
#include "rmp/resource_manager.h"

namespace rmp
{

//Forward declatation of ResourceManager
template<class Owner>
class ResourceManager;

template<class Owner>
class BaseResourceServer
{
	public:
//		BaseResourceServer()
//		{
//			name_ = "default_server_name";
//			resource_manager_ = NULL;
//		}

		BaseResourceServer(const std::string name, ResourceManager<Owner>& resource_manager) :
			name_(name),
			resource_manager_(resource_manager)
		{
		}
		virtual ~BaseResourceServer()
		{
		}
		//template <class Service>
		//bool addResourceEntry(ResourceEntry<Service> resource)=0;

		const std::string& getName()
		{
			return name_;
		};

		virtual void registerInternalClient(std::string client_name, temoto_id::ID resource_id){};

	protected:

		bool activateServer()
		{
			resource_manager_.setActiveServer(this);
		};

		bool deactivateServer()
		{
			resource_manager_.setActiveServer(NULL);
		};

		ResourceManager<Owner>& resource_manager_;
		std::string name_;


	private:
};

}



#endif
