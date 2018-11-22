#include <cstdio>
#include <string>
#include <iostream>
//#include <sstream>
//#include <fstream>
//#include <vector>
using namespace std;


#include <errno.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>		// for memset
#include <unistd.h>		// for open, close
#include <math.h>

#include "AD9834.h"


// On the Raspberry Pi, we have a single SPI controller
// available with two chip enable lines.
// The devices appear as  /dev/spidev0.0  and  /dev/spidev0.1

// Which SPI "mode" do we need?  Looks like mode 2.
// The SCLK must be high when FSYNC goes low.  (CPOL=1)
// Data is sampled on falling (leading) edge.  (CPHA=0)
// Mode 3 also works but I don't think it is entirely correct
// because there is not enough data hold time after the clock.

#define SCLK_SPEED 40000000			// Rated at 40 MHz but we might
						// use half that to be safe.

#define DDS_BITS 28				// Number of bits in counter.


bool AD9834::adopen(string dev) {

	unsigned char mode = SPI_MODE_2;
	unsigned char bits = 8;
	int speed = SCLK_SPEED;

	fd = open (dev.c_str(), O_RDWR);

	if (fd < 0){
	  cerr << "Could not open " << dev << "  errno = " << errno << endl;
	  if (errno == ENOENT) {
	    cerr << "You need to use the device name /dev/spidev0.0 or /dev/spidev0.1." << endl;
	    cerr << "If they do not exist, run raspi-config and enable SPI." << endl;
	  }
	  else if (errno == EACCES) {
	    cerr << "Permission denied.  Verify that /dev/spidev0.* have group spi." << endl;
	    cerr << "Make sure that your account is in the spi group." << endl;
	    cerr << "You can check with with the \"id\" command." << endl;
	  }
	  return false;
	}

	if (ioctl (fd, SPI_IOC_WR_MODE, &mode) < 0 ||
	    ioctl (fd, SPI_IOC_RD_MODE, &mode) < 0 ||
	    ioctl (fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
	    ioctl (fd, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0 ||
	    ioctl (fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0 ||
	    ioctl (fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed) < 0) {
	  cerr << "Could not set properties for " << dev << "  errno = " << errno << endl;
	  return false;
	}

    return adreset();
}

bool AD9834::adreset(void) {

	unsigned char data[] = {
		0b00100001, 0,		// reset on
		0b01000000, 0,		// zero freq0
		0b01000000, 0,
		0b10000000, 0,		// zero freq1
		0b10000000, 0,
		0b11000000, 0,		// zero phase0
		0b11100000, 0,		// zero phase1
		0b00100000, 0 };	// reset off

	return adwrite (data, sizeof(data));
}

bool AD9834::adwrite(unsigned char *data, int len) {


	//printf ("adwrite: ");
	//for (int k=0; k<len; k++) printf (" %02x", data[k]); 
	//printf ("\n");
	
	struct spi_ioc_transfer sit;
	memset (&sit, 0, sizeof(sit));

	sit.tx_buf = (__u64)data;
	sit.rx_buf = (__u64)NULL;
	sit.len = len;
	sit.speed_hz = SCLK_SPEED;
	sit.bits_per_word = 8;

	if (ioctl (fd, SPI_IOC_MESSAGE(1), &sit) < 0) {
	  cerr << "Write error for DDS chip.  errno = " << errno << endl;
	  return false;
	}
	return true;
}


// Idea:  supply double precision desired freq. Return actual.
// Where do we set MCLK?  At open or here?

bool AD9834::adsetfreq(int f) {

// The datasheet warns, "Note however that continuous writes to the same
// frequency register are not recommended.  This results in intermediate
// updates during the writes.  If a frequency sweep, or something similar,
// is required, it is recommended that users alternate between two
// frequency registers."

// So, we will write to the inactive frequency register and then select it.

	unsigned char data[6];
	data[0] = (f & 0b0000000000000011111100000000) >> 8;
	data[1] = (f & 0b0000000000000000000011111111);
	data[2] = (f & 0b1111110000000000000000000000) >> 22;
	data[3] = (f & 0b0000001111111100000000000000) >> 14;

	if (fsel) {
	  data[0] |= 0b01000000;			// Set freq0
	  data[2] |= 0b01000000;
	  data[4] =  0b00100000;			// Select freq0
	  data[5] =  0;
	  fsel = 0;
	}
	else {
	  data[0] |= 0b10000000;			// Set freq1
	  data[2] |= 0b10000000;
	  data[4] =  0b00101000;			// Select freq1
	  data[5] =  0;
	  fsel = 1;
	}

	return (adwrite (data, (int)sizeof(data)));
}


void AD9834::adclose() {
	
	// Disable MCLK and power down DAC on our way out.

	unsigned char data[] = {
		0b00100000, 0b11000000 };

	(void)adwrite (data, sizeof(data));

	close (fd);
	fd = -1;
}



#if TEST

// Hook up a frequency counter to see if results are as expected.

#define EVAL_MCLK 75.0e6


int main () {

	int hz[] = {1000, 2000, 3000, 1000000, 2000000, 3000000};

	AD9834 dds;
	if ( ! dds.adopen ("/dev/spidev0.0")) {
	  exit (1);
	}
	
	for (int n = 0; n < (int)sizeof(hz)/(int)sizeof(int); n++) {
	  int step = (int) ((hz[n] * pow(2,DDS_BITS) / EVAL_MCLK) + 0.5);
	  dds.adsetfreq(step);
	  double actual = step * EVAL_MCLK / pow(2,DDS_BITS);
	  cout << hz[n] << "  actual " << actual << endl;
	  sleep (10);
	}
	dds.adsetfreq(0);
	dds.adclose ();
	cout << "Done" << endl;
}

#endif
