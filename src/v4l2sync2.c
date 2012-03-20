/*
 * v4l2sync.c
 * $Id:$
 * doel, zorgen dat we geen video streamen die er niet is.
 * dus, streamen als er goede video is...
 * die kan komen van input 0,1,2,3 wie weet welke
 * we proberen alle inputs op sync/signal detect
 * daarna kiezen we er 1 die het beste is
 * als er geen goed is proberen we opnieuw gedurende timeout-periode
 * als er dan geen gevonden is geven we -1 terug
 * anders de ingang die wel goed is, zodat ffmpeg of vlc die kan gebruiken
 * we activeren zowieso die ingang
 *
 * pseudeo code:
 - timeout=param
 - stoptime = timeout+now()
 - goodsignal=false
 - goodinput=-1

 while (1) {
 loop over all inputs as i
 activate_input i
 wait_a_while_to_settle

 if signal==good
 goodsignal=true
 goodinput=i
 break

 if time > stoptime
 break
 }

 if goodsignal
 return goodinput
 else
 beep
 return error

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

// struct v4l2_queryctrl queryctrl;
// struct v4l2_querymenu querymenu;
struct v4l2_input input;
struct v4l2_output output;

int fd; // handle to device

const char dev_name[] = "/dev/video0"; // default name
char *inputtype[] = { "unknown", "tuner", "camera", 0, 0, 0, 0 };
char buf[100]; // string buf
int timeout = 300; // default timeout in seconds
int shorttime = 50000; // microseconds to wait for input get ready
struct timespec ts;
int goodsignal = 0;
int goodinput = -1;

static void open_device(void) {
	struct stat st;

	if (-1 == stat(dev_name, &st)) {
		fprintf(stderr, "Cannot identify '%s': %d, %s\n", dev_name, errno,
				strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is no device\n", dev_name);
		exit(EXIT_FAILURE);
	}

	fd = open(dev_name, O_RDWR /* required */| O_NONBLOCK, 0);

	if (-1 == fd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno,
				strerror(errno));
		exit(EXIT_FAILURE);
	}
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
		strcat(b, "H-Flip ");
	if (s & V4L2_IN_ST_VFLIP)
		strcat(b, "V-Flip ");

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

int main(int argc, char *argv[]) {

	open_device();
	struct timeval now;
	struct timeval future;
	int rc;

	rc = gettimeofday(&now, NULL);
	if (rc == 0) {
		//	printf("gettimeofday() successful.\n");
		//	printf("time = %lu.%06lu\n", now.tv_sec, now.tv_usec);
		memcpy(&future, &now, sizeof(future));
		future.tv_sec += timeout;
	} else {
		printf("gettimeofday() failed, errno = %d\n", errno);
		return -1;
	}

	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t m = 0;
	// find the number of inputs
	while (1) {
		memset(&input, 0, sizeof(input)); // clean
		input.index = i; // this one

		j = ioctl(fd, VIDIOC_ENUMINPUT, &input); // get info

		if (j == -1)
			break; // no more inputs

		m = i; // new maximum
		i++;
	}

	if (i == 0) {
		printf("video device has no inputs\n");
		return -1;
	}

	while (1) {

		printf("\e[1;1H== list of inputs ==\n");
		i = 0;
		while (i <= m) {
			memset(&input, 0, sizeof(input)); // clean
			input.index = i; // this one

			j = ioctl(fd, VIDIOC_ENUMINPUT, &input); // get info

			if (j == -1)
				break; // no more inputs

			// The input exists, lets use it and query again
			uint32_t value;
			j = ioctl(fd, VIDIOC_G_INPUT, &value);
			if (j == -1) {
				printf("can not happen, query input (VIDIOC_G_INPUT) failed\n");
				break;
			}

			value = i; // set to other input
			j = ioctl(fd, VIDIOC_S_INPUT, &value);
			if (j == -1) {
				printf("can not happen, query input (VIDIOC_G_INPUT) failed\n");
				break;
			}
			usleep(50000);
			// query again
			j = ioctl(fd, VIDIOC_ENUMINPUT, &input); // get info
			if (j == -1) {
				printf(
						"can not happen, query input (VIDIOC_ENUMINPUT) failed\n");
				break;
			}

			// printf("The selected input is %d\n", value);

			printf("input     = %d     \n", input.index);
			printf("name      = %s     \n", input.name);
			printf("InputType = %s     \n", inputtype[input.type]);
			printf("Status    = %d     \n", input.status);
			printf("StatusText= %s     \n\n", status_to_text(input.status, buf,
					sizeof(buf)));

			if (input.status == 0) {
				goodsignal = 1; // true
				// goodsignal found
				goodinput = i;
			}

			i++; // next please
		}
		usleep(100000);
		// did we timeout
		rc = gettimeofday(&now, NULL);
		if (rc == 0 && (future.tv_sec < now.tv_sec)) {
			// timeout
			break;
		}

		if (goodsignal) {
			// select that input
			uint32_t value = goodinput; // set to other input
			printf("set input to %d\n",value);
			j = ioctl(fd, VIDIOC_S_INPUT, &value);
			if (j == -1) {
				printf("can not happen, query input (VIDIOC_G_INPUT) failed\n");
			}
			// return value to ffmpeg
			return goodinput;
		}

	}

	return (-1);
}
