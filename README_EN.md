# CrossDesk

#### Bridging work, uniting efficiency

----
[中文](README_CN.md) / [English](README.md)

![sup_example](https://github.com/user-attachments/assets/3f17d8f3-7c4a-4b63-bae4-903363628687)

# Intro

CrossDesk is a lightweight cross-platform remote desktop software.

CrossDesk is an experimental application of [MiniRTC](https://github.com/kunkundi/minirtc.git), a lightweight cross-platform real-time audio and video transmission library. MiniRTC provides fundamental capabilities including network traversal ([RFC5245](https://datatracker.ietf.org/doc/html/rfc5245)), video software/hardware encoding and decoding (H264/AV1), audio encoding/decoding ([Opus](https://github.com/xiph/opus)), signaling interaction, network congestion control, and transmission encryption ([SRTP](https://tools.ietf.org/html/rfc3711)).

## Usage

Enter the remote desktop ID in the menu bar’s “Remote ID” field and click “→” to initiate a remote connection.

![usage1](https://github.com/user-attachments/assets/bf39f8fa-de77-41a1-8db3-73d6cab9da6a)

If the remote desktop requires a connection password, you must enter the correct password on your side to successfully establish the connection.

![password](https://github.com/user-attachments/assets/f6556966-a84f-4301-a79b-2726b389ed71)

Before connecting, you can customize configuration options in the settings, such as language and video encoding format.

![settings](https://github.com/user-attachments/assets/12f7e9c3-a472-40c1-8fb9-ae7d1ae3865c)

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

<img width="108" height="57" alt="image" src="https://github.com/user-attachments/assets/26e8b9f3-b326-410e-9466-dd073eaf675a" />

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
