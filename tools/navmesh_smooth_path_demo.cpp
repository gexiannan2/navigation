#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

namespace
{

struct NavMeshSetHeader
{
	int version;
	int tileCount;
	dtNavMeshParams params;
};

struct NavMeshTileHeader
{
	dtTileRef tileRef;
	int dataSize;
};

struct LoadedNavMesh
{
	dtNavMesh* navMesh;
	dtNavMeshQuery* navQuery;

	LoadedNavMesh() : navMesh(NULL), navQuery(NULL) {}
	~LoadedNavMesh()
	{
		if (navQuery) dtFreeNavMeshQuery(navQuery);
		if (navMesh) dtFreeNavMesh(navMesh);
	}
};

template <typename T>
T readStruct(const unsigned char* data, size_t size, size_t offset, const char* what)
{
	if (offset + sizeof(T) > size)
	{
		std::ostringstream oss;
		oss << "unexpected EOF while reading " << what;
		throw std::runtime_error(oss.str());
	}
	T value;
	std::memcpy(&value, data + offset, sizeof(T));
	return value;
}

std::vector<unsigned char> readFile(const std::string& path)
{
	std::ifstream input(path.c_str(), std::ios::binary);
	if (!input)
	{
		std::ostringstream oss;
		oss << "failed to open '" << path << "': " << std::strerror(errno);
		throw std::runtime_error(oss.str());
	}
	input.seekg(0, std::ios::end);
	const std::streamoff fileSize = input.tellg();
	input.seekg(0, std::ios::beg);
	std::vector<unsigned char> data(static_cast<size_t>(fileSize));
	if (!data.empty())
	{
		input.read(reinterpret_cast<char*>(&data[0]), fileSize);
	}
	return data;
}

LoadedNavMesh loadWrappedNavMesh(const std::string& path)
{
	const std::vector<unsigned char> data = readFile(path);
	size_t offset = 0;
	const NavMeshSetHeader header =
		readStruct<NavMeshSetHeader>(&data[0], data.size(), offset, "NavMeshSetHeader");
	offset += sizeof(NavMeshSetHeader);
	if (header.version != 1)
	{
		throw std::runtime_error("unsupported navmesh wrapper version");
	}

	LoadedNavMesh loaded;
	loaded.navMesh = dtAllocNavMesh();
	if (!loaded.navMesh || dtStatusFailed(loaded.navMesh->init(&header.params)))
	{
		throw std::runtime_error("dtNavMesh init failed");
	}

	for (int i = 0; i < header.tileCount; ++i)
	{
		const NavMeshTileHeader tile =
			readStruct<NavMeshTileHeader>(&data[0], data.size(), offset, "NavMeshTileHeader");
		offset += sizeof(NavMeshTileHeader);
		unsigned char* tileData = static_cast<unsigned char*>(dtAlloc(tile.dataSize, DT_ALLOC_PERM));
		std::memcpy(tileData, &data[offset], tile.dataSize);
		offset += static_cast<size_t>(tile.dataSize);
		if (dtStatusFailed(loaded.navMesh->addTile(
			tileData, tile.dataSize, DT_TILE_FREE_DATA, tile.tileRef, NULL)))
		{
			dtFree(tileData);
			throw std::runtime_error("addTile failed");
		}
	}

	loaded.navQuery = dtAllocNavMeshQuery();
	if (!loaded.navQuery || dtStatusFailed(loaded.navQuery->init(loaded.navMesh, 8192)))
	{
		throw std::runtime_error("dtNavMeshQuery init failed");
	}

	return loaded;
}

bool inRange(const float* v1, const float* v2, float r, float h)
{
	const float dx = v2[0] - v1[0];
	const float dy = v2[1] - v1[1];
	const float dz = v2[2] - v1[2];
	return dx * dx + dz * dz < r * r && std::fabs(dy) < h;
}

int fixupCorridor(dtPolyRef* path, const int npath, const int maxPath,
	const dtPolyRef* visited, const int nvisited)
{
	int furthestPath = -1;
	int furthestVisited = -1;

	for (int i = npath - 1; i >= 0; --i)
	{
		bool found = false;
		for (int j = nvisited - 1; j >= 0; --j)
		{
			if (path[i] == visited[j])
			{
				furthestPath = i;
				furthestVisited = j;
				found = true;
			}
		}
		if (found)
		{
			break;
		}
	}

	if (furthestPath == -1 || furthestVisited == -1)
	{
		return npath;
	}

	const int req = nvisited - furthestVisited;
	const int orig = std::min(furthestPath + 1, npath);
	int size = std::max(0, npath - orig);
	if (req + size > maxPath)
	{
		size = maxPath - req;
	}
	if (size)
	{
		std::memmove(path + req, path + orig, sizeof(dtPolyRef) * size);
	}
	for (int i = 0; i < req; ++i)
	{
		path[i] = visited[(nvisited - 1) - i];
	}
	return req + size;
}

bool getSteerTarget(dtNavMeshQuery* navQuery, const float* startPos, const float* endPos,
	const float minTargetDist, const dtPolyRef* path, const int pathSize,
	float* steerPos, unsigned char& steerPosFlag, dtPolyRef& steerPosRef)
{
	static const int MAX_STEER_POINTS = 16;
	float steerPath[MAX_STEER_POINTS * 3];
	unsigned char steerPathFlags[MAX_STEER_POINTS];
	dtPolyRef steerPathPolys[MAX_STEER_POINTS];
	int nsteerPath = 0;

	navQuery->findStraightPath(startPos, endPos, path, pathSize,
		steerPath, steerPathFlags, steerPathPolys, &nsteerPath, MAX_STEER_POINTS);

	if (!nsteerPath)
	{
		return false;
	}

	int ns = 0;
	while (ns < nsteerPath)
	{
		if ((steerPathFlags[ns] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ||
			!inRange(&steerPath[ns * 3], startPos, minTargetDist, 1000.0f))
		{
			break;
		}
		++ns;
	}
	if (ns >= nsteerPath)
	{
		return false;
	}

	dtVcopy(steerPos, &steerPath[ns * 3]);
	steerPos[1] = startPos[1];
	steerPosFlag = steerPathFlags[ns];
	steerPosRef = steerPathPolys[ns];
	return true;
}

void printUsage(const char* argv0)
{
	std::cout << "Usage: " << argv0
		<< " <input.navmesh> <startX> <startY> <startZ> <endX> <endY> <endZ> [stepSize] [slop]\n";
}

} // namespace

int main(int argc, char** argv)
{
	if (argc != 8 && argc != 10)
	{
		printUsage(argv[0]);
		return argc == 1 ? 0 : 1;
	}

	try
	{
		LoadedNavMesh loaded = loadWrappedNavMesh(argv[1]);
		const float start[3] =
		{
			static_cast<float>(std::atof(argv[2])),
			static_cast<float>(std::atof(argv[3])),
			static_cast<float>(std::atof(argv[4]))
		};
		const float end[3] =
		{
			static_cast<float>(std::atof(argv[5])),
			static_cast<float>(std::atof(argv[6])),
			static_cast<float>(std::atof(argv[7]))
		};
		const float stepSize = argc == 10 ? static_cast<float>(std::atof(argv[8])) : 10.0f;
		const float slop = argc == 10 ? static_cast<float>(std::atof(argv[9])) : 0.3f;

		dtQueryFilter filter;
		filter.setIncludeFlags(0xffff);
		filter.setExcludeFlags(0);
		const float extents[3] = {2.f, 4.f, 2.f};

		dtPolyRef startRef = 0;
		dtPolyRef endRef = 0;
		float nearestStart[3];
		float nearestEnd[3];
		loaded.navQuery->findNearestPoly(start, extents, &filter, &startRef, nearestStart);
		loaded.navQuery->findNearestPoly(end, extents, &filter, &endRef, nearestEnd);
		if (!startRef || !endRef)
		{
			throw std::runtime_error("findNearestPoly failed");
		}

		static const int MAX_POLYS = 256;
		dtPolyRef polys[MAX_POLYS];
		int npolys = 0;
		loaded.navQuery->findPath(startRef, endRef, start, end, &filter, polys, &npolys, MAX_POLYS);
		if (!npolys)
		{
			throw std::runtime_error("findPath returned empty corridor");
		}

		float iterPos[3];
		float targetPos[3];
		dtVcopy(iterPos, start);
		dtVcopy(targetPos, end);

		std::vector<float> smooth;
		smooth.insert(smooth.end(), iterPos, iterPos + 3);

		while (npolys && smooth.size() / 3 < 256)
		{
			float steerPos[3];
			unsigned char steerPosFlag = 0;
			dtPolyRef steerPosRef = 0;
			if (!getSteerTarget(loaded.navQuery, iterPos, targetPos, slop,
				polys, npolys, steerPos, steerPosFlag, steerPosRef))
			{
				break;
			}

			const bool endOfPath = (steerPosFlag & DT_STRAIGHTPATH_END) ? true : false;
			float delta[3];
			dtVsub(delta, steerPos, iterPos);
			const float len = std::sqrt(delta[0] * delta[0] + delta[2] * delta[2]);
			float moveTgt[3];
			if ((endOfPath || len < stepSize) && len > 0.0001f)
			{
				dtVcopy(moveTgt, steerPos);
			}
			else
			{
				const float scale = stepSize / len;
				moveTgt[0] = iterPos[0] + delta[0] * scale;
				moveTgt[1] = iterPos[1] + delta[1] * scale;
				moveTgt[2] = iterPos[2] + delta[2] * scale;
			}

			float result[3];
			dtPolyRef visited[16];
			int nvisited = 0;
			loaded.navQuery->moveAlongSurface(polys[0], iterPos, moveTgt, &filter,
				result, visited, &nvisited, 16);
			npolys = fixupCorridor(polys, npolys, MAX_POLYS, visited, nvisited);

			float h = 0.0f;
			loaded.navQuery->getPolyHeight(polys[0], result, &h);
			result[1] = h;
			dtVcopy(iterPos, result);

			smooth.insert(smooth.end(), iterPos, iterPos + 3);
			if (endOfPath && inRange(iterPos, steerPos, slop, 1.0f))
			{
				break;
			}
		}

		std::cout << std::fixed << std::setprecision(6);
		std::cout << "stepSize=" << stepSize << " slop=" << slop
			<< " points=" << smooth.size() / 3 << "\n";
		for (size_t i = 0; i < smooth.size() / 3; ++i)
		{
			std::cout << i << ','
				<< smooth[i * 3 + 0] << ','
				<< smooth[i * 3 + 1] << ','
				<< smooth[i * 3 + 2] << "\n";
		}
		return 0;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "navmesh_smooth_path_demo: " << ex.what() << "\n";
		return 1;
	}
}
