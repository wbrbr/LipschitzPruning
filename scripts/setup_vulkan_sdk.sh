#!/bin/bash

sdk_version=$(curl https://vulkan.lunarg.com/sdk/latest/linux.txt)

curl -O https://sdk.lunarg.com/sdk/download/latest/linux/vulkan_sdk.tar.xz


tar xf vulkan_sdk.tar.xz
mv $sdk_version sdk