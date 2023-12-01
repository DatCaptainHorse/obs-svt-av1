# OBS SVT-AV1 Encoder Plugin

## Introduction

This plugin adds SVT-AV1 encoder to OBS Studio, using it directly rather than through FFmpeg.

!!! Only streaming works for now, recording will get stuck and cause OBS to live in background !!!

## Building

Get libobs and obs-frontent-api from your package manager or build and install them from source.

Get SVT-AV1 from your package manager or build and install it from source.

Configure using CMake preset (linux-x86_64, windows-x64, macos..)

Build and copy `obs-svt-av1.so/dll` to your OBS-Studio install's `obs-plugins` directory.
