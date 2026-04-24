# navigation 项目模块整理

> **变更说明（当前代码已不再包含 Tile / tmxparser / tinyxml 路线）**
>
> 项目当前聚焦在 `NavMesh` 与 `Grid` 两条路线。`NavTileHandle` 及其 `.tmx` 解析依赖（历史位置为 `depend/tmxparser/`、外部 `tinyxml`）已从 `CMakeLists.txt` 与仓库中移除。
>
> `navigation_handle.h` 中仍保留 `NAV_TILE = 2` 枚举值仅用于兼容原有枚举顺序，但已无对应实现，`Navigation::loadNavigation` 也不再尝试按 `.tmx` 分派。
>
> 下文保留的 Tile/tmxparser 相关描述仅作历史参考，阅读时请以这一说明为准。

## 1. 项目定位

这个仓库是一个偏底层的 C++ 导航库，整体来自 `KBEngine` 体系，核心目标是对外提供统一的寻路与空间查询能力。

当前代码实际保留的两条导航路线：

- `NavMesh` 路线：基于 `Recast/Detour` 的 3D 导航网格。
- `Grid` 路线：基于 `.map` 栅格阻挡图和 `A*` 的 2D 网格导航。

（历史上还存在一条基于 `TMX` 瓦片地图的 `Tile` 路线，现已移除，仅后文历史章节有所涉及。）

对业务层来说，统一入口是 `Navigation` 单例，外部只需要按场景名加载对应导航资源即可。

---

## 2. 模块总览

| 模块 | 目录 / 文件 | 作用 |
| --- | --- | --- |
| 导航总控模块 | `navigation.h/.cpp` | 统一加载、缓存、查找、移除导航实例 |
| 导航接口模块 | `navigation_handle.h/.cpp` | 定义统一导航能力接口 |
| Grid 运行时模块 | `navigation_grid_handle.h/.cpp` | 加载 `.map` 栅格地图并执行 2D A* |
| NavMesh 运行时模块 | `navigation_mesh_handle.h/.cpp` | 加载 `.navmesh` 文件并基于 Detour 执行查询 |
| Tile 运行时模块 | `navigation_tile_handle.h/.cpp` | 加载 `TMX` 地图并执行 A* 格子寻路 |
| Recast 构建核心 | `Recast*.h/.cpp` | 导航网格体素化、区域划分、轮廓与多边形网格生成 |
| Detour 查询核心 | `Detour*.h/.cpp` | NavMesh 数据结构、路径查询、射线、拥挤控制等 |
| KBEngine 基础支撑 | `depend/common` | 平台、内存、单例、智能指针、工具函数 |
| 数学模块 | `depend/math` | 向量、角度、位置类型、几何辅助 |
| 资源管理模块 | `depend/resmgr` | 资源路径解析、文件打开、目录枚举 |
| 第三方地图解析模块 | `depend/tmxparser` | `TMX` 地图解析，当前已移除 |
| 其他第三方依赖 | `depend/g3dlite` | `g3dlite` 通用依赖 |
| 工具模块 | `tools/navmesh_dump.cpp` | 解析并 dump `.navmesh` 文件结构 |
| 构建模块 | `CMakeLists.txt`、`Makefile`、`.vcxproj` | Linux / Windows 构建入口 |

---

## 3. 总体调用关系

```text
业务调用
   |
   v
Navigation
   |
   +-- 判断是否存在 spaces/<name>/<name>.map
   |      |
   |      +-- 是 -> NavGridHandle
   |
   +-- 判断是否存在 spaces/<name>/<name>.tmx
   |      |
   |      +-- 是 -> NavTileHandle
   |
   +-- 否 -> 扫描 spaces/<name>/*.navmesh
          |
          +-- NavMeshHandle

NavMeshHandle
   |
   +-- DetourNavMesh
   +-- DetourNavMeshQuery

NavGridHandle
   |
   +-- stlastar(A*)

NavTileHandle
   |
   +-- tmxparser
   +-- stlastar(A*)

所有模块共同依赖
   |
   +-- depend/common
   +-- depend/math
   +-- depend/resmgr
```

