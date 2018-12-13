#include "Modulator.hpp"
#include <limits>
#include <qmath.h>
#include <QDateTime>
#include <QDebug>
#include "widgets/mainwindow.h" // TODO: G4WJS - break this dependency
#include "soundout.h"
#include "commons.h"
#include <stdio.h>
#include <sstream>
#include <iostream>
#include <string>

#include <sys/socket.h> 
#include <arpa/inet.h>  // for htons
#include <netdb.h>      // for gethostbyname
#include <string.h>     // for memcpy


#include "moc_Modulator.cpp"

extern float gran();		// Noise generator (for tests only)

#define RAMP_INCREMENT 64  // MUST be an integral factor of 2^16

#if defined (WSJT_SOFT_KEYING)
# define SOFT_KEYING WSJT_SOFT_KEYING
#else
# define SOFT_KEYING 1
#endif

double constexpr Modulator::m_twoPi;

//    float wpm=20.0;
//    unsigned m_nspd=1.2*48000.0/wpm;
//    m_nspd=3072;                           //18.75 WPM

Modulator::Modulator (unsigned frameRate, unsigned periodLengthInSeconds,
                      QObject * parent)
  : AudioDevice {parent}
  , m_quickClose {false}
  , m_phi {0.0}
  , m_toneSpacing {0.0}
  , m_fSpread {0.0}
  , m_frameRate {frameRate}
  , m_period {periodLengthInSeconds}
  , m_state {Idle}
  , m_tuning {false}
  , m_cwLevel {false}
  , m_j0 {-1}
  , m_toneFrequency0 {1500.0}
{
  std::cout << "JWL DEBUG 12-12-2018  Modulator::Modulator" << std::endl;
  m_tnrFd = -1;
  char *port = getenv("TNR_PORT");

  if (port != NULL) {
      char *host = getenv("TNR_HOST");
      if (host == NULL) {
          host = (char*)"localhost";
      }

// This should be done once at application start up time.
 
      struct hostent *hp = gethostbyname(host); 
      if (hp != NULL)  {

          memset ((char*)(&m_tnrDest), 0, sizeof(m_tnrDest)); 
          m_tnrDest.sin_family = hp->h_addrtype; 
          memcpy((void *)(&m_tnrDest.sin_addr), hp->h_addr_list[0], hp->h_length); 
          m_tnrDest.sin_port = htons(atoi(port)); 

          m_tnrFd = socket(PF_INET, SOCK_DGRAM, 0);
          if (m_tnrFd != -1) {
              std::cout << "Tech Night Radio: Sending to " << host << ":" << port << std::endl;
          } else { 
              std::cerr << "Tech Night Radio ERROR: could not create dgram socket." << std::endl;
          }  
      }
      else { 
          std::cerr << "Tech Night Radio ERROR: Could not obtain address of " << host << std::endl;
      } 
  }

}


// This is called at the start of a transmission.


