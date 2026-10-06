#!/bin/sh
# Installs the build tools and the PS5 payload SDK inside WSL (Ubuntu).
set -e
sudo apt-get update
sudo apt-get install -y bash clang lld make wget unzip file xxd
if [ ! -d /opt/ps5-payload-sdk ]; then
    cd /tmp
    wget -q -O ps5-payload-sdk.zip https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip
    sudo unzip -q -o ps5-payload-sdk.zip -d /opt
fi
ls /opt/ps5-payload-sdk
echo "export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk" > "$HOME/.ps5sdk"
echo ok
