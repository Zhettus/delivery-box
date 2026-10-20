/*
 * mic_ros.h — The microphone, streamed live to ROS 2 over micro-ROS.
 *
 * From the mic hand-off (mic_ros_handoff/component_tests/mic_ros), as part of
 * the box firmware. Transport: the ESP32-S3 native USB port ("USB" USB-C),
 * custom XRCE-DDS transport in usb_jtag_transport.c. Logs go to UART0 (the
 * "UART"/"COM" USB-C port) instead, because nothing else may write to the
 * native USB port.
 *
 * Node "delivery_box", topics (prefix MIC_ROS_TOPIC_PREFIX, config.h):
 *   <prefix>/enable  std_msgs/Bool             in,  reliable : true = stream, false = stop
 *   <prefix>/status  std_msgs/Bool             out, reliable : the real state; on change + every 1 s
 *   <prefix>/audio   std_msgs/Int16MultiArray  out, best effort
 *        data              160 samples = 10 ms of 16 kHz mono int16
 *        layout.data_offset index of the first sample since the stream started
 *                           (the receiver fills lost chunks with silence)
 *
 * The microphone is OFF after boot and after every (re)connection to the
 * agent. On the computer: tools/listen_live.py (see README.md).
 *
 * Built only when components/micro_ros_espidf_component exists (MIC_ROS=1).
 */
#pragma once

#include "esp_err.h"

/** Start the capture and ROS tasks. The audio component must be running. */
esp_err_t mic_ros_start(void);
