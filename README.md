# CrossDesk

[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()
[![License: LGPL v3](https://img.shields.io/badge/License-LGPL%20v3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0)
[![Build Status](https://github.com/kunkundi/crossdesk/actions/workflows/build.yaml/badge.svg)](https://github.com/kunkundi/crossdesk/actions)  
[![GitHub issues](https://img.shields.io/github/issues/kunkundi/crossdesk.svg)]()
[![GitHub stars](https://img.shields.io/github/stars/kunkundi/crossdesk.svg?style=social)]()
[![GitHub forks](https://img.shields.io/github/forks/kunkundi/crossdesk.svg?style=social)]()

[English](README_EN.md) / [中文](README.md)

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
```
sudo docker run -d \
  --name crossdesk_server \
  --network host \
  -e EXTERNAL_IP=xxx.xxx.xxx.xxx \
  -e INTERNAL_IP=xxx.xxx.xxx.xxx \
  -e CROSSDESK_SERVER_PORT=9099 \
  -v /path/to/your/certs:/crossdesk-server/certs \
  -v /path/to/your/db:/crossdesk-server/db \
  -v /path/to/your/logs:/crossdesk-server/logs \
  crossdesk/crossdesk-server:latest
```

上述命令中，用户需注意的参数如下：

- EXTERNAL_IP：服务器公网 IP , 对应 CrossDesk 客户端**自托管服务器配置**中填写的**服务器地址**

- INTERNAL_IP：服务器内网 IP

- CROSSDESK_SERVER_PORT：自托管服务使用的端口，对应 CrossDesk 客户端**自托管服务器配置**中填写的**服务器端口**

- /path/to/your/certs：证书文件目录

- /path/to/your/db：CrossDesk Server 设备管理数据库

- /path/to/your/logs：日志目录

**注意**：
- **/path/to/your/ 是示例路径，请替换为你自己的实际路径。挂载的目录必须事先创建好，否则容器会报错。**
- **服务器需开放端口：3478/udp，3478/tcp，30000-60000/udp，CROSSDESK_SERVER_PORT/tcp，443/tcp。**

## 证书文件
客户端需加载根证书文件，服务端需加载服务器私钥和服务器证书文件。

如果已有SSL证书的用户，可以忽略下面的证书生成步骤。

对于无证书的用户，可使用下面的脚本自行生成证书文件：
```
# 创建证书生成脚本
vim generate_certs.sh
```
拷贝到脚本中
```
#!/bin/bash
set -e

# 检查参数
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <SERVER_IP>"
    exit 1
fi

SERVER_IP="$1"

# 文件名
ROOT_KEY="crossdesk.cn_root.key"
ROOT_CERT="crossdesk.cn_root.crt"
SERVER_KEY="crossdesk.cn.key"
SERVER_CSR="crossdesk.cn.csr"
SERVER_CERT="crossdesk.cn_bundle.crt"
FULLCHAIN_CERT="crossdesk.cn_fullchain.crt"

# 证书主题
SUBJ="/C=CN/ST=Zhejiang/L=Hangzhou/O=CrossDesk/OU=CrossDesk/CN=$SERVER_IP"

# 1. 生成根证书
echo "Generating root private key..."
openssl genrsa -out "$ROOT_KEY" 4096

echo "Generating self-signed root certificate..."
openssl req -x509 -new -nodes -key "$ROOT_KEY" -sha256 -days 3650 -out "$ROOT_CERT" -subj "$SUBJ"

# 2. 生成服务器私钥
echo "Generating server private key..."
openssl genrsa -out "$SERVER_KEY" 2048

# 3. 生成服务器 CSR
echo "Generating server CSR..."
openssl req -new -key "$SERVER_KEY" -out "$SERVER_CSR" -subj "$SUBJ"

# 4. 生成临时 OpenSSL 配置文件，加入 SAN
SAN_CONF="san.cnf"
cat > $SAN_CONF <<EOL
[ req ]
default_bits = 2048
distinguished_name = req_distinguished_name
req_extensions = req_ext
prompt = no

[ req_distinguished_name ]
C = CN
ST = Zhejiang
L = Hangzhou
O = CrossDesk
OU = CrossDesk
CN = $SERVER_IP

[ req_ext ]
subjectAltName = IP:$SERVER_IP
EOL

# 5. 用根证书签发服务器证书（包含 SAN）
echo "Signing server certificate with root certificate..."
openssl x509 -req -in "$SERVER_CSR" -CA "$ROOT_CERT" -CAkey "$ROOT_KEY" -CAcreateserial \
  -out "$SERVER_CERT" -days 3650 -sha256 -extfile "$SAN_CONF" -extensions req_ext

# 6. 生成完整链证书
cat "$SERVER_CERT" "$ROOT_CERT" > "$FULLCHAIN_CERT"

# 7. 清理中间文件
rm -f "$ROOT_CERT.srl" "$SAN_CONF" "$ROOT_KEY" "$SERVER_CSR" "FULLCHAIN_CERT"

echo "Generation complete. Deployment files:"
echo "  Client root certificate: $ROOT_CERT"
echo "  Server private key: $SERVER_KEY"
echo "  Server certificate: $SERVER_CERT"
```
执行
```
chmod +x generate_certs.sh
./generate_certs.sh 服务器公网IP

# 例如 ./generate_certs.sh 111.111.111.111
```
输出如下：
```
Generating root private key...
Generating self-signed root certificate...
Generating server private key...
Generating server CSR...
Signing server certificate with root certificate...
Certificate request self-signature ok
subject=C = CN, ST = Zhejiang, L = Hangzhou, O = CrossDesk, OU = CrossDesk, CN = xxx.xxx.xxx.xxx
cleaning up intermediate files...
Generation complete. Deployment files::
  Client root certificate:: crossdesk.cn_root.crt
  Server private key: crossdesk.cn.key
  Server certificate: crossdesk.cn_bundle.crt
```

#### 服务端
将 **crossdesk.cn.key** 和 **crossdesk.cn_bundle.crt** 放置到 **/path/to/your/certs** 目录下。

#### 客户端
1. 点击右上角设置进入设置页面。<br>
<img width="600" height="210" alt="image" src="https://github.com/user-attachments/assets/6431131d-b32a-4726-8783-6788f47baa3b" /><br><br>

3. 点击点击**自托管服务器配置**。<br><br>
<img width="600" height="160" alt="image" src="https://github.com/user-attachments/assets/24c761a3-1985-4d7e-84be-787383c2afb8" /><br><br>

5. 在**证书文件路径**选择框中找到 **crossdesk.cn_root.crt** 的存放路径，选中 **crossdesk.cn_root.crt**，点击确认。<br><br>
<img width="600" height="220" alt="image" src="https://github.com/user-attachments/assets/4af7cd3a-c72e-44fb-b032-30e050019c2a" /><br><br>

7. 勾选使用**自托管服务器配置**，点击确认配置生效。<br><br>
<img width="600" height="160" alt="image" src="https://github.com/user-attachments/assets/1e455dc3-4087-4f37-a544-1ff9f8789383" /><br><br>
