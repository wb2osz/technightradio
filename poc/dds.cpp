
// The WSJT modes use multi-FSK which means switching between several different frequencies.
// Since there are more than two tones, we can send more than one bit at a time.
// But we are not concerned about the encoding here.

// The normal mode of operation is to feed audio into an SSB transmitter.
// The result is FSK of the radio frequency.  
// Here, we want to skip the audio step and go directly to RF.

// We will use a specially modified version of WSJT-X which sends a JSON formatted
// string with the necessary information at the start of a transmission. 

// You can do a standalong test, without WSJT-X, like this:
// echo '{"period":15,"txdelay":103,"freq0":1500,"spacing":6.25,"baud":6.25,"symcount":79,"tones":[2,5,6,0,4,1,3,4,0,1,3,0,0,1,6,1,3,0,2,5,0,4,5,3,2,0,2,7,4,0,2,0,3,6,0,4,2,5,6,0,4,1,3,7,6,4,0,4,0,6,1,4,7,3,2,2,6,6,4,1,3,2,5,4,1,5,0,0,1,7,6,4,2,5,6,0,4,1,3]}' | socat - udp-sendto:localhost:8888


#include <cstdio>
#include <string>
#include <iostream>
#include <sstream>
#include <iostream>
#include <fstream>
#include <vector>
using namespace std;

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>		// for memset.
#include <time.h>
#include <sys/time.h>
#include <stdint.h>		// for uint64_t
#include <math.h>
//#include <ctype.h>
#include <unistd.h>

// Install required library with apt-get install libjsoncpp-dev
#include <jsoncpp/json/json.h>


static double rf_carrier = 0;	// Radio carrier frequency.  
				// In our initial tests, we will generate audio.
				// Later will we will need to set this somehow, probably
				// via hamlib so WSJT-X controls it rather than needing to
				// set it separately with a radio control panel.


#define PORT 8888			// Listen for messages from WSJT-X
					// on this UDP port.

#define DDS_MCLK 75.e6			// 75 MHz for breakout board.
					// 80 MHz for current radio design.

#define DDS_BITS 28			// AD9834 has 28 bit counter.
					// AD8832 has 32 bit counter.


static void xmit(int period, int txdelay, double freq0, double spacing, 
			double baud, int symcount, vector<int>& tones);
static uint64_t gettime_us (void);



// Main program.
// Read messsages from UDP socket and control AD9834 DDS chip.

#include "AD9834.h"

AD9834 dds;


int main () {

	struct sockaddr_in sa;
	int sock;

// Listen for UDP packets.

	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
	  cerr << "Could not create UDP socket to listen to port " << PORT << endl;
	  exit (1);
	}

	memset((char *) &sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;   
	sa.sin_port = htons(PORT);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sock, (const struct sockaddr *) &sa, sizeof(sa)) == -1) {
	  cerr << "Could not create UDP socket to listen to port " << PORT << endl;
	  exit (1);
	}


// Open the DDS chip.

        if ( ! dds.adopen ("/dev/spidev0.0")) {
          exit (1);
        }

	while (1) {

	  char wsjtx_msg[4000];

	  int len = recv (sock, wsjtx_msg, sizeof(wsjtx_msg)-1, 0);
	  if (len < -1) {
	    cerr << "UDP socket read error on port " << PORT << endl;
	    exit (1);
	  }

	  wsjtx_msg[len] = '\0';

// Extract values from JSON string.
// Anything missing is set to zero.

// API documentatin here:    jsoncpp.sourceforge.net/class_json_1_1_value.html


	  Json::Reader reader;
	  Json::Value obj;
	  if (reader.parse(wsjtx_msg, obj)) {

	    if ( ! obj["period"].empty()) {

// Regular transmission

	      int period = obj["period"].asInt();	// Time slot for transmission, 15, 30, or 60 seconds.
	      int txdelay = obj["txdelay"].asInt();	// milli seconds to wait before starting.
	      double freq0 = obj["freq0"].asDouble();	// Lowest audio tone, Hz.  Typical default 1500.
	      double spacing = obj["spacing"].asDouble(); // Spacing between tones.  e.g. 6.25 for FT8
	      double baud = obj["baud"].asDouble();	// Number of symbols sent per second.  e.g.  6.25 for FT8.
	      int symcount = obj["symcount"].asInt();	// Count of tones to be sent.  Redundant because
							// it is the same as length of 'tones' vector.
	      vector<int> tones;			// List of small integers for tones to be sent.
							// For value n, we send carrier + freq0 + n * spacing.	
    
	      Json::Value tobj = obj["tones"];
	      if ( ! tobj.isNull()) {
	        for (int i = 0; i < (int)(tobj.size()); i++) {
	          tones.push_back(tobj[i].asInt());
	        }
	      }
  
// Transmit it.
  
	      xmit (period, txdelay, freq0, spacing, baud, symcount, tones);
	    }
	    else if ( ! obj["tune"].empty()) {

// Turn steady tone on or off.  (Tune button)

	      int tune = obj["tune"].asInt();

	      if (tune) {
	        double freq0 = obj["freq0"].asDouble();
	        double f = rf_carrier + freq0;
                int step = (int) ((f * pow(2,DDS_BITS) / DDS_MCLK) + 0.5);
                dds.adsetfreq(step);
                double actual = step * DDS_MCLK / pow(2,DDS_BITS);
	        cout << "Begin steady tone   f= " << f << "  dds= " << step << "  actual= " << actual << endl;
	      }
	      else {
                dds.adsetfreq(0);
	        cout << "End steady tone" << endl;
	      }
	    }
	    else if ( ! obj["halttx"].empty()) {

// Early termination of transmission in progress.  (Halt Tx button)

	      // Would need to do some restructuring to implement here.
	      // Currently we don't read from socket while transmitting.

	      cout << "Halt Tx not implemented yet." << endl;
	    }
	    else {
	      cerr << "ERROR! Could not find expected values in JSON string." << endl;
	      cerr << wsjtx_msg << endl;
	    }
	  }
	  else {
	    cerr << "ERROR! Could not parse JSON string." << endl;
	    cerr << wsjtx_msg << endl;
	  }
	} // end while 1
	return 1;

} // end main


