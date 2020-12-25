/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include <soundio/soundio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const int pcm_bytes = 1152;

typedef struct ScreamHeader {
    char sample_rate;
    char bit_depth;
    char channels;
    char channelMaskH;
    char channelMaskL;
} ScreamHeader;

static bool quit = false;

struct RecordContext {
    struct SoundIoRingBuffer *ring_buffer;
};

static enum SoundIoFormat prioritized_formats[] = {
    SoundIoFormatS16NE,
    SoundIoFormatS16FE,
    };

static int prioritized_sample_rates[] = {
    44100
};

static int min_int(int a, int b) {
    return (a < b) ? a : b;
}

static void read_callback(struct SoundIoInStream *instream, int frame_count_min, int frame_count_max) {
    struct RecordContext *rc = instream->userdata;
    struct SoundIoChannelArea *areas;
    int err;

    char *write_ptr = soundio_ring_buffer_write_ptr(rc->ring_buffer);
    int free_bytes = soundio_ring_buffer_free_count(rc->ring_buffer);
    int free_count = free_bytes / instream->bytes_per_frame;

    if (free_count < frame_count_min) {
        fprintf(stderr, "ring buffer overflow\n");
        exit(1);
    }

    int write_frames = min_int(free_count, frame_count_max);
    int frames_left = write_frames;

    for (;;) {
        int frame_count = frames_left;

        if ((err = soundio_instream_begin_read(instream, &areas, &frame_count))) {
            fprintf(stderr, "begin read error: %s", soundio_strerror(err));
            exit(1);
        }

        if (!frame_count)
            break;

        if (!areas) {
            // Due to an overflow there is a hole. Fill the ring buffer with
            // silence for the size of the hole.
            memset(write_ptr, 0, frame_count * instream->bytes_per_frame);
        } else {
            for (int frame = 0; frame < frame_count; frame += 1) {
                for (int ch = 0; ch < instream->layout.channel_count; ch += 1) {
                    memcpy(write_ptr, areas[ch].ptr, instream->bytes_per_sample);
                    areas[ch].ptr += areas[ch].step;
                    write_ptr += instream->bytes_per_sample;
                }
            }
        }

        if ((err = soundio_instream_end_read(instream))) {
            fprintf(stderr, "end read error: %s", soundio_strerror(err));
            exit(1);
        }

        frames_left -= frame_count;
        if (frames_left <= 0)
            break;
    }

    int advance_bytes = write_frames * instream->bytes_per_frame;
    soundio_ring_buffer_advance_write_ptr(rc->ring_buffer, advance_bytes);
}

static void overflow_callback(struct SoundIoInStream *instream) {
    static int count = 0;
    fprintf(stderr, "overflow %d\n", ++count);
}

static int usage(char *exe) {
    fprintf(stderr, "Usage: %s [options] outfile\n"
            "Options:\n"
            "  [--backend dummy|alsa|pulseaudio|jack|coreaudio|wasapi]\n"
            "  [--device id]\n"
            "  [--raw]\n"
            "  [--rate sample_rate]\n"
            "  [--mgroup group_address]\n"
            "  [--port udp_port]\n"
            , exe);
    return 1;
}

void stop_signal_handler(int signal) {
    quit = true;
}

