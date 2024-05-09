#!/bin/sh
rm -rf ./build
# shellcheck disable=SC2164
mkdir build && cd build && cmake .. && make
echo "build done,coping plugin ..."
cp -r ./libplugin-mqtt-quic.so /home/songyibao/Downloads/neuron-main/build/plugins/
# 拷贝 .so 和 json配置 文件到 系统安装的 neuron 目录
cp -r ./libplugin-mqtt-quic.so /opt/neuron/plugins/
cp -r ../mqtt-quic.json /opt/neuron/plugins/schema/
echo "copy done, refresh http://127.0.0.1:7000"
