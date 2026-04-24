# navigation

`navigation` 是一个偏底层的 C++ 导航项目，核心目标是为游戏或仿真场景提供统一的寻路与空间查询能力。当前仓库以 `Recast/Detour` 为核心，保留了可直接用于运行时查询的 `NavMesh` 路线，同时保留了基于 `.map` 与 `A*` 的 `Grid` 路线，适合做场景导航验证、烘焙链路研究和运行时接口联调。

项目当前已经补齐一套面向 Linux 的 CMake 构建方式，并提供了一组可直接执行的小工具，覆盖从 `OBJ -> .navmesh/.svg` 烘焙、二进制结构检查、运行时查询验证，到导出 JSON/OBJ 的常见工作流。

## 项目特点

- 基于仓库内源码直接构建，不依赖额外拉取 `Recast/Detour`
- 支持 `NavMesh` 运行时加载、路径查询、射线检测、贴地移动等能力
- 保留 `Grid` 路线，可基于 `.map` 做 2D A* 验证
- 自带 `navmesh_bake_from_obj`，可将 `OBJ` 场景几何烘焙成项目可加载的 `.navmesh`
- 支持导出顶视图 `SVG`、结构化 `JSON`、调试 `dump` 信息
- 仓库内附带 `101.obj`、`res/101_nav.navmesh` 和 `output_101_navmesh/` 示例结果，便于直接复现

## 目录说明

- `CMakeLists.txt`
  当前推荐的构建入口
- `navigation*.h/.cpp`
  项目对外导航接口与总控逻辑
- `Recast*.h/.cpp`
  NavMesh 构建相关核心实现
- `Detour*.h/.cpp`
  NavMesh 查询、路径和运行时相关核心实现
- `depend/`
  `common`、`math`、`resmgr`、`g3dlite` 等依赖模块
- `tools/`
  一组独立可执行工具，覆盖烘焙、查询和导出
- `test/`
  最小运行时加载示例
- `docs/`
  构建说明、模块整理和设计文档
- `res/`
  仓库内示例导航资源
- `output_101_navmesh/`
  `101.obj` 的样例输出结果

## 主要能力

### 1. NavMesh 运行时能力

当前 `NavMesh` 路线围绕 `navigation_mesh_handle.cpp` 展开，主要支持：

- 加载项目自定义封装格式的 `.navmesh`
- 恢复内部 `Detour` tile 数据
- `findPath` / `findStraightPath`
- `raycast`
- `moveAlongSurface`
- `findDistanceToWall`
- 最近可达点、高度与空间相交检测

### 2. Grid 路线

`Grid` 路线围绕 `navigation_grid_handle.cpp` 展开，使用 `.map` 阻挡图与 `A*` 做 2D 网格导航验证，适合与 `NavMesh` 路线做结果对照或做兼容性保留。

### 3. NavMesh 烘焙与分析工具

当前仓库主要工具包括：

- `navmesh_bake_from_obj`
  将 `OBJ` 场景网格烘焙为项目可直接加载的 `.navmesh`，并可选输出顶视图 `SVG`
- `navmesh_dump`
  读取 `.navmesh` 并打印文件头、tile 头与 `dtMeshHeader` 摘要
- `navmesh_query_demo`
  加载 `.navmesh` 后直接运行一组最小查询验证
- `navmesh_export_json`
  将 `.navmesh` 导出为便于阅读和二次处理的 JSON
- `navmesh_export_obj`
  将 `.navmesh` 中的几何导出回 OBJ
- `navmesh_path_compare`
  用于路径结果对比分析
- `navmesh_triangle_path_demo`
  用于观察多边形/三角形路径相关行为
- `navmesh_smooth_path_demo`
  用于检查平滑路径效果

## 快速开始

### 环境要求

推荐环境：

- `cmake >= 3.28`
- `g++` 或 `clang++`
- `make` 或 `ninja`

### 构建

在仓库根目录执行：

