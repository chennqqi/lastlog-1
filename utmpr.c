#include <stdio.h>
#include <inttypes.h>
#include <utmp.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void do_file(char *filename);
void version(void);
void usage(void);
void detect_filetype(FILE *fp);


#define Q_(x) #x
#define Q(x) Q_(x)
//TODO: don't we need to do UT_LINESIZE - 1, and likewise for the other specifiers?
#if __WORDSIZE == 64 && defined __WORDSIZE_COMPAT32
#define UTMP_WRITE_FORMAT "%hd\t%d\t%s\t%s\t%s\t%s\t%hd\t%hd\t%"PRId32"\t%"PRId32"\t%"PRId32"\t%s\n"
#define UTMP_READ_FORMAT "%hd\t%d\t%"Q(UT_LINESIZE)"[^\t]\t%4[^\t]\t%"Q(UT_NAMESIZE)"[^\t]\t%"Q(UT_HOSTSIZE)"[^\t]\t%hd\t%hd\t%"PRId32"\t%"PRId32"\t%"PRId32"\t%16[^\n]\n"
#else
#define UTMP_WRITE_FORMAT "%hd\t%d\t%s\t%s\t%s\t%s\t%hd\t%hd\t%ld\t%ld\t%ld\t%s\n";
#define UTMP_READ_FORMAT "%hd\t%d\t%"Q(UT_LINESIZE)"[^\t]\t%4[^\t]\t%"Q(UT_NAMESIZE)"[^\t]\t%"Q(UT_HOSTSIZE)"[^\t]\t%hd\t%hd\t%ld\t%ld\t%ld\t%16[^\n]\n";
#endif
char *PROGNAME;
char VERSION_STRING[] = "0.01";
static int read_binary_mode = -1;

int main(int argc, char **argv)
{
	PROGNAME = argv[0];

	static struct option long_options[] = {
		{"version",	no_argument,	0,	'v'},
		{"help",	no_argument,	0,	'h'},
		{"binary",	no_argument,	0,	'b'},
		{"text",	no_argument,	0,	't'}
	};
	int c;
	int option_index;
	while(1) {
		c  = getopt_long(argc, argv, "tbhv", long_options, &option_index);
		if (c == -1)
			break;

		switch(c) {
			case 'h':
				usage();
				break;
			case 'v':
				version();
				break;
			case 't':
			case 'b':
				if (read_binary_mode == -1)
					read_binary_mode = (c == 'b') ? 1 : 0;
				else {
					printf("%s: You may not specify both binary and text mode\n", PROGNAME);
					exit(EXIT_FAILURE);
				}
				break;
		}
	}

	if (optind < argc) {
		while (optind < argc)
			do_file(argv[optind++]);
	}
	else
		do_file("-");
}

void do_file(char *filename)
{
	FILE *fp;
	if (filename[0] == '-' && filename[1] == '\0')
		fp = stdin;
	else {
		fp = fopen(filename, "r");
		if (fp == NULL) {
			printf("%s: %s: %s\n", PROGNAME, filename, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	if (read_binary_mode == -1)
		detect_filetype(fp);

	if (read_binary_mode == 1)
		binary_to_text(fp);
	else
		text_to_binary(fp);

	if (fp != stdin)
		fclose(fp);
}

void detect_filetype(FILE *fp)
{
	// we only read 1 character and then ungetc it because you can't rewind stdin past 1 character
	int character = fgetc(fp);
	// every record should start with something less than ascii zero
	read_binary_mode = character < '0';
	ungetc(character, fp);
}

void version()
{
	printf("utmpr %s\n", VERSION_STRING);
	exit(EXIT_SUCCESS);
}

void usage()
{
	printf("Usage %s: [OPTION]... [FILE]...\n", PROGNAME);
	printf("Convert FILE(s), or standard input from/to utmp binary format/text\n");
	printf("and print to standard output\n\n");

	printf("  -t, --text       read text format and write utmp binary\n");
	printf("  -b, --binary     read utmp binary and write text\n");
	printf("      --help       display this help and exit\n");
	printf("      --version    output version and exit\n");

	printf("\nWith no FILE, or when FILE is -, read standard input.\n");
	printf("If neither --text nor --binary specified, auto-detect.\n");
	exit(EXIT_SUCCESS);
}

int binary_to_text(FILE *fp)
{
	struct utmp entry;
	char ip[16];
	while (fread(&entry, sizeof(struct utmp), 1, fp)) {
		if (0) //TODO: determine how to figure out of it's an ipv6. entries 1 2 and 3 are zero perhaps?
			ip[0] = 'a'; //TODO: format ipv6 address
		else
			sprintf(ip, "%d.%d.%d.%d",
				(entry.ut_addr_v6[0] >> 0x0) & 0xFF,
				(entry.ut_addr_v6[0] >> 0x8) & 0xFF,
				(entry.ut_addr_v6[0] >> 0x10) & 0xFF,
				(entry.ut_addr_v6[0] >> 0x18) & 0xFF);

		printf(UTMP_WRITE_FORMAT, entry.ut_type, entry.ut_pid, entry.ut_line,
					 entry.ut_id, entry.ut_user, entry.ut_host,
					 entry.ut_exit.e_termination, entry.ut_exit.e_exit,
					 entry.ut_session, entry.ut_tv.tv_sec,
					 entry.ut_tv.tv_usec, ip);
	}
	return 0;
}

int text_to_binary(FILE *fp)
{
	struct utmp entry;
	char ip[16];
	unsigned char ip_n[4];

	memset(&entry, 0, sizeof(struct utmp));
	//TODO: reading %s does not work for empty strings, so this has an awful time
	while (fscanf(fp, UTMP_READ_FORMAT, &entry.ut_type, &entry.ut_pid,
		       entry.ut_line, entry.ut_id, entry.ut_user, entry.ut_host,
	 	       &entry.ut_exit.e_termination, &entry.ut_exit.e_exit,
	 	       &entry.ut_session, &entry.ut_tv.tv_sec,
	 	       &entry.ut_tv.tv_usec, ip) == 12) {
		sscanf(ip, "%hhu.%hhu.%hhu.%hhu", &ip_n[0], &ip_n[1], &ip_n[2], &ip_n[3]);
		entry.ut_addr_v6[0] = ((ip_n[3] << 24) |
				       (ip_n[2] << 16) |
				       (ip_n[1] << 8)  |
				       (ip_n[0]));

		fwrite(&entry, sizeof(struct utmp), 1, stdout);
		memset(&entry, 0, sizeof(struct utmp));
	}
	return 0;
}
