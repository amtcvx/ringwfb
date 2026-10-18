#!/bin/bash
PROJ=$PWD

sudo systemctl stop wfb.service
sudo systemctl disable wfb.service
sudo rm /etc/systemd/system/wfb.service

sudo rm /etc/NetworkManager/conf.d/unmanaged.conf
sudo systemctl reload NetworkManager.service
sudo systemctl daemon-reload

sudo rm /etc/udev/rules.d/60-wfb.rules
sudo udevadm control --reload-rules
