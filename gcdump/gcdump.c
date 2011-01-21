#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <reprap/comms.h>
#include <reprap/util.h>

#define STR(x) #x

#define DEFAULT_SPEED 19200

#define HELP \
	"\t-?\n" \
	"\t-h\t\tDisplay this help message.\n" \
	"\t-q\t\tQuiet mode; no output unless an error occurs.\n" \
	"\t-v\t\tVerbose: Prints debug info and all serial I/O, instead of just received data.\n" \
	"\t-s speed\tSerial line speed.  Defaults to " STR(DEFAULT_SPEED) ".\n" \
  "\t-5\t\tUse 5D protocol (default is 3D)\n" \
	"\t-c\t\tFilter out non-meaningful chars. May stress noncompliant gcode interpreters.\n" \
	"\t-u number\tMaximum number of messages to send without receipt confirmation.  Unsafe, but necessary for certain broken firmware.\n" \
  "\t-f file\t\tFile to dump.  If no gcode file is specified, or the file specified is -, gcode is read from the standard input.\n"


void usage(char* name) {
	fprintf(stderr, "Usage: %s [-s <speed>] [-5] [-q] [-v] [-c] [-u <number>] [-f <gcode file>] [port]\n", name);
}

/* Allows atexit to be used for guaranteed cleanup */
rr_dev device = NULL;
int input = STDIN_FILENO;
void cleanup() 
{
	if(device) {
		rr_close(device);
	}
	if(input != STDIN_FILENO) {
		close(input);
	}
}

void onsend(rr_dev dev, void *data, void *blockdata, const char *line, size_t len) {
  write(STDOUT_FILENO, line, len);
}

void onrecv(rr_dev dev, void *data, const char *reply, size_t len) {
  write(STDOUT_FILENO, reply, len);
}

void onerr(rr_dev dev, void *data, rr_error err, const char *source, size_t len) {
  switch(err) {
  case RR_E_UNCACHED_RESEND:
    fprintf(stderr, "Device requested we resend a line older than we cache!\n"
            "Aborting.\n");
    exit(EXIT_FAILURE);
    break;

  case RR_E_HARDWARE_FAULT:
    fprintf(stderr, "HARDWARE FAULT!\n"
            "Aborting.\n");
    exit(EXIT_FAILURE);
    break;

  case RR_E_UNKNOWN_REPLY:
    fprintf(stderr, "Warning:\t Recieved an unknown reply from the device.\n"
            "\t Your firmware is not supported or there is an error in libreprap.\n"
            "\t Please report this!\n");
    break;

  default:
    fprintf(stderr, "libreprap and gcdump are out of sync.  Please report this!\n");
    break;
  }
}

void update_buffered(rr_dev device, void *state, char value) {
  *(int*)state = value;
}

int main(int argc, char** argv)
{
	atexit(cleanup);

	// Get arguments
	long speed = DEFAULT_SPEED;
	char *devpath = NULL;
	char *filepath = NULL;
  rr_proto protocol = RR_PROTO_SIMPLE;
	int quiet = 1;
	int verbose = 0;
	int strip = 0;
	int interactive = isatty(STDIN_FILENO);
  int buffered = 0;
	unsigned max_unconfirmed = 0;
	{
		int opt;
		while ((opt = getopt(argc, argv, "h?5qvcs:u:f:")) >= 0) {
			switch(opt) {
			case 's':			/* Speed */
				speed = strtol(optarg, NULL, 10);
				break;

			case 'f':
				filepath = optarg;
				break;

			case 'u':
				max_unconfirmed = strtol(optarg, NULL, 10);
				break;

			case 'q':			/* Quiet */
				quiet = 1;
				break;

			case 'v':			/* Verbose */
				verbose = 1;
				break;

			case 'c':
				strip = 1;
				break;

			case '?':			/* Help */
			case 'h':
				usage(argv[0]);
				fprintf(stderr, HELP);
				exit(EXIT_SUCCESS);
				break;

      case '5':
        protocol = RR_PROTO_FIVED;
        break;

			default:
				break;
			}
		}
		switch(argc - optind) {
		case 1:
			devpath = argv[optind];
			break;

		case 0:
			devpath = rr_guess_port();
			if(devpath == NULL) {
				fprintf(stderr, "Unable to autodetect a serial port.  Please specify one explicitly.\n");
				usage(argv[0]);
				exit(EXIT_FAILURE);
			}
			break;

		default:
			fprintf(stderr, "Too many arguments!\n");
			usage(argv[0]);
			exit(EXIT_FAILURE);
		}
		
		if(filepath == NULL) {
			filepath = "-";
		}
	}
	if(verbose) {
		printf("Serial port:\t%s\n", devpath);
		printf("Line speed:\t%ld\n", speed);
		printf("Gcode file:\t%s\n", filepath);
	}

  device = rr_create(protocol,
                     (verbose ? &onsend : NULL), NULL,
                     (quiet ? NULL : &onrecv), NULL,
                     NULL, NULL,
                     &onerr, NULL,
                     &update_buffered, &buffered,
                     32);

	/* Connect to machine */
	if(rr_open(device, devpath, speed) < 0) {
		fprintf(stderr, "Error opening connection to machine on port %s: %s\n",
            devpath, strerror(errno));
		exit(EXIT_FAILURE);
	}

  /* Open input */
	if(strncmp("-", filepath, 1) == 0) {
		if(verbose) {
			printf("Will read gcode from standard input");
			if(interactive) {
				printf("; enter Ctrl-D (EOF) to finish.");
			}
			printf("\n");
		}
		/* input defaults to stdin */
	} else {
		input = open(filepath, O_RDONLY);
		if(input < 0) {
			fprintf(stderr, "Unable to open gcode file \"%s\": %s\n",
              filepath, strerror(errno));
			exit(EXIT_FAILURE);
		}
		interactive = 0;
	}

  /* Mainloop */

	if(verbose) {
		printf("Successfully completed!\n");
	}

	exit(EXIT_SUCCESS);
}
