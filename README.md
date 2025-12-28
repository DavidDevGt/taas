![platform](https://img.shields.io/badge/platform-Raspberry%20Pi%20Zero%202%20W-red)
![kernel](https://img.shields.io/badge/kernel-v7%20/64--bit-green)
![license](https://img.shields.io/badge/license-GPLv2-red)
![realtime](https://img.shields.io/badge/realtime-SCHED_FIFO-critical)

# TaaS — Time as a Service

**High-Precision Hardware Timestamping & PTP Node optimized for Raspberry Pi Zero 2 W**

---

## 📌 Visión General

**TaaS (Time as a Service)** es una solución de sincronización de tiempo de **ultra-alta precisión**, diseñada para sistemas embebidos que requieren determinismo absoluto.

Aprovecha el **System Timer de 64 bits del SoC BCM2837** presente en la **Raspberry Pi Zero 2 W**, exponiéndolo directamente desde el kernel al espacio de usuario. Esto permite obtener marcas de tiempo eliminando el *jitter* de las syscalls tradicionales de Linux.

> 🎯 **Misión:** Proporcionar tiempo puro de hardware con latencia mínima para aplicaciones de infraestructura crítica y monitoreo industrial.

---

## 🧩 Arquitectura del Sistema




```

┌────────────────────────────┐
│      Hardware (BCM2837)    │
│  System Timer 64-bit (ST)  │
└───────────────┬────────────┘
│ MMIO (Direct Access)
┌───────────────▼────────────┐
│   Kernel Module (taas)     │
│   - ioremap ST registers   │
│   - /dev/taas_timer        │
│   - mmap zero-copy API     │
└───────────────┬────────────┘
│ Page Mapping
┌───────────────▼────────────┐
│    User Space Node         │
│    - SCHED_FIFO RT         │
│    - UDP PTP (Port 1588)   │
│    - 64-bit RAW timestamp  │
└────────────────────────────┘

```

---

## 🚀 Componentes

### 1️⃣ Kernel Driver — `taas_driver`
Módulo de kernel que mapea los registros de hardware del SoC.
* **Dispositivo:** `/dev/taas_timer`
* **Acceso:** Implementa `mmap` para permitir que el nodo de usuario lea el timer sin entrar en modo kernel (cero cambios de contexto).

### 2️⃣ Nodo PTP — `taas_node`
Daemon de tiempo real que sirve el tiempo sobre la red.
* **Protocolo:** UDP custom (PTP-like).
* **Prioridad:** `SCHED_FIFO 99` (Máxima prioridad de tiempo real en Linux).

---

## 🛠️ Instalación y Despliegue

### Requisitos
* Raspberry Pi Zero 2 W (o RPi 3).
* Raspberry Pi OS (probado en Debian 13 "Trixie").
* Kernel headers instalados.

### Compilación rápida
```bash
make

```

### Instalación Automática

Utiliza el script de despliegue para configurar el servicio y las reglas de hardware:

```bash
chmod +x setup_taas.sh
sudo ./setup_taas.sh

```

---

## ⚙️ Compatibilidad Verificada

| Componente | Detalle |
| --- | --- |
| **Hardware** | Raspberry Pi Zero 2 W Rev 1.0 ✅ |
| **SoC** | BCM2837 (4 cores @ 1.00 GHz) ✅ |
| **Arquitectura** | armv7l (32-bit) / aarch64 (64-bit) ✅ |
| **OS** | Raspbian GNU/Linux 13 (trixie) ✅ |
| **Kernel** | Linux 6.12.47+rpt-rpi-v7 ✅ |

---

## 🧪 Pruebas de Funcionamiento (Verification)

Para verificar que el nodo está respondiendo con el timestamp de 64 bits del hardware, puedes usar `netcat` y `hexdump`:

```bash
# Envía un trigger al puerto 1588
echo -n "ping" | nc -u -w 1 127.0.0.1 1588 | hexdump -C

```

**Salida esperada:**

```hexdump
00000000  04 f1 96 71 00 00 00 00  |...q....|

```

*(Los primeros 8 bytes representan el valor actual del System Timer en formato Little Endian)*.

---

## 📜 Licencia

Distribuido bajo la licencia **GPL v2**.

---

## 🧠 Filosofía

> "El tiempo no se solicita al sistema operativo; se extrae directamente del silicio."


