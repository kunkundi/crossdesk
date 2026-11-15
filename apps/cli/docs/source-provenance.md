# CLI 源码与许可信息

[返回 CLI 指南](../README.md)

CrossDesk CLI 的源码位于 [CrossDesk 仓库](https://github.com/kunkundi/crossdesk)。下载某个版本的安装包后，请使用包内记录的源码版本和构建信息定位对应源码，不要把当前分支当作该安装包的精确来源。

CLI 使用 [MiniRTC](https://github.com/kunkundi/minirtc)；具体版本由主仓库的 `deps/submodules/minirtc` 子模块提交记录。克隆时使用 `--recurse-submodules`，或在已有仓库执行 `git submodule update --init --recursive`。更新依赖后需重新构建 CLI。

npm 原生包附带 `source/` 源码快照，保留 CLI、共享库、构建配置和 MiniRTC 的目录结构。需要自行编译时，在源码目录按 [构建步骤](terminal-client.md#构建与安装) 配置 `--CROSSDESK_BUILD_DESKTOP=n --CROSSDESK_BUILD_CLI=y`。

CrossDesk 使用 [GPL-3.0-only](../../../LICENSE)，MiniRTC 保留其 LGPL-3.0 及第三方许可声明。其他依赖分别适用各自许可证；具体材料以所用版本附带的许可证和源码说明为准。
