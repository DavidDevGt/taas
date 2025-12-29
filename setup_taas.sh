#!/bin/bash
# TaaS Deployment Script (Root Enforcement Edition)
set -e

# 1. Verificar privilegios
if [[ $EUID -ne 0 ]]; then
   echo "[!] Este script debe ejecutarse con sudo: sudo ./setup_taas.sh"
   exit 1
fi

# Definir rutas absolutas para herramientas de sistema
MODPROBE="/sbin/modprobe"
RMMOD="/sbin/rmmod"

echo "[*] Building kernel module and node..."
make clean > /dev/null
make

# 2. Gestión del módulo anterior
if lsmod | grep -q "taas_driver"; then
    echo "[*] Stopping service and removing existing module..."
    systemctl stop taas 2>/dev/null || true
    $MODPROBE -r taas_driver || $RMMOD taas_driver
fi

echo "[*] Installing artifacts..."
make install > /dev/null

echo "[*] Loading module..."
$MODPROBE taas_driver

# Esperar a que udev cree el nodo
sleep 1
if [ ! -c /dev/taas_timer ]; then
    echo "[!] Error: /dev/taas_timer no se encontró tras cargar el driver."
    exit 1
fi

echo "[*] Deploying systemd service..."
cp taas.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now taas

echo "[OK] TaaS is active and running."
systemctl status taas --no-pager | grep "Active:"
