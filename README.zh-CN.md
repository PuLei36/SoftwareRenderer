# 软光栅渲染器

[English](README.md) | **简体中文**

一个用 C89 从零实现的着色器式软光栅渲染器，不依赖任何图形 API，全部渲染在 CPU 上完成，支持 Windows、macOS 和 Linux。在此基础上实现了基于 Tile 分块的多线程光栅化。

本科毕业设计（中南林业科技大学 计算机科学与技术，2026）。

## 功能

**渲染管线**

- 可编程管线：顶点着色器、片元着色器、通用 varyings 插值
- 齐次空间裁剪（Sutherland-Hodgman，7 个平面）
- 背面剔除、透视校正插值、Early-Z 深度测试
- Alpha 测试（discard）与 Alpha 混合
- Cubemap 天空盒

**着色与材质**

- Blinn-Phong 光照模型
- PBR：Cook-Torrance BRDF（GGX + 高度相关 Smith + Schlick）
- 两套工作流：金属度/粗糙度、高光/光泽度
- IBL：漫反射 irradiance + 分裂求和近似的镜面预过滤 + BRDF LUT
- 切线空间法线贴图、环境光遮蔽、自发光
- Shadow Mapping（斜率缩放 bias）
- ACES 色调映射与 gamma 校正

**其他**

- 骨骼动画：线性混合蒙皮、四元数 slerp 插值
- 轨道相机（旋转、平移、缩放）
- 资源加载：OBJ 网格、TGA 与 Radiance HDR 图像、文本格式的场景与动画文件
- 引用计数的资源缓存
- PBR 材质分层查看器
- Tile-Based 多线程光栅化与跨平台线程池

## 渲染管线

`graphics_draw_triangle` 处理一个三角形，依次经过以下阶段：

**1. 顶点着色**　调用顶点着色器，返回裁剪空间坐标，同时写出 varyings。

**2. 齐次裁剪**　先用 `|x|,|y|,|z| <= w` 快速判断三角形是否完全可见，可见则直接跳过裁剪。否则按 Sutherland-Hodgman 依次对 7 个平面裁剪：`w >= EPSILON` 以及 `±x`、`±y`、`±z`。w 平面留出 1e-5 的余量，避免后面除以接近 0 或负数的 w。裁剪在透视除法之前进行，因此顶点坐标和 varyings 都可以用同一个比例直接线性插值。每个平面最多增加一个顶点，所以多边形顶点数上限为 3+7=10。

**3. 三角形组装**　裁剪得到的多边形按 `(0, i+1, i+2)` 拆成三角形扇。

**4. 图元装配**　逐个三角形做透视除法得到 NDC 坐标，判断朝向，保存每个顶点的 `1/w`，做视口变换，求屏幕空间包围盒。

朝向在 NDC 空间用二维有向面积判断，逆时针为正面（OpenGL 约定）。背面在 `double_sided` 为假时剔除，为真时把 backface 标记传给片元着色器，由着色器翻转法线。天空盒就是靠 `double_sided` 保证不被剔除。

**5. 光栅化**　在包围盒内逐像素扫描，取像素中心 `(x+0.5, y+0.5)` 作为采样点，解 `AP = s·AB + t·AC` 求重心坐标，三个权重都大于 `-EPSILON` 才算覆盖。

**6. 深度测试**　深度值是窗口空间 z，即 `(NDC z + 1) / 2`，范围 [0,1]，越小越近，缓冲清空为 1。NDC z 在屏幕空间是线性的，所以深度直接用重心坐标插值，不需要透视校正。深度测试在片元着色器之前执行（Early-Z），被 discard 的片元不写深度。

**7. varyings 插值**　透视校正插值的实现是：用每个顶点的 `1/w` 乘上对应的重心权重，归一化后作为实际插值系数。varyings 块被当作 float 数组统一处理，着色器各自转换成自己的结构体，所以管线不需要知道 varyings 的具体布局。

**8. 片元着色与输出**　运行片元着色器（可以 discard），结果 saturate 到 [0,1]，按需做 Alpha 混合 `src·a + dst·(1-a)`，写入 8 位颜色缓冲和浮点深度缓冲。

### 帧的绘制顺序

每帧先更新模型和骨骼动画，然后：

1. **阴影 pass**（若场景开启）：按光源视角距离排序，只绘制不透明模型写入深度，再拷贝为 shadow map 纹理。
2. **主 pass**：按相机距离排序，先不透明模型（由近到远，利用 Early-Z 减少着色），再天空盒，最后透明模型（由远到近）。