---

## 4. 详细模块说明

### 4.1 导航总控模块

**核心文件**

- `navigation.h`
- `navigation.cpp`

**职责**

- 作为导航系统统一入口。
- 负责按名称加载场景导航资源。
- 负责缓存已经加载过的导航句柄。
- 负责按名称查找或移除导航实例。

**关键点**

- `Navigation::loadNavigation(name)` 会先检查 `spaces/<name>/<name>.tmx`。
- 如果 `tmx` 存在，则走 `NavTileHandle::create(name)`。
- 否则扫描 `spaces/<name>` 下的 `.navmesh` 文件，走 `NavMeshHandle::create(name)`。

**依赖**

- `depend/resmgr`
- `navigation_handle`
- `navigation_mesh_handle`
- `navigation_tile_handle`

**当前状态**

- 是整个项目最上层的门面模块。
- 已经把 tile 导航和 navmesh 导航统一到了同一个加载入口上。

---

### 4.2 导航接口模块

**核心文件**

- `navigation_handle.h`
- `navigation_handle.cpp`

**职责**

- 抽象出统一导航能力接口。
- 屏蔽 Tile 和 NavMesh 两种实现的差异。

**统一接口**

- `findStraightPath`
- `raycast`
- `raycastNear`
- `raycastAlong`
- `GetHeight`
- `Intersects`
- `GetNearPos`

**设计特点**

- 使用 `RefCountable` 和 `SmartPointer` 做句柄生命周期管理。
- 通过 `NAV_TYPE` 区分 `NAV_MESH` 与 `NAV_TILE`。

**当前状态**

- 接口边界是清楚的。
- 但不同实现对这些接口的完成度不完全一致。

---

### 4.3 NavMesh 运行时模块

**核心文件**

- `navigation_mesh_handle.h`
- `navigation_mesh_handle.cpp`

**职责**

- 加载 `.navmesh` 文件。
- 将文件中的 tile 数据恢复到 `dtNavMesh`。
- 提供基于 Detour 的运行时查询能力。

**主要能力**

- 直线路径查询
- NavMesh 射线检测
- 贴地移动
- 最近可达点查询
- 高度查询
- 地形相交检测

**内部数据**

- `std::vector<dtNavMesh*> navmesh_layers`
- `std::vector<dtNavMeshQuery*> navmeshQuery_layers`

这说明它支持“多层 navmesh”。

**文件格式**

`.navmesh` 文件由两层结构组成：

1. 外层是项目自定义封装：
   - `NavMeshSetHeader`
   - 多个 `NavMeshTileHeader`
2. 内层 tile 数据是 Detour 原生的 tile blob。

**依赖**

- `DetourNavMesh`
- `DetourNavMeshQuery`
- `DetourNavMeshBuilder`
- `depend/resmgr`
- `depend/math`

**当前状态**

- 是当前项目里功能最完整、最成熟的导航实现。
- 更适合 3D 场景和运行时空间查询。

---

### 4.4 Grid 运行时模块

**核心文件**

- `navigation_grid_handle.h`
- `navigation_grid_handle.cpp`
- `stlastar.h`

**职责**

- 加载 `spaces/<name>/<name>.map`
- 将二进制栅格数据解析为单层 2D 阻挡图
- 基于 A* 执行 2D 路径搜索和直线穿行检查

**解析规则**

- 前 8 字节按 little-endian `int32` 读取为 `width`、`height`
- 后续数据长度必须精确等于 `width * height`
- 格子值语义当前固定为：`0` 可走，非 `0` 阻挡

**主要能力**

- 2D A* 路径搜索
- Bresenham 栅格射线
- 最近可走格查询
- 固定高度返回

**当前状态**

- 这是当前项目里最适合对接现有 `101.map` 的 2D 路线
- 不依赖 `tmxparser`
- 不做离线 navmesh 烘焙，直接读取运行时栅格图

---

### 4.5 Tile 运行时模块

**核心文件**

- `navigation_tile_handle.h`
- `navigation_tile_handle.cpp`
- `stlastar.h`

**职责**

