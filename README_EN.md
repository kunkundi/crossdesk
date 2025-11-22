# CrossDesk

[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-brightgreen.svg)]()
[![License: LGPL v3](https://img.shields.io/badge/License-LGPL%20v3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0)
[![GitHub last commit](https://img.shields.io/github/last-commit/kunkundi/crossdesk)](https://github.com/kunkundi/crossdesk/commits/web-client)
[![Build Status](https://github.com/kunkundi/crossdesk/actions/workflows/build.yml/badge.svg)](https://github.com/kunkundi/crossdesk/actions)  
[![Docker Pulls](https://img.shields.io/docker/pulls/crossdesk/crossdesk-server)](https://hub.docker.com/r/crossdesk/crossdesk-server/tags)
[![GitHub issues](https://img.shields.io/github/issues/kunkundi/crossdesk.svg)]()
[![GitHub stars](https://img.shields.io/github/stars/kunkundi/crossdesk.svg?style=social)]()
[![GitHub forks](https://img.shields.io/github/forks/kunkundi/crossdesk.svg?style=social)]()

[ [中文](README.md) / English ]

PC Client
![sup_example](https://github.com/user-attachments/assets/3f17d8f3-7c4a-4b63-bae4-903363628687)

Web Client
<p align="center">
  <img width="850" height="550" alt="6bddcbed47ffd4b9988a4037c7f4f524" src="https://github.com/user-attachments/assets/e44f73f9-24ac-46a3-a189-b7f8b6669881" />
</p>

# Intro

CrossDesk is a lightweight cross-platform remote desktop software.

CrossDesk is an experimental application of [MiniRTC](https://github.com/kunkundi/minirtc.git), a lightweight cross-platform real-time audio and video transmission library. MiniRTC provides fundamental capabilities including network traversal ([RFC5245](https://datatracker.ietf.org/doc/html/rfc5245)), video software/hardware encoding and decoding (H264/AV1), audio encoding/decoding ([Opus](https://github.com/xiph/opus)), signaling interaction, network congestion control, and transmission encryption ([SRTP](https://tools.ietf.org/html/rfc3711)).

## System Requirements

| Platform | Minimum Version |
|-----------|-----------------|
| **Windows** | Windows 10 or later (64-bit) |
| **macOS** | macOS Intel 15.0 or later *(versions between 14.0 and 15.0 can be built manually for compatibility)*<br>macOS Apple Silicon 14.0 or later |
| **Linux** | Ubuntu 22.04 or later *(older versions can be built manually for compatibility)* |


## Usage

Enter the remote desktop ID in the menu bar’s “Remote ID” field and click “→” to initiate a remote connection.

![usage1](https://github.com/user-attachments/assets/3a4bb59f-c84c-44d2-9a20-11790aac510e)

If the remote desktop requires a connection password, you must enter the correct password on your side to successfully establish the connection.

![password](https://github.com/user-attachments/assets/1beadcce-640d-4f5c-8e77-51917b5294d5)

Before connecting, you can customize configuration options in the settings, such as language and video encoding format.

![settings](https://github.com/user-attachments/assets/8bc5468d-7bbb-4e30-95bd-da1f352ac08c)

### Web Client

Visit  [CrossDesk Web Client](https://web.crossdesk.cn/).
Enter the **Remote Device ID** and **Password**, then click Connect to access the remote device. As shown, **iOS Safari remotely controlling Windows 11**:

<img width="645" height="300" alt="_cgi-bin_mmwebwx-bin_webwxgetmsgimg__ MsgID=932911462648581698 skey=@crypt_1f5153b1_b550ca7462b5009ce03c991cca2a92a7 mmweb_appid=wx_webfilehelper" src="https://github.com/user-attachments/assets/a5109e6f-752c-4654-9f4e-7e161bddf43e" />

## How to build

Requirements:
- [xmake](https://xmake.io/#/guide/installation)
- [cmake](https://cmake.org/download/)

Following packages need to be installed on Linux:

```
sudo apt-get install -y software-properties-common git curl unzip build-essential libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxcb-randr0-dev libxcb-xtest0-dev libxcb-xinerama0-dev libxcb-shape0-dev libxcb-xkb-dev libxcb-xfixes0-dev libxfixes-dev libxv-dev libxtst-dev libasound2-dev libsndio-dev libxcb-shm0-dev libasound2-dev libpulse-dev
```

Build:
```
git clone https://github.com/kunkundi/crossdesk.git

cd crossdesk

git submodule init 

git submodule update

xmake b -vy crossdesk
```
Build options:
```
--USE_CUDA=true/false: enable CUDA acceleration codec, default: false
--CROSSDESK_VERSION=xxx: set the version number

# example:
xmake f --CROSSDESK_VERSION=1.0.0 --USE_CUDA=true
```
Run:
```
xmake r crossdesk
```

#### Development Without CUDA Environment

For **Linux developers who do not have a CUDA environment installed and want to enable hardware codec feature**, a preconfigured [Ubuntu 22.04 Docker image](https://hub.docker.com/r/crossdesk/ubuntu22.04) is provided.  
This image comes with all required build dependencies and allows you to build the project directly inside the container without any additional setup.

After entering the container, download the project and run:
```
export CUDA_PATH=/usr/local/cuda
export XMAKE_GLOBALDIR=/data

xmake f --USE_CUDA=true
xmake b --root -vy crossdesk
```

For **Windows developers without a CUDA environment** installed, run the following command to install the CUDA build environment:
```
xmake require -vy "cuda 12.6.3"
```
After the installation is complete, execute:
```
xmake require --info "cuda 12.6.3"
```
The output will look like this:

<img width="860" height="226" alt="Image" src="https://github.com/user-attachments/assets/999ac365-581a-4b9a-806e-05eb3e4cf44d" />

From the output above, locate the CUDA installation directory — this is the path pointed to by installdir.
Add this path to your system environment variable CUDA_PATH, or set it in the terminal using:
```
set CUDA_PATH=path_to_cuda_installdir:
```
Then re-run:
```
xmake f --USE_CUDA=true
xmake b -vy crossdesk
```

#### Notice
If the client status bar shows **Disconnected** during runtime, please first install the client from the [CrossDesk official website](https://www.crossdesk.cn/) to ensure the required certificate files are available in the environment.

<img width="256" height="120" alt="image" src="https://github.com/user-attachments/assets/1812f7d6-516b-4b4f-8a3d-98bee505cc5a" />

## About Xmake
#### Installing Xmake

You can install Xmake using one of the following methods:

Using curl:
```
curl -fsSL https://xmake.io/shget.text | bash
```
Using wget:
```
wget https://xmake.io/shget.text -O - | bash
```
Using powershell:
```
irm https://xmake.io/psget.text | iex
```

#### Build Options
```
# Switch build mode
xmake f -m debug/release

# Optional build parameters
-r : Rebuild the target
-v : Show detailed build logs
-y : Automatically confirm prompts

# Example
xmake b -vy crossdesk
```

#### Run Options
```
# Run in debug mode
xmake r -d crossdesk
```

For more information, please refer to the [official Xmake documentation](https://xmake.io/guide/quick-start.html) .

## Self-Hosted Server
It is recommended to deploy CrossDesk Server using Docker.
```
sudo docker run -d \
  --name crossdesk_server \
  --network host \
  -e EXTERNAL_IP=xxx.xxx.xxx.xxx \
  -e INTERNAL_IP=xxx.xxx.xxx.xxx \
  -e CROSSDESK_SERVER_PORT=xxxx \
  -e COTURN_PORT=xxxx \
  -e MIN_PORT=xxxxx \
  -e MAX_PORT=xxxxx \
  -v /path/to/your/certs:/crossdesk-server/certs \
  -v /path/to/your/db:/crossdesk-server/db \
  -v /path/to/your/logs:/crossdesk-server/logs \
  crossdesk/crossdesk-server:v1.1.1
```

The parameters you need to pay attention to are as follows:

- **EXTERNAL_IP**: The server's public IP, corresponding to the **Server Address** in the CrossDesk client **Self-Hosted Server Configuration**.

- **INTERNAL_IP**: The server's internal IP.

- **CROSSDESK_SERVER_PORT**: The port used by the self-hosted server, corresponding to the **Server Port** in the CrossDesk client **Self-Hosted Server Configuration**.

- **COTURN_PORT**: The port used by Coturn, corresponding to the **Relay Server Port** in the CrossDesk client **Self-Hosted Server Configuration**.

- **MIN_PORT** and **MAX_PORT**: The range of ports used by the self-hosted server, corresponding to the **Minimum Port** and **Maximum Port** in the CrossDesk client **Self-Hosted Server Configuration**. Example: 50000-60000. It depends on the number of devices connected to the server.

- **/path/to/your/certs**: Directory for certificate files.

- **/path/to/your/db**: CrossDesk Server device management database.

- **/path/to/your/logs**: Log directory.

**Note**:  
- **/path/to/your/ is an example path; please replace it with your actual path. The mounted directories must be created in advance, otherwise the container will fail.**
- **The server must open the following ports: 3478/udp, 3478/tcp, 30000-60000/udp, CROSSDESK_SERVER_PORT/tcp.**

## Certificate Files
The client needs to load the root certificate, and the server needs to load the server private key and server certificate.

If you already have an SSL certificate, you can skip the following certificate generation steps.

For users without a certificate, you can use the script below to generate the certificate files:
```
# Create certificate generation script
vim generate_certs.sh
```
Copy the following into the script:
```
#!/bin/bash
set -e

# Check arguments
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <SERVER_IP>"
    exit 1
fi

SERVER_IP="$1"

# Filenames
ROOT_KEY="crossdesk.cn_root.key"
ROOT_CERT="crossdesk.cn_root.crt"
SERVER_KEY="crossdesk.cn.key"
SERVER_CSR="crossdesk.cn.csr"
SERVER_CERT="crossdesk.cn_bundle.crt"
FULLCHAIN_CERT="crossdesk.cn_fullchain.crt"

# Certificate subject
SUBJ="/C=CN/ST=Zhejiang/L=Hangzhou/O=CrossDesk/OU=CrossDesk/CN=$SERVER_IP"

# 1. Generate root certificate
echo "Generating root private key..."
openssl genrsa -out "$ROOT_KEY" 4096

echo "Generating self-signed root certificate..."
openssl req -x509 -new -nodes -key "$ROOT_KEY" -sha256 -days 3650 -out "$ROOT_CERT" -subj "$SUBJ"

# 2. Generate server private key
echo "Generating server private key..."
openssl genrsa -out "$SERVER_KEY" 2048

# 3. Generate server CSR
echo "Generating server CSR..."
openssl req -new -key "$SERVER_KEY" -out "$SERVER_CSR" -subj "$SUBJ"

# 4. Create temporary OpenSSL config file with SAN
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

# 5. Sign server certificate with root certificate (including SAN)
echo "Signing server certificate with root certificate..."
openssl x509 -req -in "$SERVER_CSR" -CA "$ROOT_CERT" -CAkey "$ROOT_KEY" -CAcreateserial \
  -out "$SERVER_CERT" -days 3650 -sha256 -extfile "$SAN_CONF" -extensions req_ext

# 6. Generate full chain certificate
cat "$SERVER_CERT" "$ROOT_CERT" > "$FULLCHAIN_CERT"

# 7. Clean up intermediate files
rm -f "$ROOT_CERT.srl" "$SAN_CONF" "$ROOT_KEY" "$SERVER_CSR" "FULLCHAIN_CERT"

echo "Generation complete. Deployment files:"
echo "  Client root certificate: $ROOT_CERT"
echo "  Server private key: $SERVER_KEY"
echo "  Server certificate: $SERVER_CERT"
```
Execute:
```
chmod +x generate_certs.sh
./generate_certs.sh EXTERNAL_IP

# example ./generate_certs.sh 111.111.111.111
```
Expected output:
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

#### Server Side
Place **crossdesk.cn.key** and **crossdesk.cn_bundle.crt** into the **/path/to/your/certs** directory.

#### Client Side
1. Click the settings icon in the top-right corner to enter the settings page.<br>
<img width="600" height="210" alt="image" src="https://github.com/user-attachments/assets/6431131d-b32a-4726-8783-6788f47baa3b" /><br><br>

2. Click **Self-Hosted Server Configuration**.<br><br>
<img width="600" height="160" alt="image" src="https://github.com/user-attachments/assets/24c761a3-1985-4d7e-84be-787383c2afb8" /><br><br>

3. In the **Certificate File Path** selection, locate and select the **crossdesk.cn_root.crt** file.<br><br>
<img width="600" height="220" alt="image" src="https://github.com/user-attachments/assets/4af7cd3a-c72e-44fb-b032-30e050019c2a" /><br><br>

4. Check the option to use **Self-Hosted Server Configuration**.<br><br>
<img width="600" height="160" alt="image" src="https://github.com/user-attachments/assets/1e455dc3-4087-4f37-a544-1ff9f8789383" /><br><br>

# FAQ
See [FAQ](https://github.com/kunkundi/crosssesk/blob/self-hosted-server/docs/FAQ.md) .