天空盒通过把 `clip.z` 强制设为 `w·(1-EPSILON)` 压到远平面，配合 `<=` 的深度测试，只填充不透明物体没有覆盖的像素。

## 着色

### Blinn-Phong

最终颜色是自发光、环境光和直接光三项之和：

```
emission + diffuse · ambient_intensity
         + [diffuse · (n·l) + specular · (n·h)^shininess] · punctual_intensity
```

环境光是漫反射颜色乘一个常量强度。直接光来自一个方向光，只在 `n·l > 0` 且不在阴影中时累加，高光用 Blinn 半程向量 `normalize(l+v)`，有高光贴图时才计算。Blinn-Phong 路径的贴图按 LDR 颜色加载，不做线性化，光照直接在 gamma 空间计算，输出也不做色调映射。

### PBR

直接光用 Cook-Torrance 形式的微表面 BRDF：

- **D**　GGX / Trowbridge-Reitz 法线分布，`alpha = roughness²`
- **V**　高度相关的 Smith-GGX 可见性项，已经包含 `1/(4·n·l·n·v)`，所以镜面项是 `F·V·D`
- **F**　Schlick 近似，`F90` 由 F0 的最大分量放大后饱和得到

合并为 `((1-F)·diffuse/π + F·V·D) · (n·l)`，仅在 `n·l > 0` 且 `n·v > 0` 时累加。

**两套工作流**

金属度/粗糙度：`diffuse = basecolor · 0.96 · (1-metalness)`，`F0 = lerp(0.04, basecolor, metalness)`。高光/光泽度：高光是 RGB 贴图，`diffuse` 按高光最大分量压低以保证能量守恒，`roughness = 1 - glossiness`。

**IBL**

- **漫反射**　采样预先烘焙的 irradiance cubemap，乘以漫反射颜色和 AO。
- **镜面**　分裂求和近似。反射方向 `2(n·v)n - v`，按 `roughness` 从 10 级预过滤 cubemap 中选取最接近的一级（不做级间混合），再乘以 BRDF LUT 在 `(n·v, roughness)` 处取得的 scale 与 bias。

环境贴图由 Filament 的 `cmgen` 工具离线生成，脚本在 `scripts/utils/cmgen.py`。

**法线贴图**　顶点着色器输出世界空间的 T、B、N，其中 `bitangent = cross(N, T) · tangent.w`；片元着色器把切线空间法线用 TBN 矩阵变换到世界空间。

**色调映射**　片元着色器最后对每个通道做 ACES 拟合（Narkowicz）再做 `pow(1/2.2)` 的 gamma 编码。PBR 路径的颜色贴图在加载时就线性化，数据贴图（金属度、粗糙度、法线、AO）保持原值。

### Shadow Mapping

光源使用正交投影，朝 `-light_dir` 看向原点，各场景的根变换把模型缩放到单位球附近以匹配这个投影范围。默认分辨率 512×512。阴影 pass 的片元着色器只做 alpha cutoff，实际只写深度。

采样时比较当前深度与 shadow map 深度，bias 按斜率缩放：`max(0.05·(1 - n·l), 0.005)`。阴影只遮挡直接光，不影响 IBL。单次采样，硬边。

## 骨骼动画

动画数据是文本格式的 `.ani` 文件，包含每个关节的父节点索引、逆绑定矩阵，以及平移、旋转、缩放三条关键帧轨道。平移和缩放用线性插值，旋转用四元数 slerp（自动取短弧）。

每帧按 `fmod(t, max_time)` 循环，先合成局部变换，再沿父子关系累乘得到全局变换，最后乘逆绑定矩阵得到关节矩阵。顶点带 4 组关节索引和权重，顶点着色器按权重线性混合这 4 个矩阵（线性混合蒙皮），法线使用单独的逆转置矩阵。模型也可以刚性挂接到某个关节上，用于节点动画。

## 多线程光栅化

软光栅的主要开销在片元阶段，但不同三角形可能覆盖同一像素，多线程直接写颜色和深度缓冲会产生竞争，而逐像素加锁的开销又太大。

这里的做法是把屏幕切成 16×16 的 Tile，**以 Tile 为任务单元分配，同一时刻一个 Tile 只由一个线程处理**。线程之间写的像素区域互不重叠，因此完全不需要加锁；同一 Tile 内的三角形仍按提交顺序串行处理，深度测试和混合结果与单线程一致。插值用的 varyings 缓冲按工作线程编号各自独立，避免共享临时数据。

有两条并行路径：

