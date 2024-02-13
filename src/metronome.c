#include <stdio.h>
#include <stdlib.h> 	/* malloc() */
#include <unistd.h>     /* sleep() */
#include <string.h>	/* memset() */
#include <ctype.h>	/* lolower() */
#include <sndfile.h>	/* libsndfile */
#include <portaudio.h>	/* portaudio */
#include <stdatomic.h>
#include "paUtils.h"

#define MAX_PATH_LEN        256
#define MAX_FILES	    	2
#define MAX_CHN	            2
#define LINE_LEN			80
//#define FRAMES_PER_BUFFER   1024

/* data structure to pass to callback */
typedef struct {
	atomic_int selection;	/* so selection is thread-safe */
	unsigned int channels;
	unsigned int samplerate;
	unsigned int frames;
	float *x[MAX_FILES];
	float *first_sample[MAX_FILES];
	float *next_sample[MAX_FILES];
	float *last_sample[MAX_FILES];
	/*Add the following */
	int beatsPerMinute;
	int beatsPerMeasure;
	float framePerBeatFloat;
	int framePerBeat;
	int framePerLoop;
} Buf;

/* PortAudio callback function protoype */
static int paCallback( const void *inputBuffer, void *outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData );

/* Clears screen using ANSI escape sequences. */
void clear()
{
    printf("\033[2J");
    printf("\033[%d;%dH", 0, 0);
}

void user_input(char ifilename[MAX_FILES][MAX_PATH_LEN], PaStream *stream,Buf *p)
{
    clear();
	printf("==========================\n");
	printf("     Simple    Metronome  \n");
	printf("==========================\n");
	printf("DownBeatFile: %s\n",ifilename[0]);
	printf("UpBeatFile: %s\n",ifilename[1]);
	printf("beatsPerMinute(BPM): %d\nbeatsPerMeasure: %d\n",p->beatsPerMinute,p->beatsPerMeasure);
	printf("CPU Load: %lf\n", Pa_GetStreamCpuLoad (stream) );
	printf("P to play, M to stop, Q to quit\nEnter here:\n"); 
    fflush(stdout);
}