- 加载 `TMX` 瓦片地图。
- 基于地图层和格子阻挡信息执行 A* 寻路。
- 提供简单的射线遍历能力。

**主要能力**

- 基于 A* 的路径搜索
- 4 方向或 8 方向格子移动
- 基于 Bresenham 的直线格子扫描

**依赖**

- `depend/tmxparser`
- `stlastar.h`
- `depend/math`

**设计特点**

- 使用静态 `AStarSearch<MapSearchNode>` 做搜索。
- `getMap(x, y)` 通过图层 tile id 判断格子状态和通行代价。
- 支持通过 `direction8` 配置 4 向或 8 向寻路。

**当前状态**

- 基础寻路逻辑存在。
- 一部分接口仍是占位实现，例如：
  - `raycastNear`
  - `raycastAlong`
  - `GetHeight`
  - `Intersects`
  - `GetNearPos`
- `create(name)` 里的 TMX 文件路径处理明显有残留改动痕迹，当前实现风险高于 NavMesh 路线。

**结论**

- 这个模块可视为“早期格子寻路分支”。
- 适合继续修补，但当前不是最稳的主线路。

---

### 4.6 Recast 构建核心模块

**核心文件**

- `Recast.h/.cpp`
- `RecastAlloc.h/.cpp`
- `RecastArea.cpp`
- `RecastContour.cpp`
- `RecastFilter.cpp`
- `RecastLayers.cpp`
- `RecastMesh.cpp`
- `RecastMeshDetail.cpp`
- `RecastRasterization.cpp`
- `RecastRegion.cpp`

**职责**

- 把输入几何转成导航网格构建中间结果。
- 完成体素化、可行走区域筛选、区域划分、轮廓生成、多边形网格生成、细节网格生成。

**在当前仓库中的角色**

- 这是 navmesh 的“生成算法库”。
- 当前仓库里主要看到运行时加载与查询逻辑，未看到本地完整的 `.navmesh` 导出流程。

**结论**

- Recast 在这里更多是“能力底座”。
- 生成器大概率位于该仓库外部的工具链中。

---

### 4.7 Detour 查询核心模块

**核心文件**

- `DetourNavMesh.h/.cpp`
- `DetourNavMeshQuery.h/.cpp`
- `DetourNavMeshBuilder.h/.cpp`
- `DetourNode.h/.cpp`
- `DetourCommon.h/.cpp`
- `DetourCrowd.h/.cpp`
- `DetourPathCorridor.h/.cpp`
- `DetourPathQueue.h/.cpp`
- `DetourObstacleAvoidance.h/.cpp`
- `DetourTileCache.h/.cpp`
- `DetourTileCacheBuilder.h/.cpp`
- `DetourLocalBoundary.h/.cpp`
- `DetourProximityGrid.h/.cpp`

**职责**

- 表示 NavMesh 数据结构。
- 执行路径搜索、最近多边形查找、射线检测、表面移动等查询。
- 提供 crowd、corridor、obstacle avoidance、tile cache 等高级能力。

**在当前项目里的实际使用情况**

- `navigation_mesh_handle.cpp` 主要直接使用：
  - `dtNavMesh`
  - `dtNavMeshQuery`
  - `dtCreateNavMeshData` 对应的数据格式

**说明**

- 虽然 `DetourCrowd` 和 `DetourTileCache` 也在项目中，但当前顶层封装暂未直接暴露它们的完整能力。

---

### 4.8 KBEngine 基础支撑模块

**目录**

- `depend/common`

**职责**

- 提供平台宏、基础类型、内存辅助、单例、智能指针、字符串工具、格式化工具、计时器等公共能力。

**导航模块直接使用较多的内容**

- `common.h`
- `singleton.h`
- `smartpointer.h`
- `strutil.h/.cpp`
- `refcountable.h`

**在项目中的作用**

- 是导航封装层与资源系统的公共底座。

---

### 4.9 数学模块

**目录**

- `depend/math`

**职责**

- 统一 2D / 3D 数学类型与辅助方法。
- 定义导航代码中频繁使用的 `Position3D`、`Vector3` 等类型。

**特点**

