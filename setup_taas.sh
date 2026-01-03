#!/bin/bash
set -e

if [[ $EUID -ne 0 ]]; then
   echo "[!] No sudo detected, use: sudo ./setup_taas.sh"
   exit 1
fi

MODPROBE="/sbin/modprobe"
RMMOD="/sbin/rmmod"

echo "[*] Building kernel module and node..."
make clean > /dev/null
make

if lsmod | grep -q "taas_driver"; then
    echo "[*] Stopping service and removing existing module..."
    systemctl stop taas 2>/dev/null || true
    $MODPROBE -r taas_driver || $RMMOD taas_driver
fi

echo "[*] Installing artifacts..."
make install > /dev/null

echo "[*] Loading module..."
$MODPROBE taas_driver

sleep 1
if [ ! -c /dev/taas_timer ]; then
    echo "[!] Error: /dev/taas_timer not found."
    exit 1
fi

echo "[*] Deploying systemd service..."
cp taas.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now taas

echo "[OK] TaaS is active and running."
systemctl status taas --no-pager | grep "Active:"
