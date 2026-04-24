#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
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

	LoadedNavMesh() : navMesh(NULL) {}
	~LoadedNavMesh()
	{
		if (navMesh)
		{
			dtFreeNavMesh(navMesh);
		}
	}
};

struct EdgeKey
{
	int ax;
	int ay;
	int az;
	int bx;
	int by;
	int bz;

	bool operator<(const EdgeKey& other) const
	{
		if (ax != other.ax) return ax < other.ax;
		if (ay != other.ay) return ay < other.ay;
		if (az != other.az) return az < other.az;
		if (bx != other.bx) return bx < other.bx;
		if (by != other.by) return by < other.by;
		return bz < other.bz;
	}
};

struct EdgeRef
{
	int tri;
	int edge;
};

struct Triangle
{
	float v[3][3];
	float center[3];
	int neighbors[3];
};

struct SearchNode
{
	int tri;
	float total;

	bool operator<(const SearchNode& other) const
	{
		return total > other.total;
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
	const std::vector<unsigned char> fileData = readFile(path);
	size_t offset = 0;
	const NavMeshSetHeader header =
		readStruct<NavMeshSetHeader>(&fileData[0], fileData.size(), offset, "NavMeshSetHeader");
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
		const NavMeshTileHeader tileHeader =
			readStruct<NavMeshTileHeader>(&fileData[0], fileData.size(), offset, "NavMeshTileHeader");
		offset += sizeof(NavMeshTileHeader);
		unsigned char* tileData =
			static_cast<unsigned char*>(dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM));
		std::memcpy(tileData, &fileData[offset], tileHeader.dataSize);
		offset += static_cast<size_t>(tileHeader.dataSize);
		if (dtStatusFailed(loaded.navMesh->addTile(
			tileData, tileHeader.dataSize, DT_TILE_FREE_DATA, tileHeader.tileRef, NULL)))
		{
			dtFree(tileData);
			throw std::runtime_error("addTile failed");
		}
	}
	return loaded;
}

int quantize(float v)
{
	return static_cast<int>(std::floor(v * 1000.0f + 0.5f));
}

EdgeKey makeEdgeKey(const float* a, const float* b)
{
	int av[3] = {quantize(a[0]), quantize(a[1]), quantize(a[2])};
	int bv[3] = {quantize(b[0]), quantize(b[1]), quantize(b[2])};
	if (std::lexicographical_compare(bv, bv + 3, av, av + 3))
	{
		std::swap(av[0], bv[0]);
		std::swap(av[1], bv[1]);
		std::swap(av[2], bv[2]);
	}

	EdgeKey key;
	key.ax = av[0]; key.ay = av[1]; key.az = av[2];
	key.bx = bv[0]; key.by = bv[1]; key.bz = bv[2];
	return key;
}

void getDetailTriVerts(const dtMeshTile* tile, const dtPoly* poly, int polyIndex, int triIndex, const float* out[3])
{
	const dtPolyDetail& detail = tile->detailMeshes[polyIndex];
	const unsigned char* tri = &tile->detailTris[(detail.triBase + triIndex) * 4];
	for (int i = 0; i < 3; ++i)
	{
		if (tri[i] < poly->vertCount)
		{
			out[i] = &tile->verts[poly->verts[tri[i]] * 3];
		}
		else
		{
			out[i] = &tile->detailVerts[(detail.vertBase + (tri[i] - poly->vertCount)) * 3];
		}
	}
}

void buildTriangles(const dtNavMesh& navMesh, std::vector<Triangle>& triangles)
{
	std::map<EdgeKey, std::vector<EdgeRef> > edges;

	for (int i = 0; i < navMesh.getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = navMesh.getTile(i);
		if (!tile || !tile->header)
		{
			continue;
		}

		for (int p = 0; p < tile->header->polyCount; ++p)
		{
			const dtPoly* poly = &tile->polys[p];
			if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
			{
				continue;
			}

			const dtPolyDetail& detail = tile->detailMeshes[p];
			for (unsigned int t = 0; t < detail.triCount; ++t)
			{
				const float* verts[3];
				getDetailTriVerts(tile, poly, p, static_cast<int>(t), verts);

				Triangle tri;
				std::memset(&tri, 0, sizeof(tri));
				for (int v = 0; v < 3; ++v)
				{
					dtVcopy(tri.v[v], verts[v]);
					tri.center[0] += verts[v][0];
					tri.center[1] += verts[v][1];
					tri.center[2] += verts[v][2];
					tri.neighbors[v] = -1;
				}
				tri.center[0] /= 3.f;
				tri.center[1] /= 3.f;
				tri.center[2] /= 3.f;

				const int triId = static_cast<int>(triangles.size());
				triangles.push_back(tri);
				for (int edge = 0; edge < 3; ++edge)
				{
					const EdgeKey key = makeEdgeKey(verts[edge], verts[(edge + 1) % 3]);
					edges[key].push_back(EdgeRef{triId, edge});
				}
			}
		}
	}

	for (std::map<EdgeKey, std::vector<EdgeRef> >::const_iterator iter = edges.begin();
		iter != edges.end(); ++iter)
	{
		const std::vector<EdgeRef>& refs = iter->second;
		for (size_t i = 0; i < refs.size(); ++i)
		{
			for (size_t j = i + 1; j < refs.size(); ++j)
			{
				triangles[refs[i].tri].neighbors[refs[i].edge] = refs[j].tri;
				triangles[refs[j].tri].neighbors[refs[j].edge] = refs[i].tri;
			}
		}
	}
}