void Modulator::start (unsigned symbolsLength, double framesPerSymbol,
                       double frequency, double toneSpacing,
                       SoundOutput * stream, Channel channel,
                       bool synchronize, bool fastMode, double dBSNR, int TRperiod)
{
  std::cout << "JWL DEBUG  Modulator::start" << std::endl;
  Q_ASSERT (stream);
// Time according to this computer which becomes our base time.
// Number of milliseconds since the beginning of the day.  (84600 seconds in a day)
  qint64 ms0 = QDateTime::currentMSecsSinceEpoch() % 86400000;

  if (m_state != Idle)
    {
      stop ();
    }

  m_quickClose = false;

  m_symbolsLength = symbolsLength;
  m_isym0 = std::numeric_limits<unsigned>::max (); // big number
  m_frequency0 = 0.;
  m_phi = 0.;
  m_addNoise = dBSNR < 0.;
  m_nsps = framesPerSymbol;
  m_frequency = frequency;
  m_amp = std::numeric_limits<qint16>::max ();
  m_toneSpacing = toneSpacing;
  m_bFastMode=fastMode;
  m_TRperiod=TRperiod;

  // Nominal time into the Tx sequence to start sending.
  // Looks like a special case for FT8 where m_nsps is 1920 and m_period is 15.
  // So this would be 500 ms for FT8 and 1000 for other modes.

  unsigned delay_ms = 1920 == m_nsps && 15 == m_period ? 500 : 1000;


  // noise generator parameters
  // I hope this is for generating noisy test signals and not normal operation.

  if (m_addNoise) {
    m_snr = qPow (10.0, 0.05 * (dBSNR - 6.0));
    m_fac = 3000.0;
    if (m_snr > 1.0) m_fac = 3000.0 / m_snr;
  }

  // Current number of mSec into the transmission time slot.

  unsigned mstr = ms0 % (1000 * m_period); // ms in period

  // Convert to number of audio samples (called frames here).
  //
  // Example:  Suppose we are using FT8 and current time is 200 mSec into the 15 sec time slot.
  //     (200  / 500)      * 48000       * 500      / 1000    = 9600 
  //
  // This means we are already 9600 audio sample times into 
  // the transmission time slot.  Seeing delay_ms in there twice is confusing.
  // Using a little algebra, both delay_ms could be removed and we would have the
  // same thing other than slight differences due to integer divide truncation.
  // mstr / 1000  is the current time fraction of a second then
  // multiply by the audio sample rate to get number of audio samples.
  // This could be simplified to:  
  // m_ic = mstr * m_frameRate / 1000;

  m_ic = (mstr / delay_ms) * m_frameRate * delay_ms / 1000;

  if(m_bFastMode) m_ic=0;

  m_silentFrames = 0;
  // calculate number of silent frames to send, so that audio will start at
  // the nominal time "delay_ms" into the Tx sequence.
  // Should check for negative results and set to 0 in case we are lagging
  // a little behind.

  if (synchronize && !m_tuning && !m_bFastMode)	{
    // Continuing our example for the normal case,
    //               9600 + 48000       / (1000 / 500)      - (200  * (48000 / 1000))
    //               9600 +       48000 / 2                 - ( 0.2 * 48000 ) 
    //		     9600 +           24000                  - 9600    [ same calculation as m_ic ]
    //	                              24000
    m_silentFrames = m_ic + m_frameRate / (1000 / delay_ms) - (mstr * (m_frameRate / 1000));

    // This looks wrong to me.  We are adding and subtracting the same amount and
    // always end up with number of audio samples for a half second regardless of
    // the current time from the clock.
    // I think it should be:
    //      delay_ms * m_frameRate / 1000 - m_ic                        = 500*48000/1000 - 9600 = 14400
    // or alternatively,
    //      (delay_ms - mstr) * m_frameRate / 1000                      = (500-200)*48000/1000 = 14400 
  }

#if 1

  int t = ms0;
  int h = t / 3600000;  t -= h * 3600000;
  int m = t / 60000; t -= m * 60000;
  int s = t / 1000; t -= s * 1000;
  int my_txdelay = delay_ms - mstr;   // Delay mSec before first tone.

  printf ("[%02d:%02d:%02d.%03d] Modulator::start\n", h, m, s, t);
  printf ("\tm_frameRate = %d\n", m_frameRate);
  printf ("\tm_period = %d\n", m_period);
  printf ("\tm_tuning = %d\n", m_tuning);
  printf ("\tm_toneFrequency0 = %.3f\n", m_toneFrequency0);
  
  printf ("\tm_symbolsLength = %d\n", m_symbolsLength);
  printf ("\tm_nsps = %.5f\n", m_nsps);
  printf ("\tm_frequency = %.3f\n", m_frequency);
  printf ("\tm_toneSpacing = %.5f\n", m_toneSpacing);
  printf ("\tm_bFastMode = %d\n", m_bFastMode);
  printf ("\tm_TRperiod = %d\n", m_TRperiod);
  
  printf ("\tmstr = %d\n", mstr);
  printf ("\tsynchronize = %d\n", synchronize);
  printf ("\tm_ic = %d\n", m_ic); 
  printf ("\tm_silentFrames = %d\n", (int)m_silentFrames);    //  I think this is wrong.
  printf ("\tmy_txdelay = %d\n", my_txdelay);   // Delay mSec before first tone.
  printf ("\titone[]=");
  int n;
  for (n = 0; n < (int)m_symbolsLength; n++) {
    printf (" %d", itone[n]);
  }
  printf ("\n\n");
#endif

  // m_tuning of 0 means send a normal transmission.

  if (m_tnrFd != -1) {

    if (! m_tuning) {
      int txdelay = (m_silentFrames * 1000) / m_frameRate;
      if (txdelay < 0) txdelay = 0;
      double baud = 0.25 * m_frameRate / m_nsps;

      // For anyone concerned about the overhead of constructing this format,
      // building the string and sending the packet takes less than 0.1 millisecond.
      // (Repeating this in a loop a million times took 83 seconds.)
      // This happens only when the TNR_PORT environment variable is defined.

      std::ostringstream o;
	 int symcount = m_symbolsLength;
	 if (baud > 21.43 && baud < 21.63) {
	  // Hack for ISCAT.  Limit to transmit time period.
	  // Send whole number of 24 symbol groups.
	  symcount = (int)(m_period * baud / 24) * 24;
	 }
	int repeat = 1;
	if (baud > 1999 && baud < 2001) {
	  // Hack for MSK144.  One group is less than 1/10 second so we need to repeat.
	  repeat = (int)(m_period * baud / symcount);
	 }

      o << "{";
      o << "\"period\":" << m_period << ",";
      o << "\"txdelay\":" << txdelay << ",";
      o << "\"freq0\":" << m_frequency << ",";
      o << "\"spacing\":" << m_toneSpacing << ",";
      o << "\"baud\":" << baud << ",";
	  if (repeat != 1) {
		o << "\"repeat\":" << repeat << ",";
	  }
      o << "\"symcount\":" << symcount << ",";
      o << "\"tones\":[";
      for (int i = 0; i < symcount; i++) {
        o << itone[i];
        if (i < symcount-1) o << ",";
      }
      o << "]}";

      std::string s = o.str();
      if (sendto(m_tnrFd, s.c_str(), s.length(), 0, (struct sockaddr *)(&m_tnrDest), sizeof(m_tnrDest)) < 0) 
      { 
        int e = errno;
        std::cerr << "Tech Night Radio ERROR:  Failed to send UDP packet, errno " << e << std::endl;
      } 
      std::cout << s << std::endl;

      std::cout << "BUILD TIME " << __DATE__ << "  " << __TIME__ << std::endl;
    }
  }

  initialize (QIODevice::ReadOnly, channel);
  Q_EMIT stateChanged ((m_state = (synchronize && m_silentFrames) ?
                        Synchronizing : Active));
  m_stream = stream;
  if (m_stream) m_stream->restart (this);
}

