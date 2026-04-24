/*
This source file is part of KBEngine
For the latest info, see http://www.kbengine.org/

Copyright (c) 2008-2016 KBEngine.

KBEngine is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

KBEngine is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.
 
You should have received a copy of the GNU Lesser General Public License
along with KBEngine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "navigation.h"
#include "resmgr/resmgr.h"
//#include "thread/threadguard.h"

#include "navigation_mesh_handle.h"

namespace KBEngine{

KBE_SINGLETON_INIT(Navigation);

//-------------------------------------------------------------------------------------
Navigation::Navigation()
{
	navhandles_.clear();
}

//-------------------------------------------------------------------------------------
Navigation::~Navigation()
{
	navhandles_.clear();
}

//-------------------------------------------------------------------------------------
bool Navigation::removeNavigation(std::string name)
{
//	KBEngine::thread::ThreadGuard tg(&mutex_); 
	KBEUnordered_map<std::string, NavigationHandlePtr>::iterator iter = navhandles_.find(name);
	if(navhandles_.find(name) != navhandles_.end())
	{
		//iter->second->decRef();
		navhandles_.erase(iter);

		//DEBUG_MSG(fmt::format("Navigation::removeNavigation: ({}) is destroyed!\n", name));
		return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------
NavigationHandlePtr Navigation::findNavigation(std::string name)
{
//	KBEngine::thread::ThreadGuard tg(&mutex_); 
	KBEUnordered_map<std::string, NavigationHandlePtr>::iterator iter = navhandles_.find(name);
	if(navhandles_.find(name) != navhandles_.end())
	{
		if(iter->second->type() == NavigationHandle::NAV_MESH)
		{
			return iter->second;
		}
		return iter->second;
	}

	return NULL;
}

//-------------------------------------------------------------------------------------
bool Navigation::hasNavigation(std::string name)
{
//	KBEngine::thread::ThreadGuard tg(&mutex_); 
	return navhandles_.find(name) != navhandles_.end();
}

//-------------------------------------------------------------------------------------
NavigationHandlePtr Navigation::loadNavigation(std::string name)
{
//	KBEngine::thread::ThreadGuard tg(&mutex_); 
	if(name == "")
	{
		return nullptr;		
	}

	KBEUnordered_map<std::string, NavigationHandlePtr>::iterator iter = navhandles_.find(name);
	if(iter != navhandles_.end())
	{
		return iter->second;
	}

	NavigationHandle* pNavigationHandle_ = NULL;
	pNavigationHandle_ = NavMeshHandle::create(name);

	if(pNavigationHandle_ == NULL)
		return NULL;

	navhandles_[name] = NavigationHandlePtr(pNavigationHandle_);
	return pNavigationHandle_;
}

//-------------------------------------------------------------------------------------		
}
