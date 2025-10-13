# CrossDesk

#### 跨界连接，高效如一

----
[English](README.md) / [中文](README_CN.md)

![sup_example](https://github.com/user-attachments/assets/eeb64fbe-1f07-4626-be1c-b77396beb905)

## 简介

CrossDesk 是一个轻量级的跨平台远程桌面软件。

CrossDesk 是 [MiniRTC](https://github.com/kunkundi/minirtc.git) 实时音视频传输库的实验性应用。MiniRTC 是一个轻量级的跨平台实时音视频传输库。它具有网络透传（[RFC5245](https://datatracker.ietf.org/doc/html/rfc5245)），视频软硬编解码（H264/AV1），音频编解码（[Opus](https://github.com/xiph/opus)），信令交互，网络拥塞控制，传输加密（[SRTP](https://tools.ietf.org/html/rfc3711)）等基础能力。


## 使用

在菜单栏“对端ID”处输入远端桌面的ID，点击“→”即可发起远程连接。

![usage1](https://github.com/user-attachments/assets/3a4bb59f-c84c-44d2-9a20-11790aac510e)

如果远端桌面设置了连接密码，则本端需填写正确的连接密码才能成功发起远程连接。

![password](https://github.com/user-attachments/assets/1beadcce-640d-4f5c-8e77-51917b5294d5)

发起连接前，可在设置中自定义配置项，如语言、视频编码格式等。
![settings](https://github.com/user-attachments/assets/8bc5468d-7bbb-4e30-95bd-da1f352ac08c)

## 如何编译

依赖：
- [xmake](https://xmake.io/#/guide/installation)
- [cmake](https://cmake.org/download/)

Linux环境下需安装以下包：

```
sudo apt-get install -y software-properties-common git curl unzip build-essential libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxcb-randr0-dev libxcb-xtest0-dev libxcb-xinerama0-dev libxcb-shape0-dev libxcb-xkb-dev libxcb-xfixes0-dev libxv-dev libxtst-dev libasound2-dev libsndio-dev libxcb-shm0-dev libasound2-dev libpulse-dev
```

编译
```
git clone https://github.com/kunkundi/crossdesk.git

cd crossdesk

git submodule init 

git submodule update

xmake b crossdesk
```
#### 无 CUDA 环境下的开发支持

对于未安装 **CUDA 环境** 的Linux开发者，这里提供了预配置的 [Ubuntu 22.04 Docker 镜像](https://hub.docker.com/r/crossdesk/ubuntu22.04)。  
该镜像内置必要的构建依赖，可在容器中开箱即用，无需额外配置即可直接编译项目。

进入容器，下载工程后执行：
```
export CUDA_PATH=/usr/local/cuda
export XMAKE_GLOBALDIR=/data

xmake b --root crossdesk
```

运行
```
xmake r crossdesk
```

#### 注意
运行时如果客户端状态栏显示 **未连接服务器**，请先在 [CrossDesk 官方网站](https://www.crossdesk.cn/) 安装客户端，以便在环境中安装所需的证书文件。

<img width="129" height="60" alt="image" src="https://github.com/user-attachments/assets/1812f7d6-516b-4b4f-8a3d-98bee505cc5a" />

## 关于 Xmake

#### 安装 Xmake
使用 curl：
```
curl -fsSL https://xmake.io/shget.text | bash
```
使用 wget：
```
wget https://xmake.io/shget.text -O - | bash
```
使用 powershell：
```
irm https://xmake.io/psget.text | iex
```

#### 编译选项
```
# 切换编译模式
xmake f -m debug/release

# 可选编译参数
-r ：重新构建目标
-v ：显示详细的构建日志
-y ：自动确认提示

# 示例
xmake b -vy crossdesk
```

#### 运行选项
```
# 使用调试模式运行
xmake r -d crossdesk

更多使用方法可参考 [Xmake官方文档](https://xmake.io/guide/quick-start.html) 。
