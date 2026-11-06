#!/usr/bin/env bash
# Optik build script.  ./build.sh   then   ./build/optik_selftest && build/optik
#
# Needs (Fedora):  sudo dnf install gtkmm4.0-devel tesseract-devel leptonica-devel \
#                    libzip-devel spdlog-devel tesseract-langpack-eng cmake gcc-c++ \
#                    hunspell-devel hunspell-en-US
# Needs (Ubuntu):  sudo apt install libgtkmm-4.0-dev libtesseract-dev libleptonica-dev \
#                    libzip-dev libspdlog-dev tesseract-ocr-eng cmake g++ \
#                    libhunspell-dev hunspell-en-us
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"
cmake -B build
cmake --build build -j"$(nproc)"
echo ""
echo "Built: ./build/optik  ./build/optik_selftest"
