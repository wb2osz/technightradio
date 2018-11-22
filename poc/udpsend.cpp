
// This is roughly what we need to integrate into WSJT-X.

#include <iostream>
#include <sstream>
#include <string>
using namespace std;

//#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h>	// for htons
#include <netdb.h>	// for gethostbyname
#include <string.h> 	// for memcpy


int main ()
{

// These two must be members of the class.

	int m_tnrFd = -1; 
	struct sockaddr_in m_tnrDest; 

	char *port = getenv("TNR_PORT");	

	if (port != NULL) {
	  char *host = getenv("TNR_HOST");
	  if (host == NULL) {
	    host = (char*)"localhost";
	  }

// This should be done once at application start up time.
 
	  struct hostent *hp = gethostbyname(host); 
	  if (hp != NULL) 
 {
  
	    memset ((char*)(&m_tnrDest), 0, sizeof(m_tnrDest)); 
	    m_tnrDest.sin_family = hp->h_addrtype; 
	    memcpy((void *)(&m_tnrDest.sin_addr), hp->h_addr_list[0], hp->h_length); 
	    m_tnrDest.sin_port = htons(atoi(port)); 

	    m_tnrFd = socket(PF_INET, SOCK_DGRAM, 0);
	    if (m_tnrFd == -1) {
	      cerr << "Tech Night Radio ERROR: could not create dgram socket." << endl;
	    }  
	  }
	  else { 
	    cerr << "Tech Night Radio ERROR: Could not obtain address of " << host << endl;
	  } 
	}
	else {
	  // This test app only, not in WSJT-X.
	  cerr << "You must define environment varialble TNR_PORT." << endl;
	  exit(1);
	}
  
  // This is what we need to do for each transmission.

	if (m_tnrFd != -1) {
 
	  int m_frameRate = 48000; 
	  double m_nsps = 1920;
	  int m_period = 15;
	  int m_symbolsLength = 79;
	  double m_frequency = 1500;
	  double m_toneSpacing = 6.25;
	  int m_silentFrames = 4944;
	  int txdelay = (m_silentFrames * 1000) / m_frameRate;
	  if (txdelay < 0) txdelay = 0;
	  double baud = 0.25 * m_frameRate / m_nsps;
	  int tones[] = {2,5,6,0,4,1,3,4,0,1,3,0,0,1,6,1,3,0,2,5,0,4,5,3,2,0,2,7,4,0,2,0,3,6,0,4,2,5,6,0,4,1,3,7,6,4,0,4,0,6,1,4,7,3,2,2,6,6,4,1,3,2,5,4,1,5,0,0,1,7,6,4,2,5,6,0,4,1,3};


	  // There was some concern about the overhead added by this format.
	  // My initial test revealed that building the string and sending
	  // the packet takes less than 0.1 millisecond.
	  // Need to repeat the test more scientifically.
  
          ostringstream o;
	  //o.clear();
  
          o << "{";
          o << "\"period\":" << m_period << ",";
          o << "\"txdelay\":" << txdelay << ",";
          o << "\"freq0\":" << m_frequency << ",";
          o << "\"spacing\":" << m_toneSpacing << ",";
          o << "\"baud\":" << baud << ",";
          o << "\"symcount\":" << m_symbolsLength << ",";
          o << "\"tones\":[";
          for (int i = 0; i < (int)(sizeof(tones)/4); i++) {
            o << tones[i];
            if (i < (int)((sizeof(tones)/sizeof(int))-1)) o << ",";
          }
          o << "]}";

	  string s = o.str();
	  if (sendto(m_tnrFd, s.c_str(), s.length(), 0, (struct sockaddr *)(&m_tnrDest), sizeof(m_tnrDest)) < 0) 
	  { 
	    int e = errno;
	    cerr << "Tech Night Radio ERROR:  Failed to send UDP packet, errno " << e << endl;
	  }
	  cout << s << endl;
	}
}
