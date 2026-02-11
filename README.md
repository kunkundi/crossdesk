# CrossDesk

<a href="https://hellogithub.com/repository/kunkundi/crossdesk" target="_blank"><img src="https://api.hellogithub.com/v1/widgets/recommend.svg?rid=55d41367570345f1838e02fd12be7961&claim_uid=cb0OpZRrBuGVAfL&theme=small" alt="Featured｜HelloGitHub" /></a>
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-brightgreen.svg)]()
[![License: LGPL v3](https://img.shields.io/badge/License-LGPL%20v3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0)
[![GitHub last commit](https://img.shields.io/github/last-commit/kunkundi/crossdesk)](https://github.com/kunkundi/crossdesk/commits/web-client)
[![Build Status](https://github.com/kunkundi/crossdesk/actions/workflows/build.yml/badge.svg)](https://github.com/kunkundi/crossdesk/actions)  
[![Docker Pulls](https://img.shields.io/docker/pulls/crossdesk/crossdesk-server)](https://hub.docker.com/r/crossdesk/crossdesk-server/tags)
[![GitHub issues](https://img.shields.io/github/issues/kunkundi/crossdesk.svg)]()
[![GitHub stars](https://img.shields.io/github/stars/kunkundi/crossdesk.svg?style=social)]()
[![GitHub forks](https://img.shields.io/github/forks/kunkundi/crossdesk.svg?style=social)]()

[ [English](README_EN.md) / 中文 ]

PC 客户端
![sup_example](https://github.com/user-attachments/assets/eeb64fbe-1f07-4626-be1c-b77396beb905)

Web 客户端
<p align="center">
  <img width="850" height="550" alt="6bddcbed47ffd4b9988a4037c7f4f524" src="https://github.com/user-attachments/assets/e44f73f9-24ac-46a3-a189-b7f8b6669881" />
</p>

## 简介

CrossDesk 是一个轻量级的跨平台远程桌面软件，支持 Web 端控制远程设备。

CrossDesk 是 [MiniRTC](https://github.com/kunkundi/minirtc.git) 实时音视频传输库的实验性应用。MiniRTC 是一个轻量级的跨平台实时音视频传输库。它具有网络透传（[RFC5245](https://datatracker.ietf.org/doc/html/rfc5245)），视频软硬编解码（H264/AV1），音频编解码（[Opus](https://github.com/xiph/opus)），信令交互，网络拥塞控制，传输加密（[SRTP](https://tools.ietf.org/html/rfc3711)）等基础能力。

## 系统要求

| 平台 | 最低版本 |
|----------------|---------------------------|
| **Windows** | Windows 10 及以上 (64 位) |
| **macOS** | macOS Intel 15.0 及以上 ( 大于 14.0 小于 15.0 的版本可自行编译实现兼容 )<br> macOS Apple Silicon 14.0 及以上 |
| **Linux** | Ubuntu 22.04 及以上 ( 低版本可自行编译实现兼容 ) |

## 使用

在菜单栏“对端ID”处输入远端桌面的ID，点击“→”即可发起远程连接。

![usage1](https://github.com/user-attachments/assets/3a4bb59f-c84c-44d2-9a20-11790aac510e)

如果远端桌面设置了连接密码，则本端需填写正确的连接密码才能成功发起远程连接。

![password](https://github.com/user-attachments/assets/1beadcce-640d-4f5c-8e77-51917b5294d5)

发起连接前，可在设置中自定义配置项，如语言、视频编码格式等。
![settings](https://github.com/user-attachments/assets/8bc5468d-7bbb-4e30-95bd-da1f352ac08c)

### Web 客户端
浏览器访问 [CrossDesk Web Client](https://web.crossdesk.cn/)。
输入 **远程设备 ID** 与 **密码**，点击连接即可接入远程设备。如图，**iOS Safari 远程控制 Win11**：

<img width="645" height="300" alt="_cgi-bin_mmwebwx-bin_webwxgetmsgimg__ MsgID=932911462648581698 skey=@crypt_1f5153b1_b550ca7462b5009ce03c991cca2a92a7 mmweb_appid=wx_webfilehelper" src="https://github.com/user-attachments/assets/a5109e6f-752c-4654-9f4e-7e161bddf43e" />

## 如何编译

依赖：
- [xmake](https://xmake.io/#/guide/installation)
- [cmake](https://cmake.org/download/)

Linux环境下需安装以下包：

```
sudo apt-get install -y software-properties-common git curl unzip build-essential libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxcb-randr0-dev libxcb-xtest0-dev libxcb-xinerama0-dev libxcb-shape0-dev libxcb-xkb-dev libxcb-xfixes0-dev libxfixes-dev libxv-dev libxtst-dev libasound2-dev libsndio-dev libxcb-shm0-dev libasound2-dev libpulse-dev
```

编译
```
git clone https://github.com/kunkundi/crossdesk.git

cd crossdesk

git submodule init 

git submodule update

xmake b -vy crossdesk
```
编译选项
```
--USE_CUDA=true/false: 启用 CUDA 硬件编解码，默认不启用
--CROSSDESK_VERSION=xxx: 指定 CrossDesk 的版本

# 示例
xmake f --CROSSDESK_VERSION=1.0.0 --USE_CUDA=true
```
运行
```
xmake r crossdesk
```

### 无 CUDA 环境下的开发支持

对于**未安装 CUDA 环境的 Linux 开发者，如果希望编译后的成果物拥有硬件编解码能力**，这里提供了预配置的 [Ubuntu 22.04 Docker 镜像](https://hub.docker.com/r/crossdesk/ubuntu22.04)。该镜像内置必要的构建依赖，可在容器中开箱即用，无需额外配置即可直接编译项目。

进入容器，下载工程后执行：
```
export CUDA_PATH=/usr/local/cuda
export XMAKE_GLOBALDIR=/data

xmake f --USE_CUDA=true
xmake b --root -vy crossdesk
```

对于**未安装 CUDA 环境的 Windows 开发者**，执行下面的命令安装 CUDA 编译环境：
```
xmake require -vy "cuda 12.6.3"
```
安装完成后执行:
```
xmake require --info "cuda 12.6.3"
```
输出如下:

<img width="860" height="226" alt="Image" src="https://github.com/user-attachments/assets/999ac365-581a-4b9a-806e-05eb3e4cf44d" />

根据上述输出获取到 CUDA 的安装目录，即 installdir 指向的位置。将 CUDA_PATH 加入系统环境变量，或在终端中输入：
```
set CUDA_PATH=path_to_cuda_installdir
```
重新执行：
```
xmake f --USE_CUDA=true
xmake b -vy crossdesk
```

#### 注意
运行时如果客户端状态栏显示 **未连接服务器**，请先在 [CrossDesk 官方网站](https://www.crossdesk.cn/) 安装客户端，以便在环境中安装所需的证书文件。

<img width="256" height="120" alt="image" src="https://github.com/user-attachments/assets/1812f7d6-516b-4b4f-8a3d-98bee505cc5a" />

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
```
更多使用方法可参考 [Xmake官方文档](https://xmake.io/guide/quick-start.html) 。

## 自托管服务器
推荐使用Docker部署CrossDesk Server。
```bash
sudo docker run -d \
  --name crossdesk_server \
  --network host \
  -e EXTERNAL_IP=xxx.xxx.xxx.xxx \
  -e INTERNAL_IP=xxx.xxx.xxx.xxx \
  -e CROSSDESK_SERVER_PORT=xxxx \
  -e COTURN_PORT=xxxx \
  -e MIN_PORT=xxxxx \
  -e MAX_PORT=xxxxx \
  -v /var/lib/crossdesk:/var/lib/crossdesk \
  -v /var/log/crossdesk:/var/log/crossdesk \
  crossdesk/crossdesk-server:v1.1.3
```

上述命令中，用户需注意的参数如下：

**参数**
- EXTERNAL_IP：服务器公网 IP , 对应 CrossDesk 客户端**自托管服务器配置**中填写的**服务器地址**
- INTERNAL_IP：服务器内网 IP
- CROSSDESK_SERVER_PORT：自托管服务使用的端口，对应 CrossDesk 客户端**自托管服务器配置**中填写的**服务器端口**
- COTURN_PORT: COTURN 服务使用的端口, 对应 CrossDesk 客户端**自托管服务器配置**中填写的**中继服务端口**
- MIN_PORT/MAX_PORT：COTURN 服务使用的端口范围，例如：MIN_PORT=50000, MAX_PORT=60000，范围可根据客户端数量调整。
- `-v /var/lib/crossdesk:/var/lib/crossdesk`：持久化数据库和证书文件到宿主机
- `-v /var/log/crossdesk:/var/log/crossdesk`：持久化日志文件到宿主机

**示例**：
```bash
sudo docker run -d \
  --name crossdesk_server \
  --network host \
  -e EXTERNAL_IP=114.114.114.114 \
  -e INTERNAL_IP=10.0.0.1 \
  -e CROSSDESK_SERVER_PORT=9099 \
  -e COTURN_PORT=3478 \
  -e MIN_PORT=50000 \
  -e MAX_PORT=60000 \
  -v /var/lib/crossdesk:/var/lib/crossdesk \
  -v /var/log/crossdesk:/var/log/crossdesk \
  crossdesk/crossdesk-server:v1.1.3
```

**注意**：
- **服务器需开放端口：COTURN_PORT/udp，COTURN_PORT/tcp，MIN_PORT-MAX_PORT/udp，CROSSDESK_SERVER_PORT/tcp。**
- 如果不挂载 volume，容器删除后数据会丢失
- 证书文件会在首次启动时自动生成并持久化到宿主机的 `/var/lib/crossdesk/certs` 路径下。由于默认使用的是自签证书，无法保障安全性，建议在云服务商申请正式证书放到该目录下并重启服务。
- 数据库文件会自动创建并持久化到宿主机的 `/var/lib/crossdesk/db/crossdesk-server.db` 路径下
- 日志文件会自动创建并持久化到宿主机的 `/var/log/crossdesk/` 路径下

**权限注意**：如果 Docker 自动创建的目录权限不足（属于 root），容器内用户无法写入，会导致：
  - 证书生成失败，容器启动脚本会报错退出
  - 数据库目录创建失败，程序会抛出异常并崩溃
  - 日志目录创建失败，日志文件无法写入（但程序可能继续运行）
  
**解决方案**：在启动容器前手动设置权限：
```bash
sudo mkdir -p /var/lib/crossdesk /var/log/crossdesk
sudo chown -R $(id -u):$(id -g) /var/lib/crossdesk /var/log/crossdesk
```


### 客户端
1. 点击右上角设置进入设置页面。<br><br>
<img width="600" height="210" alt="image" src="https://github.com/user-attachments/assets/6431131d-b32a-4726-8783-6788f47baa3b" /><br>

2. 点击`自托管服务器配置`按钮。<br><br>
<img width="600" height="160" alt="image" src="https://github.com/user-attachments/assets/24c761a3-1985-4d7e-84be-787383c2afb8" /><br>

3. 输入`服务器地址`(**EXTERNAL_IP**)、`信令服务端口`(**CROSSDESK_SERVER_PORT**)、`中继服务端口`(**COTURN_PORT**)，点击确认按钮。
   
4. 勾选`自托管服务器配置`选项，点击确认按钮保存设置。如果服务端使用的是正式证书，则到此步骤为止，客户端即可显示已连接服务器。

5. 如果使用默认证书（正式证书忽略此步骤），则需要将服务端`/var/lib/crossdesk/certs/`目录下的`api.crossdesk.cn_root.crt`自签根证书下载到运行客户端的机器，并执行下述命令安装证书：

Windows 平台使用**管理员权限**打开 PowerShell 执行
```
certutil -addstore "Root" "C:\path\to\api.crossdesk.cn_root.crt"
```
Linux
```
sudo cp /path/to/api.crossdesk.cn_root.crt /usr/local/share/ca-certificates/api.crossdesk.cn_root.crt
sudo update-ca-certificates
```
macOS
```
sudo security add-trusted-cert -d -r trustRoot -k /Library/Keychains/System.keychain path/to/api.crossdesk.cn_root.crt
```

### Web 客户端
详情见项目 [CrossDesk Web Client](https://github.com/kunkundi/crossdesk-web-client)。

# 常见问题
见 [常见问题](https://github.com/kunkundi/crossdesk/blob/self-hosted-server/docs/FAQ.md) 。
