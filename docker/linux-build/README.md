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

## 镜像发布与维护

工具链标签记录实际构建环境；应用构建所用镜像以 [构建工作流](../../.github/workflows/build.yml) 为准。切换标签前应确认 amd64 与 arm64 变体均可用。



推送影响镜像的文件到 `ci/linux-build-image` 分支时，
`build-linux-image.yml` 会在 GitHub 原生 amd64/arm64 runner 上分别构建和验证，
全部通过后再合并为同一个 multi-arch 镜像：

```text
crossdesk/ubuntu20.04:buildenv-cuda12.6.3-gcc10-cmake3.31.6-pipewire0.3.48-xmake3.1.1-rust1.92.0
```

Docker 会根据运行机器自动选择正确架构。amd64 变体以
`nvidia/cuda:12.6.3-devel-ubuntu20.04` 为基础；arm64 变体不包含当前构建
用不到的 CUDA SDK。两个变体都使用 Ubuntu 20.04/glibc 2.31，因此生成的
普通版和 Xfce 虚拟桌面版安装包均可运行在 Ubuntu 20.04 及更高版本。

Ubuntu 20.04 仓库只提供 PipeWire 0.2。镜像通过
`install-pipewire-sdk.sh` 安装固定版本的 PipeWire 0.3.48/SPA 头文件，但不
安装或打包 PipeWire 动态库。CrossDesk 在运行时通过 `dlopen()` 使用宿主系统
提供的 PipeWire 0.3：Ubuntu 20.04 没有该运行库时仍可使用 X11（以及构建时
启用的 DRM），Ubuntu 22.04 及更高版本则可使用 Wayland 捕获。

每次成功发布还会生成 `sha-<完整提交 SHA>`，用于精确回滚；`latest` 和上面的
工具链版本 tag 只会在两个架构都验证成功后更新。

## GitHub 配置

在仓库的 Actions secrets 中配置：

- `DOCKERHUB_USERNAME`：有权推送 `crossdesk/ubuntu20.04` 的 Docker Hub 用户名
- `DOCKERHUB_TOKEN`：该用户的 Docker Hub access token

工作流文件进入默认分支后，也可以从 Actions 页面手动运行。为了避免首次发布
新 tag 时应用构建先于镜像完成，常规 `build.yml` 不响应
`ci/linux-build-image` 分支的 push。第一次启用时，应先推送该分支并等待镜像
发布成功，再把应用构建工作流的修改合并到 `main`。

## 更新依赖

修改 Dockerfile、xmake 清单、本地包配方或子模块中的包配方后，推送该分支即可。
升级工具链时应先修改 Dockerfile 的版本参数和镜像工作流中的 `STACK_TAG`，
等两个架构的镜像发布成功后，再修改常规构建工作流引用的镜像 tag。