int main(int argc, char *argv[])
{
  	char ifilename[MAX_FILES][MAX_PATH_LEN], ch, line[LINE_LEN];
	char *ifile;
  	int i, num_input_files;
	unsigned long count;
  	FILE *fp;
  	/* my data structure */
  	Buf buf, *p = &buf;
  	/* ibsndfile structures */
	SNDFILE *sndfile; 
	SF_INFO sfinfo;
  	/* PortAudio stream */
	PaStream *stream;

  	/* zero libsndfile structures */
	memset(&sfinfo, 0, sizeof(sfinfo));

	/* initialize selection: -1 indicates don't play any file */
	p->selection = -1;

  	/* Parse command line and open all files */
	if(argc != 4){
		fprintf(stderr,"Usage: %s ifile.txt beatsPerMinute beatsPerMeasure\n",argv[0]);
		return -1;
	}
	
  	/* open list of files */
	ifile = argv[1];

	fp = fopen(ifile,"r");
	if(fp == NULL){
		fprintf(stderr,"Cannot open input wav file%s\n",ifile);
	}

	printf("============================================\n");
	printf("                  Loading...                \n");
	printf("============================================\n");

	for (i = 0; i<MAX_FILES; i++) {
		if (fscanf(fp, "%s", ifilename[i]) == EOF)
			break;
		
		if ((sndfile = sf_open (ifilename[i], SFM_READ, &sfinfo)) == NULL){
			fprintf (stderr, "Error: cannot open input wav file %s\n", ifilename[i]);
			return -1;
		}

		printf("\nFileName:%s\n",ifilename[i]);
		printf(" Frames: %8d\n Channels: %d\n Samplerate: %d\n",(int)sfinfo.frames, sfinfo.channels, sfinfo.samplerate);
		
		/* check compatibility of input WAV files */		
		if (sfinfo.channels != MAX_CHN) {
			fprintf(stderr, "ERROR: input channels greater than %d\n", MAX_CHN);
			return -1;
		}

		p->channels = sfinfo.channels;
     	p->samplerate = sfinfo.samplerate;
     	p->frames = sfinfo.frames;

     	p->beatsPerMinute = atoi(argv[2]);
    	p->beatsPerMeasure = atoi(argv[3]);

    	/* check the range of beatsPerMinute and BeatsPerMeasure */
    	if ((p->beatsPerMinute < 30) || (p->beatsPerMinute > 280))
    	{
        	fprintf(stderr,"Error: beatsPerMinute must between 30 and 280!");
        	return -1;
    	}

    	if ((p->beatsPerMeasure < 2) || (p->beatsPerMeasure > 12))
    	{
        	fprintf(stderr,"Error: beatsPerMeasure must between 2 and 12!");
        	return -1;        
    	}

    	p->framePerBeatFloat =  60 * p->samplerate / p->beatsPerMinute;
		p->framePerBeat = (int)p->framePerBeatFloat;
		p->framePerLoop = p->framePerBeat * p->beatsPerMeasure;

		printf(" framePerLoop: %d\n framePerBeat: %d\n beatsPerMeasure: %d\n",p->framePerLoop,p->framePerBeat,p->beatsPerMeasure);

		if(i > 0){
			if(p->channels != sfinfo.channels){
				fprintf(stderr, "ERROR: The channel number is not the same");
				return -1;
			}
			if(p->samplerate != sfinfo.samplerate){
				fprintf(stderr, "ERROR: The sample rate is not the same");
				return -1;
			}
			if(p->frames > p->framePerBeat){
				fprintf(stderr,"ERROR: Input sound too large!");
				return -1;
			}
		} 

		/* malloc storage and read audio data into buffer */
		p->x[i] = (float *)malloc(p->framePerLoop * sfinfo.channels * sizeof(float));
		
		if (p->x[i] == NULL){
			fprintf(stderr, "ERROR: the audio data is empty");
			return -1;
		}

		count = sf_readf_float(sndfile, p->x[i], sfinfo.frames);
 		if (count != sfinfo.frames){
			fprintf(stderr, "The number of frames read is not the same as the number of frames in the file");
			return -1;
		}

		/* initialize next sample and last sample*/
		p->first_sample[i] = p->x[i];
		p->next_sample[i] = p->x[i];

		/* Close WAV file */
		sf_close(sndfile);
	}

	/*define input file number*/
	num_input_files = i;
   	
   	/* close input filelist */
	fclose(fp);

	/* pause so user can read console printout */
	sleep(3);

	/* start up Port Audio */
	stream = startupPa(1, p->channels, p->samplerate, 
		p->framePerLoop, paCallback, &buf);

  	/* User Input */
	user_input(ifilename, stream,&buf);

	ch = 0;
	while (ch != 'q') {
		fgets(line, LINE_LEN, stdin); 
		ch = tolower(line[0]);
		if (ch == 'm') {
			p->selection = -1;
		}
		if (ch == 'p') {
			p->selection = 0;
		}
		user_input(ifilename, stream,&buf);
  	}
  	clear();

	/* shut down Port Audio */
	shutdownPa(stream);

	/* free storage */
	for (int i=0; i<num_input_files; i++) {
		free(p->x[i]);
	}

	return 0;
}

static int paCallback(const void *inputBuffer, void *outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData)
{
	Buf *p = (Buf *)userData; /* Cast data passed through stream to our structure. */
	int selection = p->selection;
	int count = 0;
	float *po = (float *)outputBuffer; /* Pointer to represent outputBuffer */
	float *pn;

	/* calculate essential information to generate output buffer */
	int samplesPerBuffer = framesPerBuffer * p->channels;
	int samplesPerBeat = p->framePerBeat * p->channels;
	
	/* default value of selection is -1, which would fill output buffer with zeros. */
	if (selection == -1){
		for (int i = 0;i < samplesPerBuffer;i++){
			*po++ = 0.0;
		}
	}else{
		for(int i = 0;i < p->beatsPerMeasure;i++){
			if(count == p->beatsPerMeasure){
				count = 0;
			}
			if(count == 0){
				pn = p->first_sample[0];
			}else{
				pn = p->first_sample[1];
			}
			for(int j = 0; j < p->frames * p->channels;j++){
				po[i * samplesPerBeat + j] = *pn++;
			}
			for(int j = p->frames * p->channels; j < samplesPerBeat;j++){
				po[i * samplesPerBeat + j] = 0.0;
			}
			count += 1;
		}		
	}
	return 0;
}