float dist2D(const float* a, const float* b)
{
	const float dx = a[0] - b[0];
	const float dz = a[2] - b[2];
	return std::sqrt(dx * dx + dz * dz);
}

float pointSegDist2DSqr(const float* p, const float* a, const float* b)
{
	float t = 0.f;
	return dtDistancePtSegSqr2D(p, a, b, t);
}

int findNearestTriangle(const std::vector<Triangle>& triangles, const float* p)
{
	int best = -1;
	float bestDist = std::numeric_limits<float>::max();
	for (size_t i = 0; i < triangles.size(); ++i)
	{
		const Triangle& tri = triangles[i];
		bool inside = true;
		const float area = dtTriArea2D(tri.v[0], tri.v[1], tri.v[2]);
		for (int e = 0; e < 3; ++e)
		{
			const float edgeArea = dtTriArea2D(tri.v[e], tri.v[(e + 1) % 3], p);
			if ((area >= 0.f && edgeArea < -0.0001f) ||
				(area < 0.f && edgeArea > 0.0001f))
			{
				inside = false;
				break;
			}
		}

		float dist = 0.f;
		if (!inside)
		{
			dist = std::min(pointSegDist2DSqr(p, tri.v[0], tri.v[1]),
				std::min(pointSegDist2DSqr(p, tri.v[1], tri.v[2]),
					pointSegDist2DSqr(p, tri.v[2], tri.v[0])));
		}

		if (dist < bestDist)
		{
			bestDist = dist;
			best = static_cast<int>(i);
		}
	}
	return best;
}

bool sharedEdge(const Triangle& a, const Triangle& b, float left[3], float right[3]);

bool sharedEdgeMid(const Triangle& a, const Triangle& b, float mid[3])
{
	float left[3], right[3];
	if (!sharedEdge(a, b, left, right))
	{
		return false;
	}
	dtVlerp(mid, left, right, 0.5f);
	return true;
}

bool findTrianglePath(const std::vector<Triangle>& triangles, int start, int end,
	const float* startPos, const float* endPos, std::vector<int>& path)
{
	const int n = static_cast<int>(triangles.size());
	std::vector<float> cost(n, std::numeric_limits<float>::max());
	std::vector<int> parent(n, -1);
	std::vector<float> nodePos(n * 3, 0.f);
	std::priority_queue<SearchNode> open;

	cost[start] = 0.f;
	dtVcopy(&nodePos[start * 3], startPos);
	open.push(SearchNode{start, dist2D(startPos, endPos)});

	while (!open.empty())
	{
		const SearchNode node = open.top();
		open.pop();
		if (node.tri == end)
		{
			break;
		}

		const Triangle& tri = triangles[node.tri];
		for (int edge = 0; edge < 3; ++edge)
		{
			const int next = tri.neighbors[edge];
			if (next < 0)
			{
				continue;
			}

			float nextPos[3];
			if (next == end)
			{
				dtVcopy(nextPos, endPos);
			}
			else if (!sharedEdgeMid(tri, triangles[next], nextPos))
			{
				continue;
			}

			const float nextCost = cost[node.tri] + dist2D(&nodePos[node.tri * 3], nextPos);
			if (nextCost < cost[next])
			{
				cost[next] = nextCost;
				parent[next] = node.tri;
				dtVcopy(&nodePos[next * 3], nextPos);
				open.push(SearchNode{next, nextCost + dist2D(nextPos, endPos)});
			}
		}
	}

	if (parent[end] < 0 && start != end)
	{
		return false;
	}

	for (int at = end; at >= 0; at = parent[at])
	{
		path.push_back(at);
		if (at == start)
		{
			break;
		}
	}
	std::reverse(path.begin(), path.end());
	return !path.empty() && path.front() == start;
}

bool sharedEdge(const Triangle& a, const Triangle& b, float left[3], float right[3])
{
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			const float* a0 = a.v[i];
			const float* a1 = a.v[(i + 1) % 3];
			const float* b0 = b.v[j];
			const float* b1 = b.v[(j + 1) % 3];
			if ((dist2D(a0, b0) < 0.002f && dist2D(a1, b1) < 0.002f) ||
				(dist2D(a0, b1) < 0.002f && dist2D(a1, b0) < 0.002f))
			{
				dtVcopy(left, a0);
				dtVcopy(right, a1);
				return true;
			}
		}
	}
	return false;
}