// Transmit tones as instructed by message from WSJT-X.
// You might be tempted to use some sort of sleep function but wouldn't
// be such a good idea.   There is no guarantee that the process will wake
// up again in precisely that time.  Instead we go into a busy loop watching
// the realtime clock.  The assumption here is that we are using a quad core
// CPU so we should be able to keep going instead of being held up by
// something else going on.


static void xmit(int period, int txdelay, double freq0, double spacing, 
				double baud, int symcount, vector<int>& tones) {

	int tone_len_us;	// How long to send each tone in microseconds.
	uint64_t now;		// Current time, microsecond resolution.
	uint64_t wait_until;
	uint64_t start;		// Start of transmit time.  Just for debug output.

// Quick sanity check for anything unexpected.
// Complain but charge ahead in most cases.

	if (period != 15 && period != 30 && period != 60) {
	  cerr << "ERROR!  Transmit period " << period << " is not one of 15, 30, 60." << endl;
	}
	if (txdelay < 0 || txdelay > 999) {
	  cerr << "ERROR!  Unexpected transmit delay time " << txdelay << " milliseconds." << endl;
	}
	if (freq0 < 50 || freq0 > 5000) {
	  cerr << "ERROR!  Unexpected audio frequency " << freq0 << " Hz." << endl;
	}
	if (spacing < 1 || spacing > 1001) {
	  cerr << "ERROR!  Unexpected tone spacing of " << spacing << " Hz." << endl;
	}
	if (baud < 1 || baud > 2001) {
	  cerr << "ERROR!  Unexpected data rate of " << baud << " baud." << endl;
	  if (baud == 0) return;	// avoid divide by zero.
	}
	if (spacing < 0.999 * baud  || spacing > 1.001 * baud) {
	  cerr << "ERROR!  Expected tone spacing " << spacing << " and Baud " << baud << " to be the same."  << endl;
	}
	if (symcount != (int)(tones.size())) {
	  cerr << "ERROR!  Expected list of " << symcount << " symbols but got " << tones.size() << "." << endl;
	}

	float duration = tones.size() / baud;
	// FT8: 12.64 / 15 = 84.2%
	// JT4: 47.1 / 60 = 78.5%
	// JT65: 46.81 / 60 = 78%
	if (duration <= 0.75 * period || duration  >= period - 1.) {
	  cerr << "ERROR!  Transmission of " << duration << " seconds doesn't seem right for " << period << " second period." << endl;
	}


// Compute how many microseconds for each symbol.

	tone_len_us = (int)((1.0e6 / baud) + 0.5);
	cout <<  "Debug: each symbol is " << tone_len_us << " microseconds." << endl;

	wait_until = start = now = gettime_us();

// Possible delay until ideal time after transmit period start.

	if (txdelay > 0) {
	  start += txdelay * 1000;
	  wait_until = start;
	  do {
	    now = gettime_us();
	  } while (now < wait_until);
	}

// Send the tones.

	for (int i = 0; i < (int)(tones.size()); i++) {

	  double f = rf_carrier + freq0 + tones[i] * spacing;;

#if 0
	  // Dither between two closest integer values.

	  double desired = f * pow(2,DDS_BITS) / DDS_MCLK;
	   // now = gettime_us();
	  cout << "tone start " << now - start << "  f= " << f << "  ideal dds= " << desired << endl;
	  int lower = (int)desired;
          dds.adsetfreq(lower);
	  double error = lower - desired;
	  wait_until += tone_len_us;
	  int hi = 0, lo = 0;
	  do {
	   if (now < wait_until - 20) {
	    if (error > 0) {
              dds.adsetfreq(lower);
	      error += lower - desired;
	      lo++;
	    }
	    else {
              dds.adsetfreq(lower+1);
	      error += (lower + 1) - desired;
	      hi++;
	    }
	   }
	    now = gettime_us();
	  } while (now < wait_until);
	  //cout << "fraction = " << (float)hi / (hi + lo) <<  "   total = " << lo + hi << endl;
#else
	  // Round to nearest.

          int step = (int) ((f * pow(2,DDS_BITS) / DDS_MCLK) + 0.5);
          dds.adsetfreq(step);
          double actual = step * DDS_MCLK / pow(2,DDS_BITS);

	  cout << "tone start " << now - start << "  f= " << f << "  dds= " << step << "  actual= " << actual << endl;

	  wait_until += tone_len_us;
	  do {
	    now = gettime_us();
	  } while (now < wait_until);
#endif
	}
	
// Turn off the transmitter.

        dds.adsetfreq(0);
	cout << "tone end " << now - start << endl;

}  // end xmit



// Get current time with microsecond resolution.
// We don't care about the time zone because we will only be looking
// at elapsed time between different values.

static uint64_t gettime_us (void) {
	struct timeval tv;

	gettimeofday (&tv, NULL);
	return ((uint64_t)tv.tv_sec * (uint64_t)1000000 + (uint64_t)tv.tv_usec); 
}