- **批处理 Binning**（PBR、天空盒）：`graphics_begin_batch` 开启批次，每个三角形完成顶点处理后按包围盒登记到覆盖的各个 Tile，`graphics_end_batch` 把每个非空 Tile 作为一个任务提交给线程池并等待完成。
- **单三角形分块**（Blinn-Phong 默认）：包围盒不小于 256 像素的三角形按 Tile 拆成子任务并行，小三角形直接在当前线程完成，避免调度开销超过收益。Blinn-Phong 片元计算较轻，批处理收益不明显，可用环境变量 `RENDERER_BLINN_BATCH=1` 切换。

线程池（`core/threadpool.c`）是固定大小的实现：任务队列为互斥锁保护的链表，并用计数器记录未完成任务数；工作线程空闲时在信号量（Win32）或条件变量（POSIX）上等待；最后一个任务完成时发出通知，`threadpool_wait` 据此作为每批光栅化的同步点。两个平台的差异封装在同一组内部函数之后。

PBR 场景经过多轮迭代（初版 Tile 并行，之后简化批处理逻辑、改为由任务自行释放三角形数据），帧率从 27 FPS 提升到 40 FPS。程序运行时每秒在控制台输出帧率和平均帧耗时，可以用 1 个线程和多个线程分别运行对比。

## 构建

依赖 CMake，以及 Visual Studio（Windows）、Xcode（macOS）或 GCC/Clang 与 X11（Linux）。

```bash
# Windows：在项目根目录直接运行
build_win32.bat

# 或使用 CMake
mkdir build && cd build
cmake -D CMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

macOS 和 Linux 可使用 `build_macos.sh`、`build_linux.sh`。

## 使用

```
Viewer [test_name [scene_name]]
```

- **不带参数**　先在控制台询问渲染线程数（回车使用 CPU 核心数，输入 1 为单线程），然后打开菜单选择渲染器和模型。
- **只给 `test_name`**（`blinn` 或 `pbr`）　询问线程数后随机加载该渲染器下的一个场景。
- **同时给出 `scene_name`**　跳过询问直接加载，例如 `Viewer pbr helmet`、`Viewer blinn azura`。

环境变量：

| 变量 | 作用 |
| --- | --- |
| `RENDERER_THREADS` | 默认渲染线程数，代替 CPU 核心数 |
| `RENDERER_BLINN_BATCH` | 设为 `1` 时 Blinn-Phong 场景使用批处理 Binning |

### 操作

- 左键拖动：旋转视角
- 右键拖动：平移
- 滚轮：缩放
- <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd>：旋转光照方向
- <kbd>Space</kbd>：重置
- 双击（PBR 场景）：打开材质分层查看

## 代码结构

```
renderer/
├── core/        数学库、图形管线（graphics.c）、线程池（threadpool.c）、
│                纹理、图像、网格、骨骼、相机、场景、平台抽象
├── shaders/     Blinn-Phong、PBR、天空盒着色器，资源缓存
├── scenes/      场景文件解析与各模型的场景配置
├── tests/       主循环、启动菜单、PBR 分层查看
├── platforms/   Win32 / macOS / Linux 窗口与输入
└── main.c       入口
scripts/         资产预处理：glTF 转扩展 OBJ、动画导出、环境贴图生成
```

资产预处理脚本把 glTF 模型转成带切线、关节索引和权重的扩展 OBJ，并导出 `.ani` 动画与场景文件。模型与环境贴图的来源和授权见 `assets/` 下各目录的 README。

## 致谢与参考

本项目基于开源软光栅渲染器 [zauonlok/renderer](https://github.com/zauonlok/renderer)（MIT 协议）开发。

- [ssloy/tinyrenderer](https://github.com/ssloy/tinyrenderer)　软光栅入门教程
- [OpenGL ES 2.0 Specification](https://www.khronos.org/registry/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf)　管线行为、cubemap 面选择、透视校正插值
- [Homogeneous clipping](http://fabiensanglard.net/polygon_codec/)　齐次裁剪
- [Perspective correct interpolation](https://www.comp.nus.edu.sg/~lowkl/publications/lowk_persp_interp_techrep.pdf)
- [Physically Based Rendering in Filament](https://google.github.io/filament/Filament.html)　PBR 与 IBL
- [glTF 2.0 Sample Viewer](https://github.com/KhronosGroup/glTF-Sample-Viewer)　两套 PBR 工作流
- Heitz, *Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs*　Smith 可见性项
- [ACES filmic tone mapping curve](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/)
- [three.js OrbitControls](https://github.com/mrdoob/three.js/blob/master/examples/js/controls/OrbitControls.js)　轨道相机

