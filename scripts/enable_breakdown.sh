#!/bin/bash

echo 1 | sudo tee /sys/module/core/parameters/enable_vcpu_switch_num

cat /sys/module/core/parameters/enable_vcpu_switch_num
cat /sys/module/core/parameters/vcpu_switch_num
