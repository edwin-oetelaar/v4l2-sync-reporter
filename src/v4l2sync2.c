/*
 * v4l2sync.c
 * License GPL2 (you need to comply!!)
 * $Id:$
 * Make sure we stream a channel that really has a picture.
 * 
 * The BT878 chip has 4 inputs, any of them can have a camera and manual select
 * of the camera can fail. 
 * User can put cable on other BNC/SVHS inputs.
 * I think the software should detect the selected input, not a thing
 * the user should figure out and set in a configuration file.
 * 
 * This small program opens the video4linux2 device and checks all
 * the inputs if any of them has a sync/picture (camera) connected
 * It then selects this input and exits.
 * If no camera is detected it will try for while and exit with error.
 * This can be used in scripts for reporting to the user.
 * It can also keep running and report the status of the inputs on screen
 * for easy troubleshooting during installation.
 * 
 * Typical use:
 * run this program to wait/detect camera before starting ffmpeg or vlc 
 * video streaming (encoding) of the signal.
 * If VLC or FFMPEG start encoding from video device without proper sync,
 * problems will happen later. 
 * Audio/Video not synced, wrong timestamps in video stream etc.
 * 
 * Written by Edwin van den Oetelaar ( edwin (@) oetelaar (.) com )
 * April/Mai 2012, first attempt at hacking up some v4l2 applications
 * Warranties : none. If this program destroys you computer.. sorry 
 * but it is your problem.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev.h>
#include <stdint.h>

struct v4l2_input g_vid_input;
struct v4l2_output g_vid_output;

char *g_dev_name = "/dev/video0"; // default name
char *g_inputtype_lookup[] = {"unknown", "tuner", "camera", 0, 0, 0, 0};

int g_timeout = 300; // default timeout in seconds

int g_have_signal_flag = 0; // flag
int g_active_input_index = -1; // input number
int g_verbose = 1; // verbose default
int g_quick_exit = 1; // quick exit default

int open_device(const char *dev_name) {
    struct stat st;

    if (-1 == stat(dev_name, &st)) {
        fprintf(stderr, "Cannot identify '%s': %d, %s\n", dev_name, errno,
                strerror(errno));
        return (-1);
    }

    if (!S_ISCHR(st.st_mode)) {
        fprintf(stderr, "%s is no device\n", dev_name);
        return (-1);
    }

    int fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

    if (-1 == fd) {
        fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno,
                strerror(errno));
        return (-1);
    }
    return fd;
}

char *status_to_text(int s, char *b, int len) {
    memset(b, 0, len);

    if (s & V4L2_IN_ST_NO_POWER)
        strcat(b, "NoPower ");
    if (s & V4L2_IN_ST_NO_SIGNAL)
        strcat(b, "NoSignal ");
    if (s & V4L2_IN_ST_NO_COLOR)
        strcat(b, "NoColor ");

    if (s & V4L2_IN_ST_HFLIP)
        strcat(b, "H-Flip "); // not a problem
    if (s & V4L2_IN_ST_VFLIP)
        strcat(b, "V-Flip "); // not a problem

    if (s & V4L2_IN_ST_NO_H_LOCK)
        strcat(b, "No-H-Lock ");
    if (s & V4L2_IN_ST_COLOR_KILL)
        strcat(b, "ColorKilled ");

    if (s & V4L2_IN_ST_NO_SYNC)
        strcat(b, "NoSyncLock ");
    if (s & V4L2_IN_ST_NO_EQU)
        strcat(b, "NoEquilizer ");
    if (s & V4L2_IN_ST_NO_CARRIER)
        strcat(b, "NoCarrier ");

    if (s & V4L2_IN_ST_MACROVISION)
        strcat(b, "MacroVision ");
    if (s & V4L2_IN_ST_NO_ACCESS)
        strcat(b, "NoAccess ");
    if (s & V4L2_IN_ST_VTR)
        strcat(b, "VTR-Constant ");

    if (s == 0) {
        strcat(b, "\e[1mYes-OK-Signal\e[0m");
    }

    return b;
}

int do_the_work(void) {
    struct timeval now = {0};
    struct timeval future = {0};
    int rc = -1;
    int g_fd = -1;

    if ((g_fd = open_device(g_dev_name)) < 0)
        return -1; // exit on fail


    rc = gettimeofday(&now, NULL);
    if (rc == 0) {
        memcpy(&future, &now, sizeof (future));
        future.tv_sec += g_timeout;
    } else {
        fprintf(stderr, "gettimeofday() failed, errno = %d\n", errno);
        return -1;
    }

    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t m = 0;
    // find the number of inputs
    while (1) {
        memset(&g_vid_input, 0, sizeof (g_vid_input)); // clean
        g_vid_input.index = i; // this one

        j = ioctl(g_fd, VIDIOC_ENUMINPUT, &g_vid_input); // get info

        if (j == -1)
            break; // no more inputs

        m = i; // new maximum
        i++; // next
    }

    if (i == 0) {
        fprintf(stderr, "video device has no inputs\n");
        return -1;
    }
    while (1) {
        if (g_verbose) {
            printf("\e[1;1H== list of inputs ==\n");
        }

        i = 0;
        while (i <= m) {
            memset(&g_vid_input, 0, sizeof (g_vid_input)); // clean
            g_vid_input.index = i; // this one

            j = ioctl(g_fd, VIDIOC_ENUMINPUT, &g_vid_input); // get info

            if (j == -1)
                break; // invalid ioctl

            // The input exists, lets use it and query again
            uint32_t value;
            j = ioctl(g_fd, VIDIOC_G_INPUT, &value);
            if (j == -1) {
                fprintf(stderr, "query input (VIDIOC_G_INPUT) failed\n");
                break;
            }

            value = i; // set to other input
            j = ioctl(g_fd, VIDIOC_S_INPUT, &value);
            if (j == -1) {
                fprintf(stderr, "setting input (VIDIOC_S_INPUT) failed\n");
                break;
            }
            // give it time
            usleep(50000);
            // query again
            j = ioctl(g_fd, VIDIOC_ENUMINPUT, &g_vid_input); // get info
            if (j == -1) {
                fprintf(stderr, "enum input (VIDIOC_ENUMINPUT) failed\n");
                break;
            }
            if (g_verbose) {
                char buf[100]; // string buf
                printf("input     = %d     \n", g_vid_input.index);
                printf("name      = %s     \n", g_vid_input.name);
                printf("InputType = %s     \n", g_inputtype_lookup[g_vid_input.type]);
                printf("Status    = %d     \n", g_vid_input.status);
                printf("StatusText= %s     \n\n", status_to_text(g_vid_input.status,
                        buf, sizeof (buf)));
            }

            // force some flags to zero
            // they are not real problems
            g_vid_input.status &= ~(V4L2_IN_ST_MACROVISION | V4L2_IN_ST_HFLIP
                    | V4L2_IN_ST_VFLIP);

            if (g_vid_input.status == 0) {
                g_have_signal_flag = 1; // true
                g_active_input_index = i; // the good input
            }

            i++; // next please
        }

        // quick exit
        if (g_have_signal_flag && g_quick_exit) {
            uint32_t value = g_active_input_index; // set to other input
            fprintf(stderr, "set input to %d\n", value);
            j = ioctl(g_fd, VIDIOC_S_INPUT, &value);
            if (j == -1) {
                fprintf(stderr, "set input failed (VIDIOC_S_INPUT)\n");
            }

            return (g_active_input_index);
        }

        // wait a while before we try again
        usleep(100000);
        // did we timeout
        rc = gettimeofday(&now, NULL);
        if (rc == 0 && (future.tv_sec < now.tv_sec)) {
            // timeout
            break;
        }
    }

    if (g_have_signal_flag) {
        // select that input
        uint32_t value = g_active_input_index; // set to other input
        fprintf(stderr, "set input to %d\n", value);
        j = ioctl(g_fd, VIDIOC_S_INPUT, &value);
        if (j == -1) {
            fprintf(stderr, "set input failed (VIDIOC_S_INPUT)\n");
        }
        return (g_active_input_index);
    }
}

int main(int argc, char *argv[]) {
    int c;
    
    while (1) {
        static struct option long_options[] = {
            /* These options set a flag. */
            {"verbose", no_argument, &g_verbose, 1},
            {"brief", no_argument, &g_verbose, 0},
            {"keeprunning", no_argument, 0, 'k'},
            {"file", required_argument, 0, 'f'},
            {"timeout", required_argument, 0, 't'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long(argc, argv, "kt:f:",
                long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
            case 0:
                /* If this option set a flag, do nothing else now. */
                if (long_options[option_index].flag != 0)
                    break;
                printf("option %s", long_options[option_index].name);
                if (optarg)
                    printf(" with arg %s", optarg);
                printf("\n");
                break;

            case 'k':
                puts("option -k (--keeprunning)\n");
                g_quick_exit = 0;
                break;


            case 'f':
                printf("option -f (--file) with value `%s'\n", optarg);
                g_dev_name = strdup(optarg);
                break;
            case 't': // timeout
                g_timeout = atoi(optarg);
                break;
            case '?':
                /* getopt_long already printed an error message. */
                fprintf(stderr, "(C) GPL2, edwin@oetelaar.com\n"
                        "Test and set video4linux input based on probing sync\n"
                        "Valid options are: \n--timeout nSecs\n"
                        "--verbose\n--brief\n--keeprunning\n"
                        "--file videodevice\n\n"
                        "returns video-input-number or -1 on error\n");
                exit(1);
                break;

            default:
                abort();
        }
    }

    /* Print any remaining command line arguments (not options). */
    if (optind < argc) {
        printf("non-option ARGV-elements: ");
        while (optind < argc)
            printf("%s ", argv[optind++]);
        putchar('\n');
    }

    exit(do_the_work());
}