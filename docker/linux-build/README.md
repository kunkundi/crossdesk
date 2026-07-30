# CrossDesk Linux 构建镜像

这个目录维护 CrossDesk 专用的 Ubuntu 20.04 兼容构建环境。镜像包含 GCC 10、
CMake 3.31、xmake、Rust、Linux 开发库（包括 D-Bus、DRM，以及仅用于编译的
PipeWire 0.3/SPA 头文件 SDK）以及根据项目
`xmake.lua` 提前编译好的依赖。

## 发布方式

推送影响镜像的文件到 `ci/linux-build-image` 分支时，
`build-linux-image.yml` 会在 GitHub 原生 amd64/arm64 runner 上分别构建和验证，
全部通过后再合并为同一个 multi-arch 镜像：

```text
crossdesk/ubuntu20.04:buildenv-cuda12.6.3-gcc10-cmake3.31.6-pipewire0.3.48-xmake3.0.9-rust1.92.0
```

Docker 会根据运行机器自动选择正确架构。amd64 变体以
`nvidia/cuda:12.6.3-devel-ubuntu20.04` 为基础；arm64 变体不包含当前构建
用不到的 CUDA SDK。两个变体都使用 Ubuntu 20.04/glibc 2.31，因此生成的
单一 Linux 安装包可运行在 Ubuntu 20.04 及更高版本。

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
升级工具链时应同时修改 Dockerfile 的版本参数、工作流中的 `STACK_TAG`，以及
常规构建工作流引用的镜像 tag。
