# iperf3:  A TCP, UDP, SCTP, and VSOCK network bandwidth measurement tool

This repository is a fork of [iperf3](https://github.com/esnet/iperf) with
VSOCK support.

The VSOCK address family facilitates communication between virtual
machines and the host they are running on.

To test VSOCK sockets (only Linux), you must use the new option `--vsock` on
both server and client.
Other iperf3 options (e.g. `-t, -l, -P, -R, --bidir`) are well supported by
VSOCK tests.

## Changes from upstream iperf 3.x

New options:

    --vsock                   use VSOCK address family rather than INET (Linux)

## Build

### Prerequisites:

* Linux kernel >= 4.8

### Clone repository

```shell
git clone https://github.com/stefano-garzarella/iperf-vsock
cd iperf-vsock
```

### Building

```shell
mkdir build
cd build
../configure
make
cd src
```

(Note: If configure fails, try running `./bootstrap.sh` first)

## Example with Fedora (host and guest):

### Host: start the VM

```shell
GUEST_CID=3
sudo modprobe vhost_vsock
sudo qemu-system-x86_64 -m 1G -smp 2 -cpu host -M accel=kvm	\
     -drive if=virtio,file=/path/to/fedora.img,format=qcow2     \
     -device vhost-vsock-pci,guest-cid=${GUEST_CID}
```

### Guest: start iperf server

```shell
# SELinux can block you, so you can write a policy or temporally disable it
sudo setenforce 0
iperf3 --vsock -s
```

### Host: start iperf client

```shell
iperf3 --vsock -c ${GUEST_CID}
```

### Output

```shell
Connecting to host 3, port 5201
[  5] local 2 port 4008596529 connected to 3 port 5201
[ ID] Interval           Transfer     Bitrate
[  5]   0.00-1.00   sec  1.30 GBytes  11.2 Gbits/sec
[  5]   1.00-2.00   sec  1.67 GBytes  14.3 Gbits/sec
[  5]   2.00-3.00   sec  1.57 GBytes  13.5 Gbits/sec
[  5]   3.00-4.00   sec  1.49 GBytes  12.8 Gbits/sec
[  5]   4.00-5.00   sec   971 MBytes  8.15 Gbits/sec
[  5]   5.00-6.00   sec  1.01 GBytes  8.71 Gbits/sec
[  5]   6.00-7.00   sec  1.44 GBytes  12.3 Gbits/sec
[  5]   7.00-8.00   sec  1.62 GBytes  13.9 Gbits/sec
[  5]   8.00-9.00   sec  1.61 GBytes  13.8 Gbits/sec
[  5]   9.00-10.00  sec  1.63 GBytes  14.0 Gbits/sec
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate
[  5]   0.00-10.00  sec  14.3 GBytes  12.3 Gbits/sec                  sender
[  5]   0.00-10.00  sec  14.3 GBytes  12.3 Gbits/sec                  receiver

iperf Done.
```

## Firecracker's hybrid VSOCK over AF_UNIX

Firecracker, Cloud Hypervisor, and QEMU's vhost-user-vsock implement the
virtio-vsock device model in userspace, and they allow the host to reach
the guest using AF_UNIX sockets.
More info [here](https://github.com/firecracker-microvm/firecracker/blob/master/docs/vsock.md)

`iperf-vsock` supports **VSOCK over AF_UNIX**. In order to use it,
you can replace the guest CID with the UDS (Unix Domain Socket) base path.

In the following examples, the UDS base path to reach the guest is `/tmp/vm.vsock`

### Host runs iperf client

#### guest

```shell
iperf3 --vsock -s
```

#### host

```shell
iperf3 --vsock -c /tmp/vm.vsock
```

### Host runs iperf server

#### host

```shell
iperf3 --vsock -s -B /tmp/vm.vsock
```

#### guest

```shell
iperf3 --vsock -c 2
```