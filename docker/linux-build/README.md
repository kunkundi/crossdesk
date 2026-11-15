# 使用容器构建 Linux 客户端

[返回构建指南](../../docs/BUILD.md#linux)

需要在 Ubuntu 20.04 / glibc 2.31 环境中自行构建时，可使用本目录的 Dockerfile。日常安装不需要 Docker，直接下载对应架构的 DEB 即可。

镜像提供 GCC 10、CMake 3.31.6、Xmake 3.1.1、Rust 1.92.0 及 Linux 构建依赖。首次生成镜像需要联网下载并编译依赖，耗时和磁盘占用会较大。

## 生成本地构建镜像

先安装 Docker，并下载包含子模块的完整源码。在仓库根目录执行与本机架构对应的一条命令。

x64 / amd64：

```sh
git submodule update --init --recursive
docker build -f docker/linux-build/Dockerfile -t crossdesk-build:local .
```

arm64 使用 Ubuntu 基础镜像，不包含 CUDA SDK：

```sh
git submodule update --init --recursive
docker build -f docker/linux-build/Dockerfile \
  --build-arg BASE_IMAGE=ubuntu:20.04 -t crossdesk-build:local .
```

以上命令在当前架构构建，不自动生成两个架构的安装包。需要另一架构时，在对应环境构建。

## 进入容器并编译

从仓库根目录启动：

```sh
docker run --rm -it -v "$PWD:/workspace" -w /workspace crossdesk-build:local
```

在容器中执行：

```sh
verify-crossdesk-build-image
xmake f -c -m release --USE_CUDA=false --toolchain=gcc-10 -y
xmake b crossdesk
```

源码目录挂载到容器中，编译输出会保留在主机的 `build/` 中。容器默认以 root 运行，Linux 主机上的产物可能归 root 所有。更换架构或工具链后需重新配置，避免使用不匹配的旧产物。

生成 DEB 的命令见 [Linux 打包步骤](../../docs/BUILD.md#linux)，打包版本需与编译时的版本一致。

## Wayland 与运行要求

镜像提供编译用的 PipeWire 0.3 / SPA 头文件，不会把 PipeWire 动态库打进安装包。运行 Wayland 采集需要被控主机提供 PipeWire 0.3 和桌面授权组件；没有它们时可使用 X11。

构建容器不是远程桌面运行环境。要在无显示器的 Linux 主机运行 CrossDesk，请按 [无头运行指南](../../docs/HEADLESS.md) 准备现有桌面或独立虚拟桌面。