void Modulator::tune (bool newState)
{
  int t = QDateTime::currentMSecsSinceEpoch() % 86400000;
  int h = t / 3600000;  t -= h * 3600000;
  int m = t / 60000; t -= m * 60000;
  int s = t / 1000; t -= s * 1000;

  printf ("[%02d:%02d:%02d.%03d] Modulator::tune  m_tuning=newState = %d\n", h, m, s, t, newState);
  //std::cout << "JWL DEBUG  Modulator::tune  newState = " << newState << std::endl;
  m_tuning = newState;

  if (m_tnrFd != -1) {

    std::ostringstream o;
    o << "{";
    o << "\"tune\":" << m_tuning << ",";
    o << "\"freq0\":" << m_frequency;
    o << "}";
    std::string s = o.str();
    if (sendto(m_tnrFd, s.c_str(), s.length(), 0, (struct sockaddr *)(&m_tnrDest), sizeof(m_tnrDest)) < 0) 
    { 
      int e = errno;
      std::cerr << "Tech Night Radio ERROR:  Failed to send UDP packet, errno " << e << std::endl;
    } 
    std::cout << s << std::endl;
  }

  if ( ! m_tuning)  {
    stop(true);
  }
}


void Modulator::halttx ( bool quick)
{
  (void)quick;

  int t = QDateTime::currentMSecsSinceEpoch() % 86400000;
  int h = t / 3600000;  t -= h * 3600000;
  int m = t / 60000; t -= m * 60000;
  int s = t / 1000; t -= s * 1000;

  printf ("[%02d:%02d:%02d.%03d] Modulator::halttx  quick = %d\n", h, m, s, t, quick);

  if (m_tnrFd != -1) {

		std::string s = "{\"halttx\":1}";
		if (sendto(m_tnrFd, s.c_str(), s.length(), 0, (struct sockaddr *)(&m_tnrDest), sizeof(m_tnrDest)) < 0) 
		{ 
		int e = errno;
		std::cerr << "Tech Night Radio ERROR:  Failed to send UDP packet, errno " << e << std::endl;
		} 
		std::cout << s << std::endl;
	}
	stop(false);
}

void Modulator::stop (bool quick)
{
  int t = QDateTime::currentMSecsSinceEpoch() % 86400000;
  int h = t / 3600000;  t -= h * 3600000;
  int m = t / 60000; t -= m * 60000;
  int s = t / 1000; t -= s * 1000;
  printf ("[%02d:%02d:%02d.%03d] Modulator::hakttx  quick = %d\n", h, m, s, t, quick);

  // There are at least 3 situations where we can be here:
  //  - Tune button turned off:  quick=1  m_state=2 or 1
  //  - Normal end of transmission:  quick=0  m_state=0 (Idle)
  //  - HaltTx button pressed:  quick=0  m_state=0

  // We want to send halttx JSON message only for interrupting an incomplete
  // transmission.  We don't want to send it for a normal end.
  // How can we tell the difference?
  // I couldn't figure out a good way to distinguish between the two cases.
  // My solution was to connect the "HaltTx" button to the new halttx method.
  // It sends the message then calls "stop" as the button did formerly.


  printf ("[%02d:%02d:%02d.%03d] Modulator::stop  quick = %d, m_state = %d\n", h, m, s, t, quick, m_state);
  
  m_quickClose = quick;
  close ();
}

