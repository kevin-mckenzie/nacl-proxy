FROM ubuntu:24.04

RUN apt update && apt upgrade -y && \
    apt install -y --no-install-recommends \
        build-essential \
        python3 \
        python3-pip \
        python3-venv \
        ninja-build \
        qemu-user-static \
        curl \
        wget \
        valgrind \
        cpcheck \
    apt clean && \
    rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/venv
ENV PATH=/opt/venv/bin:$PATH
    
RUN python3 -m ensurepip --upgrade
COPY requirements.txt .
RUN python3 -m pip install -r requirements.txt

RUN mkdir -p /opt/cross

COPY tasks.py .
RUN inv install-toolchains
RUN rm -f tasks.py requirements.txt

WORKDIR /project
CMD ["/bin/bash"]
