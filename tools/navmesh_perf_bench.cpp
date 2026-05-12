#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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
	dtNavMesh* navMesh = 0;
	dtNavMeshQuery* navQuery = 0;

	~LoadedNavMesh()
	{
		if (navQuery)
			dtFreeNavMeshQuery(navQuery);
		if (navMesh)
			dtFreeNavMesh(navMesh);
	}
};

struct PolyPoint
{
	dtPolyRef ref = 0;
	float pos[3] = {0.f, 0.f, 0.f};
};

template <typename T>
T readStruct(const unsigned char* data, size_t size, size_t offset, const char* what)
{
	if (offset + sizeof(T) > size)
	{
		std::ostringstream oss;
		oss << "Unexpected EOF while reading " << what << " at offset " << offset;
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
		oss << "Failed to open '" << path << "': " << std::strerror(errno);
		throw std::runtime_error(oss.str());
	}

	input.seekg(0, std::ios::end);
	const std::streamoff fileSize = input.tellg();
	if (fileSize < 0)
		throw std::runtime_error("Failed to determine file size.");
	input.seekg(0, std::ios::beg);

	std::vector<unsigned char> data(static_cast<size_t>(fileSize));
	if (!data.empty())
	{
		input.read(reinterpret_cast<char*>(&data[0]), fileSize);
		if (!input)
			throw std::runtime_error("Failed to read the complete file.");
	}

	return data;
}

LoadedNavMesh loadWrappedNavMesh(const std::string& path)
{
	const std::vector<unsigned char> fileData = readFile(path);
	if (fileData.size() < sizeof(NavMeshSetHeader))
		throw std::runtime_error("File is smaller than NavMeshSetHeader.");

	size_t offset = 0;
	const NavMeshSetHeader fileHeader =
		readStruct<NavMeshSetHeader>(&fileData[0], fileData.size(), offset, "NavMeshSetHeader");
	offset += sizeof(NavMeshSetHeader);

	if (fileHeader.version != 1)
		throw std::runtime_error("Unsupported NavMeshSetHeader version.");

	LoadedNavMesh loaded;
	loaded.navMesh = dtAllocNavMesh();
	if (!loaded.navMesh)
		throw std::runtime_error("dtAllocNavMesh failed.");

	if (dtStatusFailed(loaded.navMesh->init(&fileHeader.params)))
		throw std::runtime_error("dtNavMesh::init(params) failed.");

	for (int i = 0; i < fileHeader.tileCount; ++i)
	{
		const NavMeshTileHeader tileHeader =
			readStruct<NavMeshTileHeader>(&fileData[0], fileData.size(), offset, "NavMeshTileHeader");
		offset += sizeof(NavMeshTileHeader);

		if (!tileHeader.tileRef || tileHeader.dataSize <= 0)
			throw std::runtime_error("Invalid tile header encountered.");
		if (offset + static_cast<size_t>(tileHeader.dataSize) > fileData.size())
			throw std::runtime_error("Tile data overruns the file.");

		unsigned char* tileData =
			static_cast<unsigned char*>(dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM));
		if (!tileData)
			throw std::runtime_error("Failed to allocate tile data.");

		std::memcpy(tileData, &fileData[offset], tileHeader.dataSize);
		offset += static_cast<size_t>(tileHeader.dataSize);

		const dtStatus status = loaded.navMesh->addTile(
			tileData, tileHeader.dataSize, DT_TILE_FREE_DATA, tileHeader.tileRef, 0);
		if (dtStatusFailed(status))
		{
			dtFree(tileData);
			throw std::runtime_error("dtNavMesh::addTile failed.");
		}
	}

	loaded.navQuery = dtAllocNavMeshQuery();
	if (!loaded.navQuery)
		throw std::runtime_error("dtAllocNavMeshQuery failed.");
	if (dtStatusFailed(loaded.navQuery->init(loaded.navMesh, 8192)))
		throw std::runtime_error("dtNavMeshQuery::init failed.");

	return loaded;
}