void Modulator::close ()
{
  std::cout << "JWL DEBUG  Modulator::close" << std::endl;
  if (m_stream)
    {
      if (m_quickClose)
        {
          m_stream->reset ();
        }
      else
        {
          m_stream->stop ();
        }
    }
  if (m_state != Idle)
    {
      Q_EMIT stateChanged ((m_state = Idle));
    }
  AudioDevice::close ();
}

qint64 Modulator::readData (char * data, qint64 maxSize)
{
  double toneFrequency=1500.0;

  // m_nsps seems to be number of audio samples per symbol.
  // Why do we have a special case for 6?
  // That value is used for MSK144 but not others that I looked at.

  if(m_nsps==6) {
    toneFrequency=1000.0;
    m_frequency=1000.0;
    m_frequency0=1000.0;
  }
  if(maxSize==0) return 0;
  Q_ASSERT (!(maxSize % qint64 (bytesPerFrame ()))); // no torn frames
  Q_ASSERT (isOpen ());

  qint64 numFrames (maxSize / bytesPerFrame ());
  qint16 * samples (reinterpret_cast<qint16 *> (data));
  qint16 * end (samples + numFrames * (bytesPerFrame () / sizeof (qint16)));
  qint64 framesGenerated (0);

  switch (m_state)
    {
    case Synchronizing:
      {
        if (m_silentFrames)	{  // send silence up to first second
          framesGenerated = qMin (m_silentFrames, numFrames);
          for ( ; samples != end; samples = load (0, samples)) { // silence
          }
          m_silentFrames -= framesGenerated;
          return framesGenerated * bytesPerFrame ();
        }

        Q_EMIT stateChanged ((m_state = Active));
        m_cwLevel = false;
        m_ramp = 0;		// prepare for CW wave shaping
      }
      // fall through

    case Active:
      {
        unsigned int isym=0;
//        qDebug() << "Mod A" << m_toneSpacing << m_ic;
        if(!m_tuning) isym=m_ic/(4.0*m_nsps);            // Actual fsample=48000
        bool slowCwId=((isym >= m_symbolsLength) && (icw[0] > 0)) && (!m_bFastMode);
        if(m_TRperiod==3) slowCwId=false;
        bool fastCwId=false;
        static bool bCwId=false;
        qint64 ms = QDateTime::currentMSecsSinceEpoch();
        float tsec=0.001*(ms % (1000*m_TRperiod));
        if(m_bFastMode and (icw[0]>0) and (tsec>(m_TRperiod-5.0))) fastCwId=true;
        if(!m_bFastMode) m_nspd=2560;                 // 22.5 WPM

        if(slowCwId or fastCwId) {     // Transmit CW ID?
          m_dphi = m_twoPi*m_frequency/m_frameRate;
          if(m_bFastMode and !bCwId) {
            m_frequency=1500;          // Set params for CW ID
            m_dphi = m_twoPi*m_frequency/m_frameRate;
            m_symbolsLength=126;
            m_nsps=4096.0*12000.0/11025.0;
            m_ic=2246949;
            m_nspd=2560;               // 22.5 WPM
            if(icw[0]*m_nspd/48000.0 > 4.0) m_nspd=4.0*48000.0/icw[0];  //Faster CW for long calls
          }
          bCwId=true;
          unsigned ic0 = m_symbolsLength * 4 * m_nsps;
          unsigned j(0);

          while (samples != end) {
            j = (m_ic - ic0)/m_nspd + 1; // symbol of this sample
            bool level {bool (icw[j])};
            m_phi += m_dphi;
            if (m_phi > m_twoPi) m_phi -= m_twoPi;
            qint16 sample=0;
            float amp=32767.0;
            float x=0;
            if(m_ramp!=0) {
              x=qSin(float(m_phi));
              if(SOFT_KEYING) {
                amp=qAbs(qint32(m_ramp));
                if(amp>32767.0) amp=32767.0;
              }
              sample=round(amp*x);
            }
            if(m_bFastMode) {
              sample=0;
              if(level) sample=32767.0*x;
            }
            if (int (j) <= icw[0] && j < NUM_CW_SYMBOLS) { // stop condition
              samples = load (postProcessSample (sample), samples);
              ++framesGenerated;
              ++m_ic;
            } else {
              Q_EMIT stateChanged ((m_state = Idle));
              return framesGenerated * bytesPerFrame ();
            }

            // adjust ramp
            if ((m_ramp != 0 && m_ramp != std::numeric_limits<qint16>::min ()) || level != m_cwLevel) {
              // either ramp has terminated at max/min or direction has changed
              m_ramp += RAMP_INCREMENT; // ramp
            }
            m_cwLevel = level;
          }
          return framesGenerated * bytesPerFrame ();
        } else {
          bCwId=false;
        } //End of code for CW ID

        double const baud (12000.0 / m_nsps);
        // fade out parameters (no fade out for tuning)
        unsigned int i0,i1;
        if(m_tuning) {
          i1 = i0 = (m_bFastMode ? 999999 : 9999) * m_nsps;
        } else {
          i0=(m_symbolsLength - 0.017) * 4.0 * m_nsps;
          i1= m_symbolsLength * 4.0 * m_nsps;
        }
        if(m_bFastMode and !m_tuning) {
          i1=m_TRperiod*48000 - 24000;
          i0=i1-816;
        }

        qint16 sample;
        for (unsigned i = 0; i < numFrames && m_ic <= i1; ++i) {
          isym=0;
          if(!m_tuning and m_TRperiod!=3) isym=m_ic / (4.0 * m_nsps);         //Actual
                                                                              //fsample=48000
	      if(m_bFastMode) isym=isym%m_symbolsLength;
          if (isym != m_isym0 || m_frequency != m_frequency0) {
            if(itone[0]>=100) {
              m_toneFrequency0=itone[0];
            } else {
              if(m_toneSpacing==0.0) {
                m_toneFrequency0=m_frequency + itone[isym]*baud;
              } else {
                m_toneFrequency0=m_frequency + itone[isym]*m_toneSpacing;
              }
            }
//            qDebug() << "Mod B" << m_bFastMode << m_ic << numFrames << isym << itone[isym]
//                     << m_toneFrequency0 << m_nsps;
            m_dphi = m_twoPi * m_toneFrequency0 / m_frameRate;
            m_isym0 = isym;
            m_frequency0 = m_frequency;         //???
          }

          int j=m_ic/480;
          if(m_fSpread>0.0 and j!=m_j0) {
            float x1=(float)qrand()/RAND_MAX;
            float x2=(float)qrand()/RAND_MAX;
            toneFrequency = m_toneFrequency0 + 0.5*m_fSpread*(x1+x2-1.0);
            m_dphi = m_twoPi * toneFrequency / m_frameRate;
            m_j0=j;
          }

          m_phi += m_dphi;
          if (m_phi > m_twoPi) m_phi -= m_twoPi;
          if (m_ic > i0) m_amp = 0.98 * m_amp;
          if (m_ic > i1) m_amp = 0.0;

          sample=qRound(m_amp*qSin(m_phi));
          if(m_toneSpacing < 0) sample=qRound(m_amp*foxcom_.wave[m_ic]);

//          if(m_ic < 100) qDebug() << "Mod C" << m_ic << m_amp << foxcom_.wave[m_ic] << sample;

          samples = load(postProcessSample(sample), samples);
          ++framesGenerated;
          ++m_ic;
        }

        if (m_amp == 0.0) { // TODO G4WJS: compare double with zero might not be wise
          if (icw[0] == 0) {
            // no CW ID to send
            Q_EMIT stateChanged ((m_state = Idle));
            return framesGenerated * bytesPerFrame ();
          }
          m_phi = 0.0;
        }

        m_frequency0 = m_frequency;
        // done for this chunk - continue on next call
        return framesGenerated * bytesPerFrame ();
      }
      // fall through
      // never gets here.

    case Idle:
      break;
    }

  Q_ASSERT (Idle == m_state);
  return 0;
}

qint16 Modulator::postProcessSample (qint16 sample) const
{
  if (m_addNoise) {  // Test frame, we'll add noise
    qint32 s = m_fac * (gran () + sample * m_snr / 32768.0);
    if (s > std::numeric_limits<qint16>::max ()) {
      s = std::numeric_limits<qint16>::max ();
    }
    if (s < std::numeric_limits<qint16>::min ()) {
      s = std::numeric_limits<qint16>::min ();
    }
    sample = s;
  }
  return sample;
}
