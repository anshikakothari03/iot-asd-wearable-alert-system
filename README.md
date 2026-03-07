# IoT Wearable System for Monitoring ASD Patients

This project presents a wearable IoT system designed to assist individuals with Autism Spectrum Disorder (ASD). The device monitors heart rate, movement patterns, and location in real time and sends alerts to caregivers if unusual behavior is detected.

The system is built using an ESP32 microcontroller connected to multiple sensors. By combining physiological signals and movement data, the device can detect conditions such as anxiety, restlessness, or unsafe wandering.

## Hardware Used

* ESP32 microcontroller
* MAX30102 pulse oximeter (heart rate monitoring)
* MPU6500 IMU sensor (movement detection)
* NEO-6M GPS module (location tracking)

## How the System Works

The sensors continuously collect heart rate, motion, and GPS data. The ESP32 processes this data and checks for certain conditions such as increased heart rate, rapid hand movement, or movement outside a predefined safe zone.

When these conditions are detected, the system sends an alert to caregivers using a Telegram bot. At the same time, the collected data is uploaded to the ThingSpeak cloud platform so that therapists or caregivers can analyze behavior patterns over time.

## Features

* Real-time heart rate monitoring
* Detection of repetitive hand movements
* GPS-based safety tracking
* Telegram alerts for caregivers
* Cloud data logging using ThingSpeak

## Purpose

The goal of this project is to provide a compact and affordable assistive device that can help caregivers monitor individuals with ASD and respond quickly during situations of anxiety or distress.

## Author

Anshika Kothari


