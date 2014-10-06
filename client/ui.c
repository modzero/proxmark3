//-----------------------------------------------------------------------------
// Copyright (C) 2009 Michael Gernoth <michael at gernoth.net>
// Copyright (C) 2010 iZsh <izsh at fail0verflow.com>
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// UI utilities
//-----------------------------------------------------------------------------

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <readline/readline.h>
#include <pthread.h>
#include "ui.h"
#include "loclass/cipherutils.h"

double CursorScaleFactor;
int PlotGridX, PlotGridY, PlotGridXdefault= 64, PlotGridYdefault= 64;
int offline;
int flushAfterWrite = 0;  //buzzy
extern pthread_mutex_t print_lock;

static char *logfilename = "proxmark3.log";

void PrintAndLog(char *fmt, ...)
{
	char *saved_line;
	int saved_point;
	va_list argptr, argptr2;
	static FILE *logfile = NULL;
	static int logging=1;

	// lock this section to avoid interlacing prints from different threats
	pthread_mutex_lock(&print_lock);
  
	if (logging && !logfile) {
		logfile=fopen(logfilename, "a");
		if (!logfile) {
			fprintf(stderr, "Can't open logfile, logging disabled!\n");
			logging=0;
		}
	}
	
	int need_hack = (rl_readline_state & RL_STATE_READCMD) > 0;

	if (need_hack) {
		saved_point = rl_point;
		saved_line = rl_copy_text(0, rl_end);
		rl_save_prompt();
		rl_replace_line("", 0);
		rl_redisplay();
	}
	
	va_start(argptr, fmt);
	va_copy(argptr2, argptr);
	vprintf(fmt, argptr);
	printf("          "); // cleaning prompt
	va_end(argptr);
	printf("\n");

	if (need_hack) {
		rl_restore_prompt();
		rl_replace_line(saved_line, 0);
		rl_point = saved_point;
		rl_redisplay();
		free(saved_line);
	}
	
	if (logging && logfile) {
		vfprintf(logfile, fmt, argptr2);
		fprintf(logfile,"\n");
		fflush(logfile);
	}
	va_end(argptr2);

	if (flushAfterWrite == 1)  //buzzy
	{
		fflush(NULL);
	}
	//release lock
	pthread_mutex_unlock(&print_lock);  
}

void SetLogFilename(char *fn)
{
  logfilename = fn;
}