int main(int argc, char **argv) {
    if (signal(SIGINT, stop_signal_handler)==SIG_ERR)
        fprintf(stderr,"Error registering handler for SIGINT.");
    if (signal(SIGTERM, stop_signal_handler)==SIG_ERR)
        fprintf(stderr,"Error registering handler for SIGKILL.");

    char *exe = argv[0];
    enum SoundIoBackend backend = SoundIoBackendNone;
    char *device_id = NULL;
    bool is_raw = false;

    struct ScreamHeader header = {
            .sample_rate = 0x81,
            .bit_depth = 16,
            .channels = 2,
            .channelMaskL = 0x01 | 0x02, // front left & front right
    };

    char* mgroup_addr = "239.255.77.77";
    int port = 4010;

    for (int i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') {
            if (strcmp(arg, "--raw") == 0) {
                is_raw = true;
            } else if (++i >= argc) {
                return usage(exe);
            } else if (strcmp(arg, "--backend") == 0) {
                if (strcmp("dummy", argv[i]) == 0) {
                    backend = SoundIoBackendDummy;
                } else if (strcmp("alsa", argv[i]) == 0) {
                    backend = SoundIoBackendAlsa;
                } else if (strcmp("pulseaudio", argv[i]) == 0) {
                    backend = SoundIoBackendPulseAudio;
                } else if (strcmp("jack", argv[i]) == 0) {
                    backend = SoundIoBackendJack;
                } else if (strcmp("coreaudio", argv[i]) == 0) {
                    backend = SoundIoBackendCoreAudio;
                } else if (strcmp("wasapi", argv[i]) == 0) {
                    backend = SoundIoBackendWasapi;
                } else {
                    fprintf(stderr, "Invalid backend: %s\n", argv[i]);
                    return 1;
                }
            } else if (strcmp(arg, "--device") == 0) {
                device_id = argv[i];
            } else if (strcmp(arg, "--rate") == 0) {
                sscanf(argv[i], "%d", prioritized_sample_rates);
            } else if (strcmp(arg, "--mgroup_addr") == 0) {
                mgroup_addr = argv[i];
            } else if (strcmp(arg, "--port") == 0) {
                sscanf(argv[i], "%d", &port);
            } else {
                return usage(exe);
            }
        } else {
            return usage(exe);
        }
    }

    struct RecordContext rc;

    struct SoundIo *soundio = soundio_create();
    if (!soundio) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    int err = (backend == SoundIoBackendNone) ?
        soundio_connect(soundio) : soundio_connect_backend(soundio, backend);
    if (err) {
        fprintf(stderr, "error connecting: %s", soundio_strerror(err));
        return 1;
    }

    soundio_flush_events(soundio);

    struct SoundIoDevice *selected_device = NULL;

    if (device_id) {
        for (int i = 0; i < soundio_input_device_count(soundio); i += 1) {
            struct SoundIoDevice *device = soundio_get_input_device(soundio, i);
            if (device->is_raw == is_raw && strcmp(device->id, device_id) == 0) {
                selected_device = device;
                break;
            }
            soundio_device_unref(device);
        }
        if (!selected_device) {
            fprintf(stderr, "Invalid device id: %s\n", device_id);
            return 1;
        }
    } else {
        int device_index = soundio_default_input_device_index(soundio);
        selected_device = soundio_get_input_device(soundio, device_index);
        if (!selected_device) {
            fprintf(stderr, "No input devices available.\n");
            return 1;
        }
    }

    printf("Device: %s\n", selected_device->name);

    if (selected_device->probe_error) {
        fprintf(stderr, "Unable to probe device: %s\n", soundio_strerror(selected_device->probe_error));
        return 1;
    }

    soundio_device_sort_channel_layouts(selected_device);

    int sample_rate = 0;
    int *sample_rate_ptr;
    for (sample_rate_ptr = prioritized_sample_rates; *sample_rate_ptr; sample_rate_ptr += 1) {
        if (soundio_device_supports_sample_rate(selected_device, *sample_rate_ptr)) {
            sample_rate = *sample_rate_ptr;
            break;
        }
    }
    if (!sample_rate)
        sample_rate = selected_device->sample_rates[0].max;

    if (sample_rate % 44100 == 0) {
        header.sample_rate = 128 | (sample_rate / 44100);
    } else if (sample_rate % 48000 == 0) {
        header.sample_rate = sample_rate / 48000;
    } else {
        fprintf(stderr, "unexpected sample rate: %d\n", sample_rate);
        return 1;
    }

    enum SoundIoFormat fmt = SoundIoFormatInvalid;
    enum SoundIoFormat *fmt_ptr;
    for (fmt_ptr = prioritized_formats; *fmt_ptr != SoundIoFormatInvalid; fmt_ptr += 1) {
        if (soundio_device_supports_format(selected_device, *fmt_ptr)) {
            fmt = *fmt_ptr;
            break;
        }
    }
    if (fmt == SoundIoFormatInvalid)
        fmt = selected_device->formats[0];

    struct SoundIoInStream *instream = soundio_instream_create(selected_device);
    if (!instream) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    instream->format = fmt;
    instream->sample_rate = sample_rate;
    instream->read_callback = read_callback;
    instream->overflow_callback = overflow_callback;
    instream->userdata = &rc;

    if ((err = soundio_instream_open(instream))) {
        fprintf(stderr, "unable to open input stream: %s", soundio_strerror(err));
        return 1;
    }

    header.bit_depth = soundio_get_bytes_per_sample(fmt) * 8;

    printf("%s %dHz %s interleaved\n",
            instream->layout.name, sample_rate, soundio_format_string(fmt));

    const int ring_buffer_duration_seconds = 30;
    int capacity = ring_buffer_duration_seconds * instream->sample_rate * instream->bytes_per_frame;
    rc.ring_buffer = soundio_ring_buffer_create(soundio, capacity);
    if (!rc.ring_buffer) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    if ((err = soundio_instream_start(instream))) {
        fprintf(stderr, "unable to start input device: %s", soundio_strerror(err));
        return 1;
    }

    int socket_descriptor;
    struct sockaddr_in address;
    socket_descriptor = socket (AF_INET, SOCK_DGRAM, 0);
    if (socket_descriptor == -1) {
        perror ("socket()");
        exit (EXIT_FAILURE);
    }
    memset (&address, 0, sizeof (address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr (mgroup_addr);
    address.sin_port = htons (port);
    printf ("Broadcasting on %s:%d\n", mgroup_addr, port);

    size_t packet_size = sizeof(struct ScreamHeader) + pcm_bytes;
    char* buf = malloc(packet_size);
    memcpy(buf, &header, sizeof(struct ScreamHeader));

    while (!quit) {
        int buffered_bytes = 0;

        while (buffered_bytes < pcm_bytes) {
            soundio_flush_events(soundio);



            int fill_bytes = soundio_ring_buffer_fill_count(rc.ring_buffer);
            char *read_buf = soundio_ring_buffer_read_ptr(rc.ring_buffer);

            if (fill_bytes > pcm_bytes - buffered_bytes)
                fill_bytes = pcm_bytes - buffered_bytes;
            else
                usleep(5000);

            memcpy(buf + sizeof header + buffered_bytes, read_buf, fill_bytes);
            buffered_bytes += fill_bytes;
            soundio_ring_buffer_advance_read_ptr(rc.ring_buffer, fill_bytes);
        }

        size_t amt = sendto(socket_descriptor,
                            buf,
                            packet_size,
                            0,
                            (struct sockaddr *) &address,
                            sizeof (address));
        if ((int) amt != packet_size) {
            fprintf(stderr, "write error: %s, amt: %d, expected: %d \n", strerror(errno), (int)amt, (int)packet_size);
            return 1;
        }
    }
    printf("shutting down...");

    free(buf);
    soundio_instream_destroy(instream);
    soundio_device_unref(selected_device);
    soundio_destroy(soundio);
    return 0;
}

