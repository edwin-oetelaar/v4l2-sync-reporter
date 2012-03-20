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
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev.h>
#include <stdint.h>

struct v4l2_queryctrl queryctrl;
struct v4l2_querymenu querymenu;
struct v4l2_input input;
struct v4l2_output output;

int fd;

const char dev_name[] = "/dev/video0";

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
#if 0
static void enumerate_menu(void) {
	printf("  Menu items:\n");

	memset(&querymenu, 0, sizeof(querymenu));
	querymenu.id = queryctrl.id;

	for (querymenu.index = queryctrl.minimum; querymenu.index
			<= queryctrl.maximum; querymenu.index++) {
		if (0 == ioctl(fd, VIDIOC_QUERYMENU, &querymenu)) {
			printf("  %s\n", querymenu.name);
		} else {
			perror("VIDIOC_QUERYMENU");
			exit(EXIT_FAILURE);
		}
	}
}
#endif

char *inputtype[] = { "unknown", "tuner", "camera", 0, 0, 0, 0 };
char buf[100];

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
		strcat(b, "Yes-OK-Signal");
	}

	return b;
}

int main(int argc, char *argv[]) {
	open_device();

	memset(&input, 0, sizeof(input));

	uint32_t i = 0;
	printf("== list of inputs ==\n");
	while (1) {
		memset(&input, 0, sizeof(input)); // clean
		input.index = i; // this one

		int j = ioctl(fd, VIDIOC_ENUMINPUT, &input); // get info

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
			printf("can not happen, query input (VIDIOC_ENUMINPUT) failed\n");
			break;
		}

		printf("The selected input is %d\n", value);

		printf("number    = %d\n", input.index);
		printf("name      = %s\n", input.name);
		printf("InputType = %s\n", inputtype[input.type]);
		printf("Status    = %d\n", input.status);
		printf("StatusText= %s\n\n", status_to_text(input.status, buf,
				sizeof(buf)));

		i++; // next please
	}
	if (i == 0)
		printf("no inputs found\n");

	// enumerate outputs
	printf("== list of outputs ==\n");
	i = 0;
	while (1) {
		memset(&output, 0, sizeof(output)); // clean
		input.index = i; // this one

		int j = ioctl(fd, VIDIOC_ENUMOUTPUT, &output); // get info

		if (j == -1)
			break; // no more

		printf("number    = %d\n", output.index);
		printf("name      = %s\n", output.name);
		i++;
	}
	if (i == 0)
		printf("no outputs found\n");

	memset(&queryctrl, 0, sizeof(queryctrl));

	for (queryctrl.id = V4L2_CID_BASE; queryctrl.id < V4L2_CID_LASTP1; queryctrl.id++) {
		if (0 == ioctl(fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)
				continue;

			printf("Control   = %s\n", queryctrl.name);

			if (queryctrl.type == V4L2_CTRL_TYPE_MENU)
				enumerate_menu();
		} else {
			if (errno == -1)
				continue;

			perror("VIDIOC_QUERYCTRL");
			exit(EXIT_FAILURE);
		}
	}

	for (queryctrl.id = V4L2_CID_PRIVATE_BASE;; queryctrl.id++) {
		if (0 == ioctl(fd, VIDIOC_QUERYCTRL, &queryctrl)) {
			if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)
				continue;

			printf("Control %s\n", queryctrl.name);

			if (queryctrl.type == V4L2_CTRL_TYPE_MENU)
				enumerate_menu();
		} else {
			if (errno == -1)
				break;

			perror("VIDIOC_QUERYCTRL");
			exit(EXIT_FAILURE);
		}
	}

	return (0);
}