int manchester_decode( int * data, const size_t len, uint8_t * dataout){
	
	int bitlength = 0;
	int i, clock, high, low, startindex;
	low = startindex = 0;
	high = 1;
	uint8_t bitStream[len];
	
	memset(bitStream, 0x00, len);
	
	/* Detect high and lows */
	for (i = 0; i < len; i++) {
		if (data[i] > high)
			high = data[i];
		else if (data[i] < low)
			low = data[i];
	}
	
	/* get clock */
	clock = GetT55x7Clock( data, len, high );	
	startindex = DetectFirstTransition(data, len, high);
  
	PrintAndLog(" Clock       : %d", clock);
	PrintAndLog(" startindex  : %d", startindex);
	
	if (high != 1)
		bitlength = ManchesterConvertFrom255(data, len, bitStream, high, low, clock, startindex);
	else
		bitlength= ManchesterConvertFrom1(data, len, bitStream, clock, startindex);

	if ( bitlength > 0 )
		PrintPaddedManchester(bitStream, bitlength, clock);

	memcpy(dataout, bitStream, bitlength);
	
	free(bitStream);
	return bitlength;
}

 int GetT55x7Clock( const int * data, const size_t len, int peak ){ 
 
 	int i,lastpeak,clock;
	clock = 0xFFFF;
	lastpeak = 0;
	
	/* Detect peak if we don't have one */
	if (!peak) {
		for (i = 0; i < len; ++i) {
			if (data[i] > peak) {
				peak = data[i];
			}
		}
	}
	
	for (i = 1; i < len; ++i) {
		/* if this is the beginning of a peak */
		if ( data[i-1] != data[i] &&  data[i] == peak) {
		  /* find lowest difference between peaks */
			if (lastpeak && i - lastpeak < clock)
				clock = i - lastpeak;
			lastpeak = i;
		}
	}
	//return clock;  
 	//defaults clock to precise values.
	switch(clock){
		case 8:
		case 16:
		case 32:
		case 40:
		case 50:
		case 64:
		case 100:
		case 128:
		return clock;
		break;
		default:  break;
	}
	
	PrintAndLog(" Found Clock : %d  - trying to adjust", clock);
	
	// When detected clock is 31 or 33 then then return 
	int clockmod = clock%8;
	if ( clockmod == 7 ) 
		clock += 1;
	else if ( clockmod == 1 )
		clock -= 1;
	
	return clock;
 }
 
 int DetectFirstTransition(const int * data, const size_t len, int threshold){

	int i =0;
	/* now look for the first threshold */
	for (; i < len; ++i) {
		if (data[i] == threshold) {
			break;
		}
	}
	return i;
 }

 int ManchesterConvertFrom255(const int * data, const size_t len, uint8_t * dataout, int high, int low, int clock, int startIndex){

	int i, j, z, hithigh, hitlow, bitIndex, startType;
	i = 0;
	bitIndex = 0;
	
	int isDamp = 0;
	int damplimit = (int)((high / 2) * 0.3);
	int dampHi =  (high/2)+damplimit;
	int dampLow = (high/2)-damplimit;
	int firstST = 0;

	// i = clock frame of data
	for (; i < (int)(len / clock); i++)
	{
		hithigh = 0;
		hitlow = 0;
		startType = -1;
		z = startIndex + (i*clock);
		isDamp = 0;
		
	
		/* Find out if we hit both high and low peaks */
		for (j = 0; j < clock; j++)
		{		
			if (data[z+j] == high){
				hithigh = 1;
				if ( startType == -1)
					startType = 1;
			}
			
			if (data[z+j] == low ){
				hitlow = 1;
				if ( startType == -1)
					startType = 0;
			} 
		
			if (hithigh && hitlow)
			  break;
		}
		
		// No high value found, are we in a dampening field?
		if ( !hithigh ) {
			//PrintAndLog(" # Entering damp test at index : %d (%d)", z+j, j);
			for (j = 0; j < clock/2; j++)
			{
				if ( 
				     (data[z+j] <= dampHi && data[z+j] >= dampLow)
				   ){
				   isDamp = 1;
				}
				else 
				   isDamp = 0;
			}
		}

		/*  Manchester Switching..
			0: High -> Low   
			1: Low -> High  
		*/
		if (startType == 0)
			dataout[bitIndex++] = 1;
		else if (startType == 1) 
			dataout[bitIndex++] = 0;
		else
			dataout[bitIndex++] = 2;
			
		if ( isDamp ) {
			firstST++;
		}
		
		if ( firstST == 4)
			break;
	}
	return bitIndex;
 }
 
 int ManchesterConvertFrom1(const int * data, const size_t len, uint8_t * dataout, int clock, int startIndex){

	PrintAndLog(" Path B");
 
	int i,j, bitindex, lc, tolerance, warnings;
	warnings = 0;
	int upperlimit = len*2/clock+8;
	i = startIndex;
	j = 0;
	tolerance = clock/4;
	uint8_t decodedArr[len];
	
	/* Detect duration between 2 successive transitions */
	for (bitindex = 1; i < len; i++) {
	
		if (data[i-1] != data[i]) {
			lc = i - startIndex;
			startIndex = i;

			// Error check: if bitindex becomes too large, we do not
			// have a Manchester encoded bitstream or the clock is really wrong!
			if (bitindex > upperlimit ) {
				PrintAndLog("Error: the clock you gave is probably wrong, aborting.");
				return 0;
			}
			// Then switch depending on lc length:
			// Tolerance is 1/4 of clock rate (arbitrary)
			if (abs((lc-clock)/2) < tolerance) {
				// Short pulse : either "1" or "0"
				decodedArr[bitindex++] = data[i-1];
			} else if (abs(lc-clock) < tolerance) {
				// Long pulse: either "11" or "00"
				decodedArr[bitindex++] = data[i-1];
				decodedArr[bitindex++] = data[i-1];
			} else {
				++warnings;
				PrintAndLog("Warning: Manchester decode error for pulse width detection.");
				if (warnings > 10) {
					PrintAndLog("Error: too many detection errors, aborting.");
					return 0; 
				}
			}
		}
	}
	
	/* 
	* We have a decodedArr of "01" ("1") or "10" ("0")
	* parse it into final decoded dataout
    */ 
    for (i = 0; i < bitindex; i += 2) {

	    if ((decodedArr[i] == 0) && (decodedArr[i+1] == 1)) {
			dataout[j++] = 1;
		} else if ((decodedArr[i] == 1) && (decodedArr[i+1] == 0)) {
			dataout[j++] = 0;
		} else {
			i++;
			warnings++;
			PrintAndLog("Unsynchronized, resync...");
			PrintAndLog("(too many of those messages mean the stream is not Manchester encoded)");

			if (warnings > 10) {	
				PrintAndLog("Error: too many decode errors, aborting.");
				return 0;
			}
		}
    }
	
	PrintAndLog("%s", sprint_hex(dataout, j));
	return j;
 }
 
 void ManchesterDiffDecodedString(const uint8_t* bitstream, size_t len, uint8_t invert){
	/* 
	* We have a bitstream of "01" ("1") or "10" ("0")
	* parse it into final decoded bitstream
    */ 
	int i, j, warnings; 
	uint8_t decodedArr[(len/2)+1];

	j = warnings = 0;
	
	uint8_t lastbit = 0;
	
    for (i = 0; i < len; i += 2) {
	
		uint8_t first = bitstream[i];
		uint8_t second = bitstream[i+1];

		if ( first == second ) {
			++i;
			++warnings;
			if (warnings > 10) {
				PrintAndLog("Error: too many decode errors, aborting.");
				return;
			}
		} 
		else if ( lastbit != first ) {
			decodedArr[j++] = 0 ^ invert;
		}
		else {
			decodedArr[j++] = 1 ^ invert;
		}
		lastbit = second;
    }
	
	PrintAndLog("%s", sprint_hex(decodedArr, j));
}
 
void PrintPaddedManchester( uint8_t* bitStream, size_t len, size_t blocksize){

	PrintAndLog(" Manchester decoded  : %d bits", len);
	  
	uint8_t mod = len % blocksize;
	uint8_t div = len / blocksize;
	int i;
  
	// Now output the bitstream to the scrollback by line of 16 bits
	for (i = 0; i < div*blocksize; i+=blocksize) {
		PrintAndLog(" %s", sprint_bin(bitStream+i,blocksize) );
	}
	
	if ( mod > 0 )
		PrintAndLog(" %s", sprint_bin(bitStream+i, mod) );	
}