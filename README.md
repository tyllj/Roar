# Roar - A Scream-like network sound service for macOS.

[Scream by duncanthrax](https://github.com/duncanthrax/scream) is an audio driver for Windows which sinks a PCM stream into a UDP multicast stream. It has receivers available for Linux (PulseAudio, ALSA and JACK) as well as Windows. This makes it very helpful to get sound from virtual machines on Linux hosts when you have issues with QEMUs emulated audio devices.
Roar was written to achieve the same goal on macOS guests and it is compatible with Scream receivers. It can also be used to route audio from a physical macOS machine to another computer, but since the stream is uncompressed, it requires a lot of network bandwith and can be choppy over wireless. There is no reason, why Roar wouldn't run on Linux or Windows guests as well, however there are already better solutions for both of these.

## Setup
### Prerequisits
Roar runs entirely in userspace and does not provide its own audio driver. Instead you'll need some kind of loopback driver. A loopback sound driver provides a virtual speaker output and passes the system audio into a virtual microphone input. Therefore it allows routing audio between applications. [BlackHole](https://github.com/ExistentialAudio/BlackHole) works great, although others are available. To build from source you'll also need  [libsoundio](https://github.com/andrewrk/libsoundio) and [cmake](https://github.com/Kitware/CMake) installed on your system. I recommend installing those dependencies with [brew](https://brew.sh/). To run roar, there has to be a virtual network connection between your host and guest system. Also open a UDP port in your firewall for this connection: `4010` is default for Scream and Roar, but it can be changed if this is desired. I recommend using `vmxnet3` as a paravirtualized network interface, it should have a smaller cpu load and less latency, compared to the emulated `e1000` NIC. `virtio-net` is currently not supported on macOS.

### Installing a receiver
Follow the instructions at https://github.com/duncanthrax/scream/blob/master/README.md on how to install the Scream client on your host.

### Installing dependencies
Head over to https://brew.sh/ to install the package manager, if you don't use it already.
Then in a Terminal run:
```
brew install cmake libsoundio blackhole-2ch
```

### Building Roar
First clone this repository into a folder of your choice:
```
git clone https://github.com/tyllj/Roar
```

Let cmake create the make files for compilation:
```
cd Roar
mkdir build
cd build
cmake ../
```
Then compile the application:
```
make
```

You can now run the application with:
```
./roar
```
or install it to `/usr/local/bin/roar` with:
```
make install
```
## Using Roar
First start the Scream receiver on your host, e.g.:
```
./scream -i 192.168.122.1 -o pulse
```
where 192.168.122.1 is the hosts address of the virtual network interface.

Find the device id for the input of your virual sound card:
```
sio_list_devices
```
For BlackHole this will be `BlackHole2ch_UID`.

Then start Roar with:
```
roar --device <device id>
```
e.g.:
```
roar --device BlackHole2ch_UID
```

Remember to select your loopback device as the system output. Now guests audio should be routed to the host.

You can customize some settings using the the following command line flags:
```
Options:
  [--backend dummy|alsa|pulseaudio|jack|coreaudio|wasapi]
  [--device id]
  [--raw]
  [--rate sample_rate]
  [--mgroup group_address]
  [--port udp_port]
```
The default multicast address is `239.255.77.77:4010`, with 44100Hz sample rate.
You can stream whatever sample rate is supported by your loopback driver (up to 192kHz), however only 16-bit stereo is currently supported.

### Custom launch script
To run Scream and Roar on VM startup for a seamless experience, you can create a launch script which you add to the Login Items in the macOS System Preferences.
```
#!/bin/sh
/usr/bin/ssh 192.168.122.1 "/path/to/scream/Receivers/unix/build/scream -i 192.168.122.1 -o pulse" &
/usr/local/bin/roar --device BlackHole2ch_UID &
```

This requires public key auth for ssh. If you have not done so already, you can set this up with:
```
ssh-copy-id 192.168.122.1
```