```bash
cmake -S . -B build
cmake --build build -j4
```

默认会生成：

- `build/navmesh_bake_from_obj`
- `build/navmesh_dump`
- `build/navmesh_query_demo`
- `build/navmesh_export_json`
- `build/navmesh_export_obj`
- `build/navmesh_path_compare`
- `build/navmesh_triangle_path_demo`
- `build/navmesh_smooth_path_demo`
- `build/grid_map_demo`
- `build/navigation_load_by_name_demo`

Linux 下同时会在仓库内约定路径生成 `libnavigation.so`。

## 示例工作流

### 1. 从 `101.obj` 烘焙 navmesh

```bash
./build/navmesh_bake_from_obj \
  ./101.obj \
  output_101_navmesh/101_web_like.navmesh \
  output_101_navmesh/101_web_like.svg \
  --cell-size 0.08 \
  --cell-height 0.48 \
  --agent-height 2.0 \
  --agent-radius 0.01 \
  --agent-max-climb 1.2 \
  --agent-max-slope 90 \
  --region-min-size 4 \
  --region-merge-size 12 \
  --edge-max-len 12 \
  --edge-max-error 1.1 \
  --verts-per-poly 6 \
  --detail-sample-dist 4 \
  --detail-sample-max-error 0.8
```

这条命令会输出：

- `output_101_navmesh/101_web_like.navmesh`
- `output_101_navmesh/101_web_like.svg`

### 2. 检查 navmesh 文件结构

```bash
./build/navmesh_dump output_101_navmesh/101_web_like.navmesh
```

### 3. 运行最小查询验证

```bash
./build/navmesh_query_demo output_101_navmesh/101_web_like.navmesh
```

### 4. 导出 JSON 便于分析

```bash
./build/navmesh_export_json \
  output_101_navmesh/101_web_like.navmesh \
  output_101_navmesh/101_web_like.json
```

## 文件格式说明

当前 `.navmesh` 文件不是纯 `Detour` 原生裸数据，而是项目自定义的二进制封装，整体结构大致如下：

```text
+---------------------+
| NavMeshSetHeader    |
+---------------------+
| NavMeshTileHeader 0 |
+---------------------+
| Tile Data 0         |
+---------------------+
| NavMeshTileHeader 1 |
+---------------------+
| Tile Data 1         |
+---------------------+
| ...                 |
+---------------------+
```

其中 tile payload 内部仍然是 `Detour` 生成的 tile blob，因此既保留了工程侧的外层封装，又兼容运行时按 tile 恢复 `dtNavMesh` 的方式。

## 文档索引

- [docs/build.md](docs/build.md)
  更详细的 Linux 构建说明与调试说明
- [docs/project_modules.md](docs/project_modules.md)
  项目模块拆解与历史背景说明
- [docs/navigation_async_design.md](docs/navigation_async_design.md)
  导航异步化相关设计整理
- [tools/README.md](tools/README.md)
  工具链与样例命令说明

## 当前适用场景

这个仓库比较适合以下几类工作：

- 研究 `KBEngine` 风格导航系统的本地实现
- 将 `OBJ` 场景转换为可运行时加载的 `.navmesh`
- 对比客户端路径、服务端路径和烘焙结果
- 在接入业务前先做最小运行时查询验证
- 对 `.navmesh` 做可视化、结构分析和格式迁移

## 说明

- 仓库中保留了部分历史工程文件，例如 `Makefile`、Visual Studio 工程文件等，当前推荐以 `CMake` 为主
- `Tile/tmxparser` 路线已从当前构建主线移除，相关历史说明请以 `docs/project_modules.md` 中的备注为准
- `build/` 是本地构建目录，不建议提交编译产物；仓库内保留的 `output_101_navmesh/` 属于示例结果

## License

仓库中保留了上游与历史代码使用的 [License.txt](License.txt)；如需对外分发或二次集成，建议结合实际来源再做一次许可证核对。
