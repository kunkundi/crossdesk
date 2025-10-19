# CrossDesk

[中文](README.md) / [English](README_EN.md)

![sup_example](https://github.com/user-attachments/assets/3f17d8f3-7c4a-4b63-bae4-903363628687)

# Intro

CrossDesk is a lightweight cross-platform remote desktop software.

CrossDesk is an experimental application of [MiniRTC](https://github.com/kunkundi/minirtc.git), a lightweight cross-platform real-time audio and video transmission library. MiniRTC provides fundamental capabilities including network traversal ([RFC5245](https://datatracker.ietf.org/doc/html/rfc5245)), video software/hardware encoding and decoding (H264/AV1), audio encoding/decoding ([Opus](https://github.com/xiph/opus)), signaling interaction, network congestion control, and transmission encryption ([SRTP](https://tools.ietf.org/html/rfc3711)).

## Usage

Enter the remote desktop ID in the menu bar’s “Remote ID” field and click “→” to initiate a remote connection.

![usage1](https://github.com/user-attachments/assets/3a4bb59f-c84c-44d2-9a20-11790aac510e)

If the remote desktop requires a connection password, you must enter the correct password on your side to successfully establish the connection.

![password](https://github.com/user-attachments/assets/1beadcce-640d-4f5c-8e77-51917b5294d5)

Before connecting, you can customize configuration options in the settings, such as language and video encoding format.

![settings](https://github.com/user-attachments/assets/8bc5468d-7bbb-4e30-95bd-da1f352ac08c)

## How to build

Requirements:
- [xmake](https://xmake.io/#/guide/installation)
- [cmake](https://cmake.org/download/)

Following packages need to be installed on Linux:

```
sudo apt-get install -y software-properties-common git curl unzip build-essential libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxcb-randr0-dev libxcb-xtest0-dev libxcb-xinerama0-dev libxcb-shape0-dev libxcb-xkb-dev libxcb-xfixes0-dev libxv-dev libxtst-dev libasound2-dev libsndio-dev libxcb-shm0-dev libasound2-dev libpulse-dev
```

Build:
```
git clone https://github.com/kunkundi/crossdesk.git

cd crossdesk

git submodule init 

git submodule update

xmake b crossdesk
```

#### Development Without CUDA Environment

For developers who do not have a **CUDA environment** installed, a preconfigured [Ubuntu 22.04 Docker image](https://hub.docker.com/r/crossdesk/ubuntu22.04) is provided.  
This image comes with all required build dependencies and allows you to build the project directly inside the container without any additional setup.

After entering the container, download the project and run:
```
export CUDA_PATH=/usr/local/cuda
export XMAKE_GLOBALDIR=/data

xmake b --root crossdesk
```

Run:
```
xmake r crossdesk
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
  -e EXTERNAL_IP=150.158.81.30 \
  -e INTERNAL_IP=10.0.4.3 \
  -e CROSSDESK_SERVER_PORT=9099 \
  -v /path/to/your/certs:/crossdesk-server/certs \
  -v /path/to/your/db:/crossdesk-server/db \
  -v /path/to/your/logs:/crossdesk-server/logs \
  crossdesk/crossdesk-server:latest
```

The parameters you need to pay attention to are as follows:

- **EXTERNAL_IP**: The server's public IP, corresponding to the **Server Address** in the CrossDesk client **Self-Hosted Server Configuration**.

- **INTERNAL_IP**: The server's internal IP.

- **CROSSDESK_SERVER_PORT**: The port used by the self-hosted server, corresponding to the **Server Port** in the CrossDesk client **Self-Hosted Server Configuration**.

- **/path/to/your/certs**: Directory for certificate files.

- **/path/to/your/db**: CrossDesk Server device management database.

- **/path/to/your/logs**: Log directory.

**Note**:  
- **/path/to/your/ is an example path; please replace it with your actual path. The mounted directories must be created in advance, otherwise the container will fail.**
- **The server must open the following ports: 3478/udp, 3478/tcp, 30000-60000/udp, CROSSDESK_SERVER_PORT/tcp, 443/tcp.**

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
