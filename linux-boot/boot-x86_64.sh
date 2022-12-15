#!/bin/bash

export LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8

# Download dependencies
mkdir -p deps/x86_64
wget -P deps/x86_64 -N https://www.busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox
wget -P deps/x86_64 -N https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/x86_64/netboot/vmlinuz-lts

# Create initrd
mkdir -p initrd/x86_64 initrd/x86_64/bin
cp deps/x86_64/busybox initrd/x86_64/bin/busybox
cat > initrd/x86_64/init <<'EOF'
#!/bin/busybox sh
/bin/busybox --install -s /bin
export PATH="/bin"
exec sh -i
EOF
chmod +x initrd/x86_64/init initrd/x86_64/bin/busybox

(cd initrd/x86_64 && find . | cpio --quiet -R 0:0 -o -H newc) | gzip > initrd/x86_64.gz

exec qemu-system-x86_64 -m 64M \
    -kernel ./deps/x86_64/vmlinuz-lts \
    -initrd ./initrd/x86_64.gz \
    -append 'console=ttyS0'\
    -nographic

