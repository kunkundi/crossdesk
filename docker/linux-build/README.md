# CrossDesk Linux 构建镜像

这个目录维护 CrossDesk 专用的 Ubuntu 22.04 构建环境。镜像包含 xmake、
Rust、Linux 开发库以及根据项目 `xmake.lua` 提前编译好的依赖。

## 发布方式

推送影响镜像的文件到 `ci/linux-build-image` 分支时，
`build-linux-image.yml` 会在 GitHub 原生 amd64/arm64 runner 上分别构建和验证，
全部通过后再合并为同一个 multi-arch 镜像：

```text
crossdesk/ubuntu22.04:buildenv-cuda12.6.3-xmake3.0.9-rust1.92.0
```

Docker 会根据运行机器自动选择正确架构。amd64 变体以
`nvidia/cuda:12.6.3-devel-ubuntu22.04` 为基础；arm64 变体不包含当前构建
用不到的 CUDA SDK。两个变体都使用 Ubuntu 22.04/glibc 2.35。
因此该镜像生成的 Linux 安装包最低支持 Ubuntu 22.04，不再保证兼容
Ubuntu 20.04。

每次成功发布还会生成 `sha-<完整提交 SHA>`，用于精确回滚；`latest` 和上面的
工具链版本 tag 只会在两个架构都验证成功后更新。

## GitHub 配置

在仓库的 Actions secrets 中配置：

- `DOCKERHUB_USERNAME`：有权推送 `crossdesk/ubuntu22.04` 的 Docker Hub 用户名
- `DOCKERHUB_TOKEN`：该用户的 Docker Hub access token

工作流文件进入默认分支后，也可以从 Actions 页面手动运行。为了避免首次发布
新 tag 时应用构建先于镜像完成，常规 `build.yml` 不响应
`ci/linux-build-image` 分支的 push。第一次启用时，应先推送该分支并等待镜像
发布成功，再把应用构建工作流的修改合并到 `main`。

## 更新依赖

修改 Dockerfile、xmake 清单、本地包配方或子模块中的包配方后，推送该分支即可。
升级工具链时应同时修改 Dockerfile 的版本参数、工作流中的 `STACK_TAG`，以及
常规构建工作流引用的镜像 tag。
