FROM ubuntu:22.04
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
       make gcc gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf \
       gdb-multiarch qemu-system-misc opensbi ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
CMD ["sleep", "infinity"]
