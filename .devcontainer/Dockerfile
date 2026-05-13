# Wir erzwingen die x86_64 Plattform
FROM --platform=linux/amd64 mcr.microsoft.com/devcontainers/python:3.13

# Installiere System-Abhängigkeiten für PlatformIO und Compiler
RUN apt-get update && apt-get install -y \
    build-essential \
    bc \
    curl \
    python3-venv \
    && apt-get clean

# Installiere PlatformIO Core global im Container
RUN pip3 install --upgrade platformio