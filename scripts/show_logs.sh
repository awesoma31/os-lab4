#!/bin/bash
sudo dmesg | grep -E "\[vtfs\]" | tail -100
