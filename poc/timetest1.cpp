// Question:  
//
// What resolution does the RPi 3 real time clock function provide?
//
//
// Experiment:
//
// The gettimeofday function returns the time in two integers, seconds
// and microseconds within each second.  Let's try calling it repeatedly
// for one second and see what values we get.
//
//
// Results:
//
// 998990 unique usec values out of 6123478 samples.
// 999231 unique usec values out of 6126214 samples.
// 999220 unique usec values out of 6130333 samples.
// 998178 unique usec values out of 6078421 samples.
// 999285 unique usec values out of 6123188 samples.
// 
//
// What does this mean?
//
//  (1) We are able to obtain the time value more than 6 million times per second.
//  (2) Nearly all of the possible microsecond values occur during this period.
//      If we had about a millisecond resolution, there would be only a 
//      thousand unique values during a second.
//


#include <sys/time.h>
#include <iostream>
#include <assert.h>
#include <string.h>	// for memset
using namespace std;

main ()
{
        struct timeval tv;
	int us_count[1000000];

	memset (us_count, 0, sizeof(us_count));

        gettimeofday (&tv, NULL);
        int s = tv.tv_sec + 1;

	do {
          gettimeofday (&tv, NULL);
	} while (tv.tv_sec != s);

	while (tv.tv_sec == s) {
	  assert (tv.tv_usec >= 0 && tv.tv_usec <= 999999);
	  us_count[tv.tv_usec]++; 
          gettimeofday (&tv, NULL);
	}

	int samples = 0;
	int unique = 0;
	for (int n = 0; n < 1000000; n++) {
	  if (us_count[n] != 0) {
	    samples += us_count[n];
	    unique++;
	  }
	}

	cout << unique << " unique usec values out of " << samples << " samples." << endl;
}  

