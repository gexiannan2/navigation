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

#include "navigation_mesh_handle.h"	
#include "navigation/navigation.h"
#include "resmgr/resmgr.h"
//#include "thread/threadguard.h"
#include "math/lmath.h"

namespace KBEngine{	

namespace
{

bool navmeshFileExists(const std::string& path)
{
	FILE* fp = fopen(path.c_str(), "rb");
	if (fp == NULL)
	{
		return false;
	}

	fclose(fp);
	return true;
}

void appendNavmeshPathIfExists(std::vector<std::string>& paths, const std::string& path)
{
	if (!navmeshFileExists(path))
	{
		return;
	}

	for (std::vector<std::string>::const_iterator iter = paths.begin(); iter != paths.end(); ++iter)
	{
		if (*iter == path)
		{
			return;
		}
	}

	paths.push_back(path);
}

std::vector<std::string> collectNavmeshPaths(const std::string& name)
{
	std::vector<std::string> paths;

	// Simple direct paths for local testing.
	appendNavmeshPathIfExists(paths, "res/" + name + ".navmesh");
	appendNavmeshPathIfExists(paths, "D:/navigation/res/" + name + ".navmesh");
	appendNavmeshPathIfExists(paths, "/mnt/d/navigation/res/" + name + ".navmesh");

	// When a repo-local test file is present, use it directly and avoid
	// depending on external KBEngine resource path discovery.
	if (!paths.empty())
	{
		return paths;
	}

	// Keep the original project-style loading rule as a fallback.
	std::string path = "spaces/" + name;
	wchar_t* wpath = strutil::char2wchar(path.c_str());
	std::wstring wspath = wpath;
	free(wpath);

	std::vector<std::wstring> results;
	Resmgr::getSingleton().listPathRes(wspath, L"navmesh", results);

	for (std::vector<std::wstring>::iterator iter = results.begin(); iter != results.end(); ++iter)
	{
		char* cpath = strutil::wchar2char((*iter).c_str());
		appendNavmeshPathIfExists(paths, cpath);
		free(cpath);
	}

	return paths;
}

} // namespace


//-------------------------------------------------------------------------------------
NavMeshHandle::NavMeshHandle():
NavigationHandle()
{
}

//-------------------------------------------------------------------------------------
NavMeshHandle::~NavMeshHandle()
{
	std::vector<dtNavMesh*>::iterator iter = navmesh_layers.begin();
	for(; iter != navmesh_layers.end(); iter++)
		dtFreeNavMesh((*iter));

	std::vector<dtNavMeshQuery*>::iterator iter1 = navmeshQuery_layers.begin();
	for(; iter1 != navmeshQuery_layers.end(); iter1++)
		dtFreeNavMeshQuery((*iter1));
	
	//DEBUG_MSG(fmt::format("NavMeshHandle::~NavMeshHandle(): ({}) is destroyed!\n", name));
}

//-------------------------------------------------------------------------------------
int NavMeshHandle::findStraightPath(int layer, const Position3D& start, const Position3D& end, std::vector<Position3D>& paths)
{
	if(layer >= (int)navmeshQuery_layers.size())
	{
		//ERROR_MSG(fmt::format("NavMeshHandle::findStraightPath: not found layer({})\n",  layer));
		return NAV_ERROR;
	}

	dtNavMeshQuery* navmeshQuery = navmeshQuery_layers[layer];
	// dtNavMesh* 

	float spos[3];
	spos[0] = start.x;
	spos[1] = start.y;
	spos[2] = start.z;

	float epos[3];
	epos[0] = end.x;
	epos[1] = end.y;
	epos[2] = end.z;

	dtQueryFilter filter;
	filter.setIncludeFlags(0xffff);
	filter.setExcludeFlags(0);

	const float extents[3] = {2.f, 4.f, 2.f};

	dtPolyRef startRef = INVALID_NAVMESH_POLYREF;
	dtPolyRef endRef = INVALID_NAVMESH_POLYREF;

	float nearestPt[3];
	navmeshQuery->findNearestPoly(spos, extents, &filter, &startRef, nearestPt);
	navmeshQuery->findNearestPoly(epos, extents, &filter, &endRef, nearestPt);

	if (!startRef || !endRef)
	{
		//ERROR_MSG(fmt::format("NavMeshHandle::findStraightPath({2}): Could not find any nearby poly's ({0}, {1})\n", startRef, endRef, name));
		return NAV_ERROR_NEARESTPOLY;
	}

	dtPolyRef polys[MAX_POLYS];
	int npolys;
	float straightPath[MAX_POLYS * 3];
	unsigned char straightPathFlags[MAX_POLYS];
	dtPolyRef straightPathPolys[MAX_POLYS];
	int nstraightPath;
	int pos = 0;

	navmeshQuery->findPath(startRef, endRef, spos, epos, &filter, polys, &npolys, MAX_POLYS);
	nstraightPath = 0;

	if (npolys)
	{
		float epos1[3];
		dtVcopy(epos1, epos);
				
			if (polys[npolys-1] != endRef)
			{
				bool posOverPoly = false;
				navmeshQuery->closestPointOnPoly(polys[npolys-1], epos, epos1, &posOverPoly);
			}
				
		navmeshQuery->findStraightPath(spos, epos1, polys, npolys, straightPath, straightPathFlags, straightPathPolys,
			&nstraightPath, MAX_POLYS);

		Position3D currpos;
		for(int i = 0; i < nstraightPath * 3; )
		{
			currpos.x = straightPath[i++];
			currpos.y = straightPath[i++];
			currpos.z = straightPath[i++];
			paths.push_back(currpos);
			pos++; 
			
			////DEBUG_MSG(fmt::format("NavMeshHandle::findStraightPath: {}->{}, {}, {}\n", pos, currpos.x, currpos.y, currpos.z));
		}
	}

	return pos;
}

//-------------------------------------------------------------------------------------
int NavMeshHandle::raycast(int layer, const Position3D& start, const Position3D& end, std::vector<Position3D>& hitPointVec)
{
	if(layer >= (int)navmeshQuery_layers.size())
	{
		//ERROR_MSG(fmt::format("NavMeshHandle::findStraightPath: not found layer({})\n",  layer));
		return NAV_ERROR;
	}

	dtNavMeshQuery* navmeshQuery = navmeshQuery_layers[layer];

	float hitPoint[3];

	float spos[3];
	spos[0] = start.x;
	spos[1] = start.y;
	spos[2] = start.z;

	float epos[3];
	epos[0] = end.x;
	epos[1] = end.y;
	epos[2] = end.z;

	dtQueryFilter filter;
	filter.setIncludeFlags(0xffff);
	filter.setExcludeFlags(0);

	const float extents[3] = {2.f, 4.f, 2.f};

	dtPolyRef startRef = INVALID_NAVMESH_POLYREF;

	float nearestPt[3];
	navmeshQuery->findNearestPoly(spos, extents, &filter, &startRef, nearestPt);

	if (!startRef)
	{
		return NAV_ERROR_NEARESTPOLY;
	}

	float t = 0;
	float hitNormal[3];
	memset(hitNormal, 0, sizeof(hitNormal));

	dtPolyRef polys[MAX_POLYS];
	int npolys;

	navmeshQuery->raycast(startRef, spos, epos, &filter, &t, hitNormal, polys, &npolys, MAX_POLYS);

	if (t > 1)
	{
		// no hit
		return NAV_ERROR;
	}
	else
	{
		// Hit
		hitPoint[0] = spos[0] + (epos[0] - spos[0]) * t;
		hitPoint[1] = spos[1] + (epos[1] - spos[1]) * t;
		hitPoint[2] = spos[2] + (epos[2] - spos[2]) * t;
		if (npolys)
		{
			float h = 0;
			navmeshQuery->getPolyHeight(polys[npolys-1], hitPoint, &h);
			hitPoint[1] = h;
		}
	}
	
	float fPara = 1.0;
	if (-0.0001 <hitNormal[1] < 0.0001)
	{
		hitPoint[0] += fPara*hitNormal[0];
		hitPoint[1] += fPara*hitNormal[1];
		hitPoint[2] += fPara*hitNormal[2];
	}
	
	hitPointVec.push_back(Position3D(hitPoint[0], hitPoint[1], hitPoint[2]));
	return 1;
}
int NavMeshHandle::GetNearPos(const Position3D& start, Position3D& end, int layer)
{
	if (layer >= (int)navmeshQuery_layers.size())
	{
		//ERROR_MSG(fmt::format("NavMeshHandle::findStraightPath: not found layer({})\n",  layer));
		return NAV_ERROR;
	}

	dtNavMeshQuery* navmeshQuery = navmeshQuery_layers[layer];

	float hitPoint[3];

	float spos[3];
	spos[0] = start.x;
	spos[1] = start.y;
	spos[2] = start.z;

	dtQueryFilter filter;
	filter.setIncludeFlags(0xffff);
	filter.setExcludeFlags(0);

	//const float extents[3] = { 2.f, 4.f, 2.f };
	const float extents[3] = { 4.f, 8.f, 4.f };

	dtPolyRef startRef = INVALID_NAVMESH_POLYREF;

	float nearestPt[3];
	navmeshQuery->findNearestPoly(spos, extents, &filter, &startRef, nearestPt);
	end.x = nearestPt[0];
	end.y = nearestPt[1];
	end.z = nearestPt[2];
	if (!startRef)
	{
		return NAV_ERROR_NEARESTPOLY;
	}
	return 0;
}
int NavMeshHandle::raycastNear(int layer, const Position3D& start, const Position3D& end, std::vector<Position3D>& hitPointVec)
{
	if (layer >= (int)navmeshQuery_layers.size())
	{
		//ERROR_MSG(fmt::format("NavMeshHandle::findStraightPath: not found layer({})\n",  layer));
		return NAV_ERROR;
	}

	dtNavMeshQuery* navmeshQuery = navmeshQuery_layers[layer];

	float hitPoint[3];

	float spos[3];
	spos[0] = start.x;
	spos[1] = start.y;
	spos[2] = start.z;

	float epos[3];
	epos[0] = end.x;
	epos[1] = end.y;
	epos[2] = end.z;

	dtQueryFilter filter;
	filter.setIncludeFlags(0xffff);
	filter.setExcludeFlags(0);

	//const float extents[3] = { 2.f, 4.f, 2.f };
	const float extents[3] = { 4.f, 8.f, 4.f };

	dtPolyRef startRef = INVALID_NAVMESH_POLYREF;

	float nearestPt[3];
	navmeshQuery->findNearestPoly(spos, extents, &filter, &startRef, nearestPt);

	if (!startRef)
	{
		return NAV_ERROR_NEARESTPOLY;
	}

	float t = 0;
	float hitNormal[3];
	memset(hitNormal, 0, sizeof(hitNormal));

	dtPolyRef polys[MAX_POLYS];
	int npolys;

	navmeshQuery->raycast(startRef, spos, epos, &filter, &t, hitNormal, polys, &npolys, MAX_POLYS);

	if (t > 1)
	{
		// no hit
		return NAV_ERROR;
	}
	else
	{
		// Hit
		hitPoint[0] = spos[0] + (epos[0] - spos[0]) * t;
		hitPoint[1] = spos[1] + (epos[1] - spos[1]) * t;
		hitPoint[2] = spos[2] + (epos[2] - spos[2]) * t;
		if (npolys)
		{
			float h = 0;
			navmeshQuery->getPolyHeight(polys[npolys - 1], hitPoint, &h);
			hitPoint[1] = h;
		}
	}
	float fPara = 1.0;
	if (-0.0001 < hitNormal[1] < 0.0001)
	{
		hitPoint[0] += fPara*hitNormal[0];
		hitPoint[1] += fPara*hitNormal[1];
		hitPoint[2] += fPara*hitNormal[2];
	}

	hitPointVec.push_back(Position3D(hitPoint[0], hitPoint[1], hitPoint[2]));
	return 1;
}

int NavMeshHandle::raycastAlong(int layer, const Position3D& start, const Position3D& end, std::vector<Position3D>& hitPointVec)
{
	if (layer >= (int)navmeshQuery_layers.size())
	{
		//ERROR_MSG(fmt::format("NavMeshHandle::findStraightPath: not found layer({})\n",  layer));
		return NAV_ERROR;
	}

	dtNavMeshQuery* navmeshQuery = navmeshQuery_layers[layer];

	float hitPoint[3];

	float spos[3];
	spos[0] = start.x;
	spos[1] = start.y;
	spos[2] = start.z;

	float epos[3];
	epos[0] = end.x;
	epos[1] = end.y;
	epos[2] = end.z;

	dtQueryFilter filter;
	filter.setIncludeFlags(0xffff);
	filter.setExcludeFlags(0);

	const float extents[3] = { 2.f, 4.f, 2.f };

	dtPolyRef startRef = INVALID_NAVMESH_POLYREF;

	float nearestPt[3];
	navmeshQuery->findNearestPoly(spos, extents, &filter, &startRef, nearestPt);

	if (!startRef)
	{
		return NAV_ERROR_NEARESTPOLY;
	}

	float t = 0;
	float hitNormal[3];
	memset(hitNormal, 0, sizeof(hitNormal));

	dtPolyRef polys[MAX_POLYS];
	int npolys;

	navmeshQuery->raycast(startRef, spos, epos, &filter, &t, hitNormal, polys, &npolys, MAX_POLYS);

	//
	// Move along navmesh and update new position.
	float result[3];
	static const int MAX_VISITED = 16;
	dtPolyRef visited[MAX_VISITED];
	int nvisited = 0;
	navmeshQuery->moveAlongSurface(startRef, spos, epos, &filter,
		result, visited, &nvisited, MAX_VISITED);
	


	//dtVcopy(m_pos, result);
	

	
	hitPointVec.push_back(Position3D(result[0], result[1], result[2]));
	return 1;
}


int NavMeshHandle::GetHeight(const Position3D& start, float *h, int layer)
{
	if (layer >= (int)navmeshQuery_layers.size())
	{
		//ERROR_MSG(fmt::format("NavMeshHandle::findStraightPath: not found layer({})\n",  layer));
		return NAV_ERROR;
	}

	dtNavMeshQuery* navmeshQuery = navmeshQuery_layers[layer];

	float hitPoint[3];

	float spos[3];
	spos[0] = start.x;
	spos[1] = start.y;
	spos[2] = start.z;

	dtQueryFilter filter;
	filter.setIncludeFlags(0xffff);
	filter.setExcludeFlags(0);

	const float extents[3] = { 4.f, 8.f, 4.f };

	dtPolyRef startRef = INVALID_NAVMESH_POLYREF;

	float nearestPt[3];
	navmeshQuery->findNearestPoly(spos, extents, &filter, &startRef, nearestPt);

	if (!startRef)
	{
		return NAV_ERROR_NEARESTPOLY;
	}
	navmeshQuery->getPolyHeight(startRef, spos, h);

	return 1;
}
//
float NavMeshHandle::GetHeight(Position3D pos,int layer)
{
	if (layer >= (int)navmeshQuery_layers.size())
	{
		//ERROR_MSG(fmt::format("NavMeshHandle::findStraightPath: not found layer({})\n",  layer));
		return NAV_ERROR;
	}
	dtNavMeshQuery* navmeshQuery = navmeshQuery_layers[layer];

	float spos[3];
	spos[0] = pos.x;
	spos[1] = pos.y;
	spos[2] = pos.z;
	dtQueryFilter filter;
	filter.setIncludeFlags(0xffff);
	filter.setExcludeFlags(0);

	const float extents[3] = { 4.f, 8.f, 4.f };
	dtPolyRef startRef = 0;
	float nearestPt[3];
	navmeshQuery->findNearestPoly(spos, extents, &filter, &startRef, nearestPt);

	if (!startRef)
	{
		return -2.f;
	}

	float height = 0.f;
	navmeshQuery->getPolyHeight(startRef, spos, &height);
	return height;
}
int NavMeshHandle::Intersects(int layer, const Position3D& start, const Position3D& end, std::vector<Position3D>& hitPointVec)
{
	if (layer >= (int)navmeshQuery_layers.size())
	{
		//ERROR_MSG(fmt::format("NavMeshHandle::findStraightPath: not found layer({})\n",  layer));
		return NAV_ERROR;
	}

	dtNavMeshQuery* navmeshQuery = navmeshQuery_layers[layer];

	float spos[3];
	spos[0] = start.x;
	spos[1] = start.y;
	spos[2] = start.z;
	float fheight = GetHeight(end);
	float fheights = GetHeight(start);
	if (fheight >= 0  && fheight -end.y < 0.5)
	{
		return raycast(layer, start, end, hitPointVec);
	}
	if (fheight <= 0 && end.y < 0 && -fheight + end.y < 0.5)
	{
		return raycast(layer, start, end, hitPointVec);
	}
	if (fheight < end.y )
	{

		return raycast(layer, start,end, hitPointVec);
	}
	

	Position3D rayPos = start;
	Position3D ray = end - start;
	
	float collisionDistance = 0;
	float blockScale = 2;
	Vector3 rayStep = ray.direction() * blockScale * 0.5f;
	Vector3 rayStartPosition = rayPos;

	// Linear search - Loop until find a point inside and outside the terrain 
	Vector3 lastRayPosition = rayPos;
	rayPos += rayStep;
	float height = GetHeight(rayPos);
	while (rayPos.y > height && height >= 0)
	{
		lastRayPosition = rayPos;
		rayPos += rayStep;
		height = GetHeight(rayPos);
	}

	// If the ray collides with the terrain 
	if (height >= 0)
	{
		Vector3 startPosition = lastRayPosition;
		Vector3 endPosition = rayPos;

		// Binary search. Find the exact collision point 
		for (int i = 0; i < 32; i++)
		{
			// Binary search pass 
			Vector3 middlePoint = (startPosition + endPosition) * 0.5f;
			if (middlePoint.y < height)
				endPosition = middlePoint;
			else
				startPosition = middlePoint;
		}
		Vector3 collisionPoint = (startPosition + endPosition) * 0.5f;
		//collisionDistance = Vector3.Distance(rayStartPosition, collisionPoint);
		hitPointVec.push_back(Position3D(collisionPoint.x, collisionPoint.y, collisionPoint.z));
	}
	return collisionDistance;
}
//-------------------------------------------------------------------------------------
NavigationHandle* NavMeshHandle::create(std::string name)
{
	if(name == "")
		return NULL;
	
	NavMeshHandle* pNavMeshHandle = NULL;

	std::vector<std::string> navmeshPaths = collectNavmeshPaths(name);
	if(navmeshPaths.size() == 0)
	{
		return NULL;
	}

	pNavMeshHandle = new NavMeshHandle();
	std::vector<std::string>::iterator iter = navmeshPaths.begin();

	for(; iter != navmeshPaths.end(); iter++)
	{
		const std::string& path = (*iter);
	
		FILE* fp = fopen(path.c_str(), "rb");
		if (!fp)
		{
			/*//ERROR_MSG(fmt::format("NavMeshHandle::create: open({}) is error!\n", Resmgr::getSingleton().matchRes(path)));*/

			break;
		}
		
		//DEBUG_MSG(fmt::format("NavMeshHandle::create: ({}), layer={}\n", 
		//	name, (pNavMeshHandle->navmeshQuery_layers.size())));

		bool safeStorage = true;
		int pos = 0;
		int size = sizeof(NavMeshSetHeader);
		
		fseek(fp, 0, SEEK_END); 
		size_t flen = ftell(fp); 
		fseek(fp, 0, SEEK_SET); 

		uint8* data = new uint8[flen];
		if(data == NULL)
		{
			/*//ERROR_MSG(fmt::format("NavMeshHandle::create: open({}), memory(size={}) error!\n", 
			Resmgr::getSingleton().matchRes(path), flen));*/

			fclose(fp);
			SAFE_RELEASE_ARRAY(data);
			break;
		}

		size_t readsize = fread(data, 1, flen, fp);//first one is len chack len
		if(readsize != flen)
		{
			/*//ERROR_MSG(fmt::format("NavMeshHandle::create: open({}), read(size={} != {}) error!\n", 
			Resmgr::getSingleton().matchRes(path), readsize, flen));*/

			fclose(fp);
			SAFE_RELEASE_ARRAY(data);
			break;
		}

		if (readsize < sizeof(NavMeshSetHeader))
		{
			/*//ERROR_MSG(fmt::format("NavMeshHandle::create: open({}), NavMeshSetHeader is error!\n", 
			Resmgr::getSingleton().matchRes(path)));*/

			fclose(fp);
			SAFE_RELEASE_ARRAY(data);
			break;
		}

		NavMeshSetHeader header;
		memcpy(&header, data, size);

		pos += size;

		if (header.version != NavMeshHandle::RCN_NAVMESH_VERSION)
		{
			/*//ERROR_MSG(fmt::format("NavMeshHandle::create: navmesh version({}) is not match({})!\n", 
			header.version, ((int)NavMeshHandle::RCN_NAVMESH_VERSION)));*/

			fclose(fp);
			SAFE_RELEASE_ARRAY(data);
			break;
		}

		dtNavMesh* mesh = dtAllocNavMesh();
		if (!mesh)
		{
			//ERROR_MSG("NavMeshHandle::create: dtAllocNavMesh is failed!\n");
			fclose(fp);
			SAFE_RELEASE_ARRAY(data);
			break;
		}

		dtStatus status = mesh->init(&header.params);
		if (dtStatusFailed(status))
		{
			//ERROR_MSG(fmt::format("NavMeshHandle::create: mesh init is error({})!\n", status));
			fclose(fp);
			SAFE_RELEASE_ARRAY(data);
			break;
		}

		// Read tiles.
		bool success = true;
		for (int i = 0; i < header.tileCount; ++i)
		{
			NavMeshTileHeader tileHeader;
			size = sizeof(NavMeshTileHeader);
			memcpy(&tileHeader, &data[pos], size);
			pos += size;

			size = tileHeader.dataSize;
			if (!tileHeader.tileRef || !tileHeader.dataSize)
			{
				success = false;
				status = DT_FAILURE + DT_INVALID_PARAM;
				break;
			}
			
			unsigned char* tileData = 
				(unsigned char*)dtAlloc(size, DT_ALLOC_PERM);
			if (!tileData)
			{
				success = false;
				status = DT_FAILURE + DT_OUT_OF_MEMORY;
				break;
			}
			memcpy(tileData, &data[pos], size);
			pos += size;

			status = mesh->addTile(tileData
				, size
				, (safeStorage ? DT_TILE_FREE_DATA : 0)
				, tileHeader.tileRef
				, 0);

			if (dtStatusFailed(status))
			{
				success = false;
				break;
			}
		}

		fclose(fp);
		SAFE_RELEASE_ARRAY(data);

		if (!success)
		{
			//ERROR_MSG(fmt::format("NavMeshHandle::create:  error({})!\n", status));
			dtFreeNavMesh(mesh);
			break;
		}

		pNavMeshHandle->navmesh_layers.push_back(mesh);
		dtNavMeshQuery* pMavmeshQuery = new dtNavMeshQuery();
		pNavMeshHandle->navmeshQuery_layers.push_back(pMavmeshQuery);
		pMavmeshQuery->init(mesh, 8192);
		pNavMeshHandle->name = name;

		uint32 tileCount = 0;
		uint32 nodeCount = 0;
		uint32 polyCount = 0;
		uint32 vertCount = 0;
		uint32 triCount = 0;
		uint32 triVertCount = 0;
		uint32 dataSize = 0;

		const dtNavMesh* navmesh = mesh;
		for (int32 i = 0; i < navmesh->getMaxTiles(); ++i)
		{
			const dtMeshTile* tile = navmesh->getTile(i);
			if (!tile || !tile->header)
				continue;

			tileCount ++;
			nodeCount += tile->header->bvNodeCount;
			polyCount += tile->header->polyCount;
			vertCount += tile->header->vertCount;
			triCount += tile->header->detailTriCount;
			triVertCount += tile->header->detailVertCount;
			dataSize += tile->dataSize;

			// //DEBUG_MSG(fmt::format("NavMeshHandle::create: verts({}, {}, {})\n", tile->verts[0], tile->verts[1], tile->verts[2]));
		}

		//DEBUG_MSG(fmt::format("\t==> tiles loaded: {}\n", tileCount));
		//DEBUG_MSG(fmt::format("\t==> BVTree nodes: {}\n", nodeCount));
		//DEBUG_MSG(fmt::format("\t==> {} polygons ({} vertices)\n", polyCount, vertCount));
		//DEBUG_MSG(fmt::format("\t==> {} triangles ({} vertices)\n", triCount, triVertCount));
		//DEBUG_MSG(fmt::format("\t==> {:.2f} MB of data (not including pointers)\n", (((float)dataSize / sizeof(unsigned char)) / 1048576)));
	}

	return pNavMeshHandle;
}

//-------------------------------------------------------------------------------------
}
