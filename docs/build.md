# 构建编译说明  add by gexiannan

本文档整理当前 `navigation` 工程在 Linux 环境下的构建方式，重点覆盖：

- 构建前依赖
- 推荐的 CMake 构建流程
- `Debug + -O0` 的调试构建方式
- 产物位置
- 常见报错与排查建议

本文档基于当前仓库代码实际编译结果整理。

## 0. 命令速查

当前工程默认使用 `Debug` 构建，方便直接进入 VS Code / `gdb` 调试。如果只想快速构建当前工程，直接执行：

```bash
cd /mnt/d/navigation
cmake -S . -B build
cmake --build build -j4
```

如果要确保断点行为最稳定，建议清理一次 `build/`，再显式指定 `Debug + -O0`：

```bash
cd /mnt/d/navigation
rm -rf build
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS_DEBUG="-O0 -g" \
  -DCMAKE_CXX_FLAGS_DEBUG="-O0 -g"
cmake --build build -j4
```

这样做的目的主要是：

- 让 `F5` 默认就能命中 `build/` 下的调试产物
- 使用 `Debug` 配置，方便断点、单步和变量观察
- 显式指定 `-O0`，避免优化把源码行折叠掉，导致某些行下不了断点
- 先清理 `build/`，避免沿用旧缓存

如果分开看，就是：

### 0.1 配置工程

```bash
cmake -S . -B build
```

### 0.2 开始编译

```bash
cmake --build build -j4
```

### 0.3 编译完成后的主要产物

```bash
build/CMakeCache.txt
build/CMakeFiles/
build/Makefile
build/cmake_install.cmake
build/grid_map_demo
build/navmesh_bake_from_obj
build/navmesh_dump
build/navmesh_export_json
build/navmesh_query_demo
build/navigation_load_by_name_demo
/mnt/d/navigation/Bin/lib/libnavigation.so
```

如果是调试构建，产物路径仍然在 `build/` 下，只是编译参数会切换为 `Debug + -O0 + -g`：

```bash
build/CMakeCache.txt
build/CMakeFiles/
build/Makefile
build/cmake_install.cmake
build/grid_map_demo
build/navmesh_bake_from_obj
build/navmesh_dump
build/navmesh_export_json
build/navmesh_query_demo
build/navigation_load_by_name_demo
/mnt/d/navigation/Bin/lib/libnavigation.so
```

这些产物的用途分别是：

- `build/CMakeCache.txt`
  保存本次 CMake 配置的缓存结果。
  里面会记录编译器、检测到的依赖、构建选项和输出路径，适合排查“为什么这次配置和上次不一样”。

- `build/CMakeFiles/`
  保存 CMake 生成的中间文件、对象文件、依赖关系和目标构建脚本。
  这是构建系统内部工作目录，通常不手工编辑，但排查增量编译或链接问题时会用到。

- `build/Makefile`
  这是 CMake 生成的顶层构建入口。
  执行 `cmake --build build` 时，本质上会驱动这里对应的底层构建系统。

- `build/cmake_install.cmake`
  这是 CMake 自动生成的安装脚本。
  当前项目主要用它描述 `install` 阶段规则，日常开发通常不会直接手动执行。

- `build/grid_map_demo`
  用来检查 `.map` 栅格地图文件内容。
  它会读取地图宽高、统计格子数量，并输出可走/阻挡数量和字节值分布。

- `build/navmesh_bake_from_obj`
  用来把 `.obj` 场景网格烘焙成当前工程可加载的 `.navmesh`。
  同时还能导出一份顶视图 `.svg`，方便肉眼检查烘焙结果。

- `build/navmesh_dump`
  用来检查 `.navmesh` 文件结构。
  它会解析项目自定义的 navmesh 外层封装，并打印每个 tile 的 `Detour` 头信息摘要。

- `build/navmesh_export_json`
  用来把 `.navmesh` 导出成便于阅读和二次处理的明文 `json`。
  输出内容包含 tile、polygon、顶点、中心点、`flags`、`area` 等结构化信息。

- `build/navmesh_query_demo`
  用来直接在 `.navmesh` 上跑一组最小查询验证。
  它会测试 `findPath`、`findStraightPath`、`moveAlongSurface`、`raycast`、`findDistanceToWall` 等运行时接口是否正常。