bool vequal2D(const float* a, const float* b)
{
	const float dx = a[0] - b[0];
	const float dz = a[2] - b[2];
	return dx * dx + dz * dz < 0.0001f;
}

void appendPoint(std::vector<float>& out, const float* p)
{
	if (out.size() >= 3)
	{
		const float* last = &out[out.size() - 3];
		if (dist2D(last, p) < 0.01f)
		{
			return;
		}
	}
	out.push_back(p[0]);
	out.push_back(p[1]);
	out.push_back(p[2]);
}

void funnelPath(const std::vector<Triangle>& triangles, const std::vector<int>& path,
	const float* start, const float* end, std::vector<float>& out)
{
	struct Portal
	{
		float left[3];
		float right[3];
	};

	std::vector<Portal> portals;
	Portal first;
	dtVcopy(first.left, start);
	dtVcopy(first.right, start);
	portals.push_back(first);

	for (size_t i = 1; i < path.size(); ++i)
	{
		float a[3], b[3];
		if (!sharedEdge(triangles[path[i - 1]], triangles[path[i]], a, b))
		{
			continue;
		}

		Portal portal;
		if (dtTriArea2D(triangles[path[i - 1]].center, triangles[path[i]].center, a) < 0.f)
		{
			dtVcopy(portal.left, a);
			dtVcopy(portal.right, b);
		}
		else
		{
			dtVcopy(portal.left, b);
			dtVcopy(portal.right, a);
		}
		portals.push_back(portal);
	}

	Portal last;
	dtVcopy(last.left, end);
	dtVcopy(last.right, end);
	portals.push_back(last);

	appendPoint(out, start);

	float portalApex[3], portalLeft[3], portalRight[3];
	dtVcopy(portalApex, portals[0].left);
	dtVcopy(portalLeft, portalApex);
	dtVcopy(portalRight, portalApex);
	int apexIndex = 0;
	int leftIndex = 0;
	int rightIndex = 0;

	for (int i = 1; i < static_cast<int>(portals.size()); ++i)
	{
		const float* left = portals[i].left;
		const float* right = portals[i].right;

		if (dtTriArea2D(portalApex, portalRight, right) <= 0.0f)
		{
			if (vequal2D(portalApex, portalRight) ||
				dtTriArea2D(portalApex, portalLeft, right) > 0.0f)
			{
				dtVcopy(portalRight, right);
				rightIndex = i;
			}
			else
			{
				appendPoint(out, portalLeft);
				dtVcopy(portalApex, portalLeft);
				apexIndex = leftIndex;
				dtVcopy(portalLeft, portalApex);
				dtVcopy(portalRight, portalApex);
				leftIndex = apexIndex;
				rightIndex = apexIndex;
				i = apexIndex;
				continue;
			}
		}

		if (dtTriArea2D(portalApex, portalLeft, left) >= 0.0f)
		{
			if (vequal2D(portalApex, portalLeft) ||
				dtTriArea2D(portalApex, portalRight, left) < 0.0f)
			{
				dtVcopy(portalLeft, left);
				leftIndex = i;
			}
			else
			{
				appendPoint(out, portalRight);
				dtVcopy(portalApex, portalRight);
				apexIndex = rightIndex;
				dtVcopy(portalLeft, portalApex);
				dtVcopy(portalRight, portalApex);
				leftIndex = apexIndex;
				rightIndex = apexIndex;
				i = apexIndex;
				continue;
			}
		}
	}

	appendPoint(out, end);
}

void printUsage(const char* argv0)
{
	std::cout << "Usage: " << argv0
		<< " <input.navmesh> <startX> <startY> <startZ> <endX> <endY> <endZ>\n";
}

} // namespace

int main(int argc, char** argv)
{
	if (argc != 8)
	{
		printUsage(argv[0]);
		return argc == 1 ? 0 : 1;
	}

	try
	{
		LoadedNavMesh loaded = loadWrappedNavMesh(argv[1]);
		std::vector<Triangle> triangles;
		buildTriangles(*loaded.navMesh, triangles);

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

		const int startTri = findNearestTriangle(triangles, start);
		const int endTri = findNearestTriangle(triangles, end);
		std::vector<int> triPath;
		if (startTri < 0 || endTri < 0 ||
			!findTrianglePath(triangles, startTri, endTri, start, end, triPath))
		{
			throw std::runtime_error("triangle path failed");
		}

		std::vector<float> points;
		funnelPath(triangles, triPath, start, end, points);

		std::cout << std::fixed << std::setprecision(6);
		std::cout << "triangles=" << triangles.size()
			<< " triPath=" << triPath.size()
			<< " points=" << points.size() / 3 << "\n";
		for (size_t i = 0; i < points.size() / 3; ++i)
		{
			std::cout << i << ','
				<< points[i * 3 + 0] << ','
				<< points[i * 3 + 1] << ','
				<< points[i * 3 + 2] << "\n";
		}
		return 0;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "navmesh_triangle_path_demo: " << ex.what() << "\n";
		return 1;
	}
}
