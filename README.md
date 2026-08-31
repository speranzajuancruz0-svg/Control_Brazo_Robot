# 🤖 Brazo Robótico de 5 GDL con Control MQTT

Este proyecto consiste en el desarrollo y control inalámbrico de un **brazo robótico articulado de 5 grados de libertad (GDL)** con efector final (pinza). El sistema permite comandar los movimientos del robot en tiempo real mediante mensajes MQTT a través de WiFi.

---

## 🎯 Objetivos del Proyecto
* **Teleoperación remota:** Controlar cada articulación individualmente o posicionar el extremo en coordenadas $(x, y, z)$.
* **Integración IoT:** Recibir órdenes desde dashboards como Node-RED, aplicaciones móviles o Python mediante el protocolo MQTT.
* **Respuesta en tiempo real:** Procesar comandos JSON para mover servomotores con precisión y suavidad.

---

## ⚙️ Estructura del Robot

| Articulación | Función | Rango de Movimiento |
| :--- | :--- | :---: |
| **1. Base** | Rotación horizontal (Eje Z) | 0° a 180° |
| **2. Hombro** | Elevación y alcance | 15° a 165° |
| **3. Codo** | Extensión del brazo | 0° a 180° |
| **4. Muñeca** | Orientación de la herramienta | 0° a 180° |
| **5. Gripper** | Apertura y cierre de la pinza | 0° a 90° |

---

## 📡 Control por MQTT

El microcontrolador (ESP32) se conecta al broker MQTT y escucha los comandos enviados en formato JSON.