- `build/navigation_load_by_name_demo`
  用来按场景名验证运行时加载链路。
  默认会尝试加载当前仓库中的 `res/101_nav.navmesh`。

- `/mnt/d/navigation/Bin/lib/libnavigation.so`
  这是当前工程真正的导航运行时库。
  上层程序如果要接入这个导航系统，主要依赖的就是这个库。

## 1. 工程产物

当前工程会生成两类构建产物：

### 1.1 CMake 构建系统文件

这些文件主要出现在 `build/` 目录下，用于驱动和记录本次构建：

- `CMakeCache.txt`
  CMake 配置缓存。

- `CMakeFiles/`
  CMake 中间目录，里面包含对象文件、依赖信息、链接脚本等。

- `Makefile`
  当前生成器对应的顶层构建文件。

- `cmake_install.cmake`
  `install` 阶段脚本。

### 1.2 业务相关产物

当前工程实际会构建出 7 个和导航业务直接相关的产物：

- `navigation` 动态库
- `grid_map_demo` 工具
- `navmesh_dump` 工具
- `navmesh_bake_from_obj` 工具
- `navmesh_query_demo` 工具
- `navmesh_export_json` 工具
- `navigation_load_by_name_demo` 工具

其中：

- Linux 下 `navigation` 默认构建为共享库 `libnavigation.so`
- Windows 下 `navigation` 默认构建为静态库

