# 构建说明

## 快速构建

```bash
cd /mnt/d/e996_navmesh
cmake -S . -B build
cmake --build build -j4
```

默认构建类型是 `Debug`。`CMakeLists.txt` 在没有显式传入 `CMAKE_BUILD_TYPE` 时会设置：

```text
CMAKE_BUILD_TYPE=Debug
```

并且 Debug 会使用 `-O0 -g`，适合调试，不适合做性能压测。

也可以直接使用仓库里的脚本：

```bash
cd /mnt/d/e996_navmesh
./build.sh
```

`build.sh` 默认也是 Debug，因为它没有传 `-DCMAKE_BUILD_TYPE=Release`。

## Release 构建

做性能测试时建议单独建一个 Release 构建目录：

```bash
cd /mnt/d/e996_navmesh
cmake -S . -B build_perf_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_perf_release --parallel
```

只编译压测工具：

```bash
cmake --build build_perf_release --target navmesh_perf_bench --parallel
```

如果要让 `build.sh` 构建 Release，需要把脚本里的 CMake 配置命令改成类似：

```bash
cmake --fresh -S "${ROOT_DIR}" -B "${ROOT_DIR}/${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
```

## 主要产物

```text
build/navmesh_bake_from_obj
build/navmesh_dump
build/navmesh_query_demo
build/navmesh_perf_bench
build/navmesh_export_json
build/navmesh_export_obj
build/navmesh_path_compare
build/navmesh_triangle_path_demo
build/navmesh_smooth_path_demo
build/navigation_load_by_name_demo
Bin/lib/libnavigation.so
```

Release 构建目录对应产物示例：

```text
build_perf_release/navmesh_perf_bench
build_perf_release/navmesh_dump
```

## 工具用途

- `navmesh_bake_from_obj`：从 OBJ 场景几何生成 `.navmesh`，可选输出 SVG 顶视图。
- `navmesh_dump`：读取 `.navmesh` 并输出文件头和 tile 摘要。
- `navmesh_query_demo`：加载 `.navmesh` 并执行最小运行时查询。
- `navmesh_perf_bench`：批量执行寻路查询，用于粗略压测寻路性能。
- `navmesh_export_json`：导出可读的多边形 JSON。
- `navmesh_export_obj`：导出导航网格细节三角面 OBJ。
- `navmesh_path_compare`：对比路径结果。
- `navmesh_triangle_path_demo`：观察多边形或三角形路径行为。
- `navmesh_smooth_path_demo`：检查平滑路径效果。
- `navigation_load_by_name_demo`：验证 `Navigation::loadNavigation(name)` 加载本地资源。

## 常用验证

```bash
./build/navmesh_dump output_101_navmesh/101_web_like.navmesh
./build/navmesh_query_demo output_101_navmesh/101_web_like.navmesh
./build/navigation_load_by_name_demo 101_nav 0 0 0 10 0 10 output_101_navmesh/path.txt
```

## 寻路性能压测

建议使用 Release 版本运行：

```bash
cd /mnt/d/e996_navmesh
/mnt/d/e996_navmesh/build_perf_release/navmesh_perf_bench \
  /mnt/d/e996_navmesh/output_101_navmesh/101_web_like.navmesh \
  10000
```

参数含义：

```text
第 1 个参数：要测试的 .navmesh 文件
第 2 个参数：循环次数，可不填，默认 10000
```

`navmesh_perf_bench` 会自动从 navmesh 的可走 polygon 中选取起点和终点，重复执行完整寻路流程：

```text
findNearestPoly(start)
findNearestPoly(end)
findPath()
findStraightPath()
```

这个工具适合做整体粗测。如果要测试真实业务性能，建议从服务器日志中采样真实起点和终点，再做 P50/P95/P99 统计。

### 示例结果

测试命令：

```bash
/mnt/d/e996_navmesh/build_perf_release/navmesh_perf_bench \
  /mnt/d/e996_navmesh/output_101_navmesh/101_web_like.navmesh \
  10000
```

输出示例：

```text
File: /mnt/d/e996_navmesh/output_101_navmesh/101_web_like.navmesh
Walkable polygons: 6192
Iterations: 10000
Completed paths: 10000
Nearest failures: 0
Empty paths: 0
Total time: 5731.574 ms
Average query: 573.157 us
Average corridor polys: 77.419
Average straight points: 14.936
```

结果说明：

```text
Average query: 573.157 us
```

表示平均一次完整寻路约 `0.573 ms`，约等于单线程每秒 `1744` 次查询。这个结果包含起点/终点吸附、A* 寻路和直线路径生成，不只是 `findPath()`。

```text
Average corridor polys: 77.419
```

表示平均每条路径经过约 `77` 个导航多边形。

```text
Average straight points: 14.936
```

表示最终输出的直线路径平均约 `15` 个点。

这个结果是当前机器、当前构建、当前 navmesh 和当前测试点分布下的结果，只能作为参考。服务端正式评估时更应该关注真实请求分布下的 P95/P99 和峰值耗时。

## 清理重建

Debug 重建：

```bash
rm -rf build
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS_DEBUG="-O0 -g" \
  -DCMAKE_CXX_FLAGS_DEBUG="-O0 -g"
cmake --build build -j4
```

Release 重建：

```bash
rm -rf build_perf_release
cmake -S . -B build_perf_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_perf_release --parallel
```

## 说明

- 当前构建只包含 3D `NavMesh` 路线。
- 二维瓦片和二维栅格相关目标已经从构建入口移除。
- Windows 下仍保留 Visual Studio 工程文件，但当前推荐以 CMake 为准。
- 性能测试不要使用默认 Debug 构建，必须显式使用 `-DCMAKE_BUILD_TYPE=Release`。