- Windows 分支可以走 `D3DX`。
- 非 Windows 分支主要走 `g3dlite`。

**在项目中的作用**

- 所有路径点、碰撞点、高度点、方向计算都依赖这里的类型定义。

---

### 4.10 资源管理模块

**目录**

- `depend/resmgr`

**职责**

- 管理资源根目录与资源搜索路径。
- 提供资源文件匹配、打开、列举等能力。

**导航模块的典型使用**

- `openRes(path + "/" + name + ".tmx")`
- `listPathRes(wspath, L"navmesh", results)`

**在项目中的作用**

- `Navigation` 不直接关心磁盘细节，统一通过 `Resmgr` 找资源。

---

### 4.11 第三方地图解析模块

**目录**

- `depend/tmxparser`

**职责**

- 解析 Tiled 导出的 `TMX` 地图。
- 提供地图、图层、tile、tileset、属性等对象模型。

**在项目中的作用**

- `NavTileHandle` 依赖它读取地图宽高、tile 尺寸、图层和 tile id。

---

### 4.12 其他第三方依赖模块

**目录**

- `depend/g3dlite`

**作用概览**

- `g3dlite`：数学与几何类型实现。

**说明**

- 这些模块多数不是导航逻辑本体，但为跨平台构建和运行时提供支撑。

---

### 4.13 工具模块

**目录**

- `tools/navmesh_dump.cpp`
- `tools/README.md`

**职责**

- 解析项目自定义 `.navmesh` 外层格式。
- 展示 `NavMeshSetHeader` 和每个 tile 的 `dtMeshHeader` 摘要。
- 用于调试资源文件、校验 tile 大小、检查 header 信息。

**适用场景**

- 验证 `.navmesh` 是否损坏。
- 查看 tile 数量、坐标、polygon 数、bounds 等元信息。
- 协助反查资源生成结果是否正确。

---

### 4.14 构建模块

**核心文件**

- `CMakeLists.txt`
- `Makefile`
- `navigation.sln`
- `navigation.vcxproj`

**职责**

- 提供 Linux / Windows 下的库构建入口。
- 当前额外支持构建 `navmesh_dump` 可执行工具。

**说明**

- `Makefile` 明显带有 `KBEngine` 总工程路径假设。
- `CMakeLists.txt` 更适合当前局部仓库的独立构建。

---

## 5. 关键资源类型

| 资源类型 | 用途 | 处理模块 |
| --- | --- | --- |
| `.map` | 2D 栅格阻挡图运行时资源 | `NavGridHandle` |
| `.tmx` | 格子地图导航资源 | `NavTileHandle` |
| `.navmesh` | 预烘焙导航网格资源 | `NavMeshHandle` |

`.navmesh` 文件结构可参考 `tools/README.md`，它本质上是：

- 项目自定义文件头
- 多个 Detour tile 数据块

---

## 6. 当前项目状态判断

### 6.1 稳定主线

- `Navigation`
- `NavigationHandle`
- `NavGridHandle`
- `NavMeshHandle`
- `Detour` 运行时查询链路

这条线已经能完成：

- 资源加载
- 路径查询
- 射线检测
- 高度与表面相关查询

### 6.2 次要分支

- `NavTileHandle`

这条线仍保留历史实现痕迹，功能不完整，适合后续补全或重构。

### 6.3 能力底座

- `Recast`
- `Detour` 完整源码
- `depend/*`
- `depend/g3dlite`

这些模块更多是底层支撑，而不是业务层直接使用的入口。

---

## 7. 建议的阅读顺序

如果第一次接触这个项目，推荐按下面顺序阅读：

1. `navigation.h/.cpp`
2. `navigation_handle.h`
3. `navigation_mesh_handle.h/.cpp`
4. `tools/README.md`
5. `tools/navmesh_dump.cpp`
6. `navigation_tile_handle.h/.cpp`
7. `DetourNavMesh.h/.cpp`
8. `DetourNavMeshQuery.h/.cpp`
9. `Recast*.cpp`

这样能先理解项目入口和运行时逻辑，再回头看底层算法实现，不容易一上来就被 Recast/Detour 细节淹没。