对应逻辑见 [CMakeLists.txt](/d:/navigation/CMakeLists.txt#L84)。

## 2. 构建前置条件

### 2.1 编译器和工具

推荐准备以下工具：

- `cmake >= 3.28`
- `g++` 或 `clang++`
- `make` 或 `ninja`

本次验证使用的是：

- `cmake 4.2.3`
- `GNU C/C++ 13.3.0`

### 2.2 代码目录依赖

当前工程的所有源码均位于仓库内，无需额外准备仓库外的依赖目录。

## 3. 推荐构建方式

推荐优先使用 CMake，而不是仓库里的旧 `Makefile`。

原因是：

- `CMakeLists.txt` 是当前最直接的构建入口
- 旧 `Makefile` 依赖外部 `KBE_ROOT` 和 `common.mak`
- CMake 更适合后续替换 `Recast/Detour` 后继续迭代

### 3.1 在仓库根目录执行

```bash
cd /mnt/d/navigation
cmake -S . -B build
cmake --build build -j4
```

如果你机器核数更多，可以把 `-j4` 调大。当前默认构建类型已经是 `Debug`。

### 3.2 已验证可行的结果

上述命令执行成功后，当前工程可以完成以下构建：

- `build/grid_map_demo`
- `build/navmesh_bake_from_obj`
- `build/navmesh_dump`
- `build/navmesh_export_json`
- `build/navmesh_query_demo`
- `build/navigation_load_by_name_demo`
- `/mnt/d/navigation/Bin/lib/libnavigation.so`

### 3.3 用于断点调试的 Debug 构建

如果目标是进入 `main`、在 `Navigation::loadNavigation()` 里单步，或者排查“某一行打不上断点”的问题，推荐直接把 `build/` 目录切成调试构建：

```bash
cd /mnt/d/navigation
rm -rf build
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS_DEBUG="-O0 -g" \
  -DCMAKE_CXX_FLAGS_DEBUG="-O0 -g"
cmake --build build -j4
```

调试构建完成后，通常直接使用这些产物：

- `build/navigation_load_by_name_demo`
- `build/navmesh_query_demo`
- `build/navmesh_dump`

如果要在 VS Code 里调试 `navigation_load_by_name_demo`，`launch.json` 里的 `program` 建议指向：

```json
"${workspaceFolder}/build/navigation_load_by_name_demo"
```

### 3.4 当前默认行为

当前 [CMakeLists.txt](/d:/navigation/CMakeLists.txt#L4) 已经改成“未显式指定时默认使用 `Debug`”。另外，`Debug` 配置还会额外附加 `-O0 -g`，让断点和单步更稳定。

如果你想确认当前 `build/` 目录确实已经是调试参数，可以检查：

```bash
sed -n '1,120p' build/CMakeFiles/navigation.dir/flags.make
```

理想情况下应该看到接近下面这种结果：

```text
-O0 -g
```

理想情况下应该看到接近下面这种结果：

```text
-g -O0
```

如果仍然看到 `-O3 -DNDEBUG`，通常说明 `build/` 里还残留旧缓存。此时先删除 `build/`，再重新执行一次配置和编译即可。

## 4. 默认输出位置

### 4.1 工具程序

6 个工具程序默认输出在构建目录里：

- `build/grid_map_demo`
- `build/navmesh_bake_from_obj`
- `build/navmesh_dump`
- `build/navmesh_export_json`
- `build/navmesh_query_demo`
- `build/navigation_load_by_name_demo`

它们的作用分别是：

- `grid_map_demo`
  检查 `.map` 栅格地图文件是否格式正确，并输出基础统计信息。

- `navmesh_bake_from_obj`
  把 `.obj` 几何烘焙成 `.navmesh`，并可附带导出 `.svg` 俯视图。
  适合在没有上层游戏流程参与时，单独验证原始几何是否能正确生成导航网格。

- `navmesh_dump`
  检查 `.navmesh` 文件是否格式正确，并输出 navmesh header、tile header、Detour tile 摘要。

- `navmesh_export_json`
  把 `.navmesh` 转成明文 `json`，适合做结构化检查、调试或后续分析脚本输入。

- `navmesh_query_demo`
  在 `.navmesh` 上直接执行一组 Detour 查询，适合验证运行时寻路接口是否工作正常。

- `navigation_load_by_name_demo`
  直接验证 `Navigation::loadNavigation(name)` 是否能成功加载指定场景名。
  默认目标是 `101_nav`，适合做 `res/101_nav.navmesh` 的最小加载测试。
- test by gexiannan
  ./build/navigation_load_by_name_demo   101_nav   119.11 4.15472 24.4367   175.11 10.5147 357.017   output_101_navmesh/101_nav_path3.txt

### 4.2 导航库

导航库默认输出到仓库内的路径：

```text
Bin/lib
```

在当前工作区下实际展开为：

```text
/mnt/d/navigation/Bin/lib/libnavigation.so
```

这个行为来自 [CMakeLists.txt](/d:/navigation/CMakeLists.txt#L20)：

```cmake
set(NAVIGATION_LIB_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Bin/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${NAVIGATION_LIB_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${NAVIGATION_LIB_OUTPUT_DIR}")
```

这意味着：

- 在本机正常开发环境里通常没有问题
- 如果仓库目录本身不可写，可能在最后链接阶段失败

这个库的作用是：

- 提供 `Navigation` 统一入口
- 加载 `.map`、`.navmesh`
- 提供寻路、射线、高度、最近点等运行时查询能力

## 5. 实际构建步骤说明

### 5.1 配置阶段

```bash
cmake -S . -B build
```

这一步会：

- 检测 C/C++ 编译器
- 生成 `build/` 下的构建文件

如果成功，会看到类似输出：

```text
-- Configuring done
-- Generating done
-- Build files have been written to: /mnt/d/navigation/build
```

### 5.2 编译阶段

```bash
cmake --build build -j4
```

这一步会先编译：

- `grid_map_demo`
- `navmesh_dump`
- `navmesh_bake_from_obj`
- `navmesh_query_demo`
- `navmesh_export_json`
- `navigation` 相关源码

最后再链接：

- `libnavigation.so`

## 6. 常见问题

### 6.1 最后链接 `libnavigation.so` 失败

典型表现：

```text
/usr/bin/ld: cannot open output file /mnt/d/navigation/Bin/lib/libnavigation.so: Read-only file system
```

这不是源码编译错误，而是输出目录不可写。

原因：

- 当前 CMake 把库输出路径固定到仓库内的 `Bin/lib`
- 如果 `navigation/Bin/lib` 不可创建或不可写，链接器无法写入 `libnavigation.so`

处理方式：

- 确认当前仓库目录可写
- 或者修改 [CMakeLists.txt](/d:/navigation/CMakeLists.txt#L20)，把库输出目录改到其他可写位置

例如可以改成类似：

```cmake
set(NAVIGATION_LIB_OUTPUT_DIR "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${NAVIGATION_LIB_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${NAVIGATION_LIB_OUTPUT_DIR}")
```

如果后续要做大版本升级，建议继续使用基于变量的绝对路径，避免构建结果受执行目录影响。

### 6.3 旧代码在新编译器上的弃用警告

当前工程在较新的 GCC 上可以编过，但会看到一些警告，例如：

- `strutil.cpp` 使用了已弃用的 `std::ptr_fun` / `std::not1`

这些警告目前不会阻止构建，但说明工程整体比较旧。

如果后面要升级 `recastnavigation`，建议把这些非核心警告也一起逐步清理。

### 6.4 `clock skew detected`

可能看到：

```text
gmake: warning: Clock skew detected. Your build may be incomplete.
```

这通常与文件时间戳或挂载盘时间不同步有关，不一定代表编译失败。

如果最终目标都成功生成，一般可以先忽略；如果反复出现，建议检查：

- 宿主机与 WSL 时间同步
- 挂载盘文件时间戳是否异常

### 6.5 某些源码行无法下断点

典型表现：

- `gdb` 或 VS Code 可以启动
- 但某一行看起来有代码，断点却落不上去，或者自动跳到下一行

常见原因：

- 当前是优化构建，比如 `-O2` 或 `-O3`
- 该行代码被编译器折叠、合并或重排后，没有独立的机器指令地址

建议处理方式：

- 改用本文档里的 `build/` 调试构建，并确认参数是 `-O0 -g`
- 确认编译参数里确实是 `-O0 -g`
- 必要时先断在函数入口或相邻的稳定可执行行，再单步进入

## 7. 运行工具程序

### 7.1 查看 `.map` 文件

```bash
./build/grid_map_demo <file.map>
```

作用：

- 打印地图宽高
- 统计格子数
- 统计可走/阻挡数量
- 输出字节值直方图

### 7.2 查看 `.navmesh` 文件头和 tile 摘要

```bash
./build/navmesh_dump <file.navmesh>
```

作用：

- 读取项目自定义 `.navmesh` 外层封装
- 打印 `NavMeshSetHeader`
- 打印每个 tile 的 `Detour dtMeshHeader` 摘要

### 7.3 从 `.obj` 烘焙生成 `.navmesh`

```bash
./build/navmesh_bake_from_obj <input.obj> <output.navmesh> <output.svg>
```

例如：

```bash
./build/navmesh_bake_from_obj \
  ./101.obj \
  output_101_navmesh/101_test.navmesh \
  output_101_navmesh/101_test.svg
```

作用：

- 读取三角面片 `.obj`
- 用当前工程内置的 `Recast/Detour` 参数生成 `.navmesh`
- 同时输出一份俯视图 `.svg` 用来快速检查结果

`navmesh_bake_from_obj` 现在支持完整命令行参数覆盖，执行：

```bash
./build/navmesh_bake_from_obj --help
```

可以查看以下可调项：

- `--cell-size`
- `--cell-height`
- `--agent-height`
- `--agent-radius`
- `--agent-max-climb`
- `--agent-max-slope`
- `--region-min-size`
- `--region-merge-size`
- `--edge-max-len`
- `--edge-max-error`
- `--verts-per-poly`
- `--detail-sample-dist`
- `--detail-sample-max-error`

如果目标是**和在线工具 `https://navmesh.kbelab.com/` 烘焙出的 navmesh 对齐**（即保留所有小障碍物/树/小台阶、细粒度还原场景），应使用 KBEngine/Recast 官方默认参数，而不是自定义的“整块化”参数。推荐命令如下：

```bash
./build/navmesh_bake_from_obj \
  ./101.obj \
  output_101_navmesh/101_web_like.navmesh \
  output_101_navmesh/101_web_like.svg \
  --cell-size 0.2 \
  --cell-height 0.1 \
  --agent-height 2.0 \
  --agent-radius 0.4 \
  --agent-max-climb 1.2 \
  --agent-max-slope 50 \
  --region-min-size 4 \
  --region-merge-size 12 \
  --edge-max-len 12 \
  --edge-max-error 1.1 \
  --verts-per-poly 6 \
  --detail-sample-dist 4 \
  --detail-sample-max-error 0.8
```

# test by gexiannan:

./101.obj   output_101_navmesh/101_web_like.navmesh   output_101_navmesh/101_web_like.svg   --cell-size 0.08   --cell-height 0.48   --agent-height 2.0   --agent-radius 0.01   --agent-max-climb 1.2   --agent-max-slope 90   --region-min-size 4   --region-merge-size 12   --edge-max-len 12   --edge-max-error 1.1   --verts-per-poly 6   --de
tail-sample-dist 4   --detail-sample-max-error 0.8


# test pass
./build/navmesh_bake_from_obj   ./101.obj   output_101_navmesh/101_web_like.navmesh   output_101_navmesh/101_web_like.svg   --cell-size 0.08   --cell-height 0.48   --agent-height 2.0   --agent-radius 0.01   --agent-max-climb 1.2   --agent-max-slope 90   --region-min-size 4   --region-merge-size 12   --edge-max-len 12   --edge-max-error 1.1   --verts-per-poly 6   --de
tail-sample-dist 4   --detail-sample-max-error 0.8
Bake settings
  cellSize=0.08
  cellHeight=0.48
  agentHeight=2
  agentRadius=0.01
  agentMaxClimb=1.2
  agentMaxSlope=90
  regionMinSize=4
  regionMergeSize=12
  edgeMaxLen=12
  edgeMaxError=1.1
  vertsPerPoly=6
  detailSampleDist=4
  detailSampleMaxError=0.8
Loaded OBJ: ./101.obj
  vertices=20719
  triangles=9235
Built navmesh
  tiles=1
  walkablePolys=6192
  walkableHeightVoxels=5
  walkableClimbVoxels=2
  walkableRadiusVoxels=1
Navmesh: output_101_navmesh/101_web_like.navmesh

参数选取依据：

- `cell-size=0.3 / cell-height=0.2`：Recast 体素化默认分辨率，保留细节。之前 `0.4` 会让每个体素覆盖更大地面，细小障碍物直接被跨过。
- `agent-radius=0.4 / agent-height=2.0`：Recast 标准 agent 尺寸，腐蚀不过猛，通道能保留。之前 `0.5` 会多腐蚀一圈，窄通道和小台阶会消失。
- `agent-max-climb=0.9`：默认台阶高度。之前 `0.4` 会把大量矮台阶判为“不可逾越”，直接吃掉小平台。
- `region-min-size=8 / region-merge-size=20`：默认区域阈值。之前 `16 / 40` 会把单棵树、单个小障碍物对应的小区域融成整片，是前面 SVG 看起来“糊”的主要原因。
- `edge-max-len=12 / edge-max-error=1.3`：默认轮廓简化阈值。之前 `20 / 2.5` 会把弯曲边界拉成近似直线，丢失多边形细节。
- `agent-max-slope=45`：与 `101.obj` 的地形坡度匹配；在线工具常用 45，不必改成 30，否则缓坡会被判为不可走。

补充说明：

- `navmesh.kbelab.com` 内部使用的就是 Recast `BuildSettings` 默认值。用上述这一组参数，理论上能和网页烘焙结果在多边形结构上对齐（像素级完全一致还受 `.obj` 三角化顺序、浮点累积差异、以及 Recast 版本的影响）。
- 之前 build.md 里名为 `101_web_like` 的参数集**并不是“像网页版”的意思**，而是某次观感调参试出来的“更整块”版本，实际上比默认参数**粒度更粗**，所以和网页版截图看起来差距很大。如果后续要“整块化”效果，请换个名字（比如 `101_blocky`），避免和“对齐网页版”这个目标混淆。
- 当前工程里 `output_101_navmesh/` 下还保留着几组历史对比：
  - `101_client_param.svg`
  - `101_tune_a.svg` ～ `101_tune_e.svg`
- 想做 3D 对比（和 kbelab 的 3D 视图直观比对），可用工程内置的 `navmesh_export_obj` 把生成的 `.navmesh` 再导出成 `.obj`，然后用 Blender / MeshLab / 任意网页 3D 查看器打开：

```bash
./build/navmesh_export_obj \
  output_101_navmesh/101_web_like.navmesh \
  output_101_navmesh/101_web_like.obj
```

### 7.4 导出 `.navmesh` 为 `json`

```bash
./build/navmesh_export_json <input.navmesh> <output.json>
```

作用：

- 读取 `.navmesh`
- 导出 tile、polygon、顶点、中心点、`flags`、`area` 等可读结构
- 方便人工检查或交给其他脚本继续处理

### 7.5 运行时查询示例

```bash
./build/navmesh_query_demo <input.navmesh>
```

作用：

- 加载 `.navmesh`
- 直接执行最小查询验证
- 用来检查 `findPath`、`findStraightPath`、`moveAlongSurface`、`raycast`、`findDistanceToWall` 等接口是否正常

## 8. 后续升级建议

如果你后面准备把当前工程中内置的老版本 `Recast/Detour` 替换成上游新版，建议先以本文档记录的“当前可编译状态”作为基线：

1. 先保留当前工程可构建状态
2. 再替换 `Recast/Detour` 源码
3. 再比较升级前后：
   - CMake 是否还能通过
   - `navigation` 是否还能链接
   - `navmesh_dump` 是否还能正常读取旧数据
   - `.navmesh` 文件格式是否保持兼容

这样可以把“源码升级问题”和“原工程本身就不能编”的问题分开处理。
### 7.6 客户端参数说明

如果后续不是走命令行，而是让客户端把烘焙请求结构化传给服务端，那么可以等价表示为：

```json
{
  "inputObjPath": "101.obj",
  "outputNavmeshPath": "101.navmesh",
  "outputSvgPath": "101.svg",
  "settings": {
    "cellSize": 0.3,
    "cellHeight": 0.2,
    "agentHeight": 2.0,
    "agentRadius": 0.5,
    "agentMaxClimb": 0.4,
    "agentMaxSlope": 45.0,
    "regionMinSize": 8,
    "regionMergeSize": 20,
    "edgeMaxLen": 12,
    "edgeMaxError": 1.3,
    "vertsPerPoly": 6,
    "detailSampleDist": 6,
    "detailSampleMaxError": 1.0
  }
}
```

字段说明：

- `inputObjPath`
  输入的场景几何文件路径。`.obj` 里存的是顶点 `xyz` 和三角面，也就是场景原始几何。
- `outputNavmeshPath`
  输出的 `.navmesh` 文件路径。这是当前工程运行时真正加载的导航网格文件。
- `outputSvgPath`
  输出的俯视图 `.svg` 路径。只用于肉眼检查烘焙结果，不参与运行时寻路。
- `settings`
  烘焙参数集合，决定“按什么规则把 `.obj` 烘成 navmesh”。

`settings` 各字段含义如下：

- `cellSize`
  XZ 平面的体素尺寸。越小，采样越细，细通道和边角越容易保留下来，但烘焙更慢。
- `cellHeight`
  Y 方向体素高度。越小，高度采样越细，台阶、坡坎、高低差判断更精确，但烘焙更慢。
- `agentHeight`
  代理高度。低于这个净空的区域会被判成不可走，对应客户端的 `Agent Height`。
- `agentRadius`
  代理半径。可走区会按这个半径向内腐蚀一圈，对应客户端的 `Agent Radius`。值越大，白色不可走边界通常越多。
- `agentMaxClimb`
  最大可跨越台阶高度，对应客户端的 `Step Height`。值越大，小台阶、小坎更容易被判成可通过。
- `agentMaxSlope`
  最大可走坡度，对应客户端的 `Max Slope`。例如 `45` 表示坡度大于 45 度的面会被判成不可走。
- `regionMinSize`
  最小孤立区域阈值。小于这个阈值的孤立小块区域会被清理掉。值越大，零碎小岛越容易消失。
- `regionMergeSize`
  小区域合并阈值。小于这个阈值的区域会尽量合并进邻居。值越大，结果越容易变得整块化。
- `edgeMaxLen`
  轮廓边最大长度阈值。过长的边会被拆分，用于避免生成过长过瘦的多边形。
- `edgeMaxError`
  轮廓简化误差。值越大，边界越容易被简化得更直、更粗；值越小，边界更贴近原始几何。
- `vertsPerPoly`
  单个导航多边形允许的最大顶点数。当前工程通常保持 `6`，一般不需要频繁调整。
- `detailSampleDist`
  细节网格采样距离。值越小，细节表面越贴原始高度场；值越大，细节更粗。
- `detailSampleMaxError`
  细节网格允许的最大高度误差。值越小，细节网格越贴近原始表面；值越大，细节更容易被简化。

补充说明：

- `.obj` 负责提供原始几何；`settings` 负责提供烘焙规则，这两者要分开理解。
- 同一类地图、同一类角色时，`settings` 往往可以作为默认模板长期复用，不必每张图都重配一套。
- 真正参与 Recast 烘焙时，部分参数还会转成体素单位：
  - `walkableHeight = ceil(agentHeight / cellHeight)`
  - `walkableClimb = floor(agentMaxClimb / cellHeight)`
  - `walkableRadius = ceil(agentRadius / cellSize)`
  所以客户端只需要传原始世界单位参数，不要自己提前换算。