std::vector<PolyPoint> collectWalkablePolyCenters(const dtNavMesh& navMesh)
{
	std::vector<PolyPoint> points;

	for (int i = 0; i < navMesh.getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = navMesh.getTile(i);
		if (!tile || !tile->header)
			continue;

		const dtPolyRef base = navMesh.getPolyRefBase(tile);
		for (int p = 0; p < tile->header->polyCount; ++p)
		{
			const dtPoly& poly = tile->polys[p];
			if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
				continue;

			PolyPoint point;
			point.ref = base | static_cast<dtPolyRef>(p);
			for (unsigned int v = 0; v < poly.vertCount; ++v)
			{
				const float* vert = &tile->verts[poly.verts[v] * 3];
				point.pos[0] += vert[0];
				point.pos[1] += vert[1];
				point.pos[2] += vert[2];
			}
			const float inv = 1.0f / static_cast<float>(poly.vertCount);
			point.pos[0] *= inv;
			point.pos[1] *= inv;
			point.pos[2] *= inv;
			points.push_back(point);
		}
	}

	return points;
}

void printUsage(const char* argv0)
{
	std::cout << "Usage: " << argv0 << " <file.navmesh> [iterations]\n";
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2 || argc > 3)
	{
		printUsage(argv[0]);
		return argc == 1 ? 0 : 1;
	}

	try
	{
		const std::string path = argv[1];
		const int iterations = argc == 3 ? std::atoi(argv[2]) : 10000;
		if (iterations <= 0)
			throw std::runtime_error("iterations must be positive.");

		LoadedNavMesh loaded = loadWrappedNavMesh(path);
		const std::vector<PolyPoint> points = collectWalkablePolyCenters(*loaded.navMesh);
		if (points.size() < 2)
			throw std::runtime_error("Need at least two walkable polygons.");

		dtQueryFilter filter;
		filter.setIncludeFlags(0xffff);
		filter.setExcludeFlags(0);
		const float extents[3] = {2.f, 4.f, 2.f};

		static const int MAX_POLYS = 256;
		dtPolyRef corridor[MAX_POLYS];
		float straight[MAX_POLYS * 3];
		unsigned char straightFlags[MAX_POLYS];
		dtPolyRef straightRefs[MAX_POLYS];

		int nearestFailures = 0;
		int emptyPaths = 0;
		long long totalCorridor = 0;
		long long totalStraight = 0;

		const auto begin = std::chrono::steady_clock::now();
		for (int i = 0; i < iterations; ++i)
		{
			const size_t a = (static_cast<size_t>(i) * 73u) % points.size();
			const size_t b = (static_cast<size_t>(i) * 193u + points.size() / 2u) % points.size();

			dtPolyRef startRef = 0;
			dtPolyRef endRef = 0;
			float nearestStart[3];
			float nearestEnd[3];
			loaded.navQuery->findNearestPoly(points[a].pos, extents, &filter, &startRef, nearestStart);
			loaded.navQuery->findNearestPoly(points[b].pos, extents, &filter, &endRef, nearestEnd);
			if (!startRef || !endRef)
			{
				++nearestFailures;
				continue;
			}

			int corridorCount = 0;
			loaded.navQuery->findPath(
				startRef, endRef, nearestStart, nearestEnd, &filter,
				corridor, &corridorCount, MAX_POLYS);
			if (!corridorCount)
			{
				++emptyPaths;
				continue;
			}

			int straightCount = 0;
			loaded.navQuery->findStraightPath(
				nearestStart, nearestEnd, corridor, corridorCount,
				straight, straightFlags, straightRefs, &straightCount, MAX_POLYS);

			totalCorridor += corridorCount;
			totalStraight += straightCount;
		}
		const auto end = std::chrono::steady_clock::now();

		const double elapsedUs =
			std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(end - begin).count();
		const int completed = iterations - nearestFailures - emptyPaths;

		std::cout << "File: " << path << "\n";
		std::cout << "Walkable polygons: " << points.size() << "\n";
		std::cout << "Iterations: " << iterations << "\n";
		std::cout << "Completed paths: " << completed << "\n";
		std::cout << "Nearest failures: " << nearestFailures << "\n";
		std::cout << "Empty paths: " << emptyPaths << "\n";
		std::cout << std::fixed << std::setprecision(3);
		std::cout << "Total time: " << elapsedUs / 1000.0 << " ms\n";
		std::cout << "Average query: " << elapsedUs / static_cast<double>(iterations) << " us\n";
		if (completed > 0)
		{
			std::cout << "Average corridor polys: "
				<< static_cast<double>(totalCorridor) / static_cast<double>(completed) << "\n";
			std::cout << "Average straight points: "
				<< static_cast<double>(totalStraight) / static_cast<double>(completed) << "\n";
		}

		return 0;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "navmesh_perf_bench: " << ex.what() << "\n";
		return 1;
	}
}
