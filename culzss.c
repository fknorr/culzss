/***************************************************************************
 *          Lempel, Ziv, Storer, and Szymanski Encoding and Decoding on CUDA
 *
 *
 ****************************************************************************
 *          CUDA LZSS
 *   Authors  : Adnan Ozsoy, Martin Swany,Indiana University - Bloomington
 *   Date    : April 11, 2011

 ****************************************************************************

         Copyright 2011 Adnan Ozsoy, Martin Swany, Indiana University - Bloomington

         Licensed under the Apache License, Version 2.0 (the "License");
         you may not use this file except in compliance with the License.
         You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

         Unless required by applicable law or agreed to in writing, software
         distributed under the License is distributed on an "AS IS" BASIS,
         WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
         See the License for the specific language governing permissions and
         limitations under the License.
 ****************************************************************************/

 /***************************************************************************
 * Code is adopted from below source
 *
 * LZSS: An ANSI C LZss Encoding/Decoding Routine
 * Copyright (C) 2003 by Michael Dipperstein (mdipper@cs.ucsb.edu)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 ***************************************************************************/

 #include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "culzss.h"
#include <sys/time.h>
#include <string.h>

static pthread_t congpu, constr, concpu, consend;

static int loopnum=0;
static int maxiterations=0;
static int numblocks=0;
static int blocksize=0;
static unsigned int * bookkeeping;

static volatile int exit_signal = 0;
static size_t out_stream_cursor;
static size_t compressed_size;
static void *out_stream;

static size_t out_stream_write(const void *buffer, size_t size, size_t items) {
    memcpy((char*) out_stream + out_stream_cursor, buffer, size * items);
    out_stream_cursor += size * items;
    return items;
}

int CULZSSp_getloopcount(){
	return loopnum;
}

static uint64_t kernel_time_us;

struct CUevent_st;
void CULZSSp_gpu_bench_start(struct CUevent_st **begin, struct CUevent_st **end);
uint64_t CULZSSp_gpu_bench_finish(struct CUevent_st *begin, struct CUevent_st *end);

static void *gpu_consumer (void *q)
{

	struct timeval t1_start,t1_end,t2_start,t2_end;
	double time_d, alltime;

	queue *fifo;
	int i, d;
	int success=0;
	fifo = (queue *)q;
	int comp_length=0;

	fifo->in_d = CULZSSp_initGPUmem((int) blocksize);
	fifo->out_d = CULZSSp_initGPUmem((int) blocksize * 2);

	struct CUevent_st *begin, *end;
    CULZSSp_gpu_bench_start(&begin, &end);

	for (i = 0; i < maxiterations; i++) {

		if(exit_signal){
			exit_signal++;
			break;
		}

		gettimeofday(&t1_start,0);

		pthread_mutex_lock (fifo->mut);
		while (fifo->ledger[fifo->headGC]!=1)
		{
			//printf ("gpu consumer: queue EMPTY.\n");
			pthread_cond_wait (fifo->produced, fifo->mut);
		}
		pthread_mutex_unlock (fifo->mut);

		gettimeofday(&t2_start,0);

		success= CULZSSp_compression_kernel_wrapper(fifo->buf[fifo->headGC], blocksize, fifo->bufout[fifo->headGC],
            0, 0, 128, 0, fifo->headGC, fifo->in_d, fifo->out_d);
		if(!success){
			printf("Compression failed. Success %d\n",success);
		}

		gettimeofday(&t2_end,0);
		time_d = (t2_end.tv_sec-t2_start.tv_sec) + (t2_end.tv_usec - t2_start.tv_usec)/1000000.0;
		//printf("GPU kernel took:\t%f \t", time_d);

		pthread_mutex_lock (fifo->mut);
		fifo->ledger[fifo->headGC]++;
		fifo->headGC++;
		if (fifo->headGC == numblocks)
			fifo->headGC = 0;

		pthread_mutex_unlock (fifo->mut);

		pthread_cond_signal (fifo->compressed);

		gettimeofday(&t1_end,0);
		alltime = (t1_end.tv_sec-t1_start.tv_sec) + (t1_end.tv_usec - t1_start.tv_usec)/1000000.0;
		//printf("GPU whole took:\t%f \n", alltime);
	}

	kernel_time_us = CULZSSp_gpu_bench_finish(begin, end);

    CULZSSp_deleteGPUmem(fifo->in_d);
    CULZSSp_deleteGPUmem(fifo->out_d);

	return (NULL);
}


static void *cpu_consumer (void *q)
{

	struct timeval t1_start,t1_end,t2_start,t2_end;
	double time_d, alltime;

	int i;
	int success=0;
	queue *fifo;
	fifo = (queue *)q;
	int comp_length=0;
	unsigned char * bckpbuf;
	bckpbuf = (unsigned char *)malloc(sizeof(unsigned char)*blocksize);

	for (i = 0; i < maxiterations; i++) {

		if(exit_signal){
			exit_signal++;
			break;
		}
		gettimeofday(&t1_start,0);

		pthread_mutex_lock (fifo->mut);
		while (fifo->ledger[fifo->headCS]!=2) {
			//printf ("cpu consumer: queue EMPTY.\n");
			pthread_mutex_unlock (fifo->mut);
			pthread_mutex_lock (fifo->mut);
		}
		pthread_mutex_unlock (fifo->mut);

        CULZSSp_onestream_finish_GPU(fifo->headCS);

		gettimeofday(&t2_start,0);


		memcpy (bckpbuf, fifo->buf[fifo->headCS], blocksize);
		success= CULZSSp_aftercompression_wrapper(fifo->buf[fifo->headCS], blocksize, fifo->bufout[fifo->headCS],
            &comp_length);
		if(!success){
			printf("After Compression failed. Success %d return size %d\n",success,comp_length);
			fifo->outsize[fifo->headCS] = 0;
			memcpy (fifo->buf[fifo->headCS],bckpbuf,  blocksize);
		}
		else
			fifo->outsize[fifo->headCS] = comp_length;


		gettimeofday(&t2_end,0);
		time_d = (t2_end.tv_sec-t2_start.tv_sec) + (t2_end.tv_usec - t2_start.tv_usec)/1000000.0;

		pthread_mutex_lock (fifo->mut);
		fifo->ledger[fifo->headCS]++;//=0;
		fifo->headCS++;
		if (fifo->headCS == numblocks)
			fifo->headCS = 0;

		pthread_mutex_unlock (fifo->mut);

		pthread_cond_signal (fifo->sendready);
		gettimeofday(&t1_end,0);
		alltime = (t1_end.tv_sec-t1_start.tv_sec) + (t1_end.tv_usec - t1_start.tv_usec)/1000000.0;
	}
	return (NULL);
}

static void *cpu_sender (void *q)
{
	struct timeval t1_start,t1_end;//,t2_start,t2_end;
	double  alltime;//time_d,

	int i;
	int success=0;
	queue *fifo;
	fifo = (queue *)q;
	int size=0;

	out_stream_cursor = 0;

	out_stream_write(bookkeeping, sizeof(unsigned int), maxiterations+2);

	for (i = 0; i < maxiterations; i++)
	{
		if(exit_signal){
			exit_signal++;
			break;
		}
		gettimeofday(&t1_start,0);

		pthread_mutex_lock (fifo->mut);
		while (fifo->ledger[fifo->headSP]!=3) {
			//printf ("cpu_sender: queue EMPTY.\n");
			pthread_cond_wait (fifo->sendready, fifo->mut);
		}
		pthread_mutex_unlock (fifo->mut);


		pthread_mutex_lock (fifo->mut);

		size = fifo->outsize[fifo->headSP];
		if(size == 0)
			size = blocksize;
		bookkeeping[i + 2] = size + ((i==0)?0:bookkeeping[i+1]);

		out_stream_write(fifo->buf[fifo->headSP], size, 1);

		//gettimeofday(&t2_end,0);
		//time_d = (t2_end.tv_sec-t2_start.tv_sec) + (t2_end.tv_usec - t2_start.tv_usec)/1000000.0;

		fifo->ledger[fifo->headSP]=0;
		fifo->headSP++;
		if (fifo->headSP == numblocks)
			fifo->headSP = 0;

		pthread_mutex_unlock (fifo->mut);

		pthread_cond_signal (fifo->sent);

		gettimeofday(&t1_end,0);
		alltime = (t1_end.tv_sec-t1_start.tv_sec) + (t1_end.tv_usec - t1_start.tv_usec)/1000000.0;
		loopnum++;
	}
	compressed_size = out_stream_cursor;
	out_stream_cursor = 0;
	out_stream_write(bookkeeping, sizeof(unsigned int), maxiterations+2);

	return (NULL);
}



queue *CULZSSp_queueInit (int maxit,int numb,int bsize)
{
	queue *q;
	maxiterations=maxiterations;
	numblocks=numblocks;
	blocksize=blocksize;

	q = (queue *)malloc (sizeof (queue));
	if (q == NULL) return (NULL);

	int i;
	//alloc bufs
	unsigned char ** buffer;
	unsigned char ** bufferout;


	/*  allocate storage for an array of pointers */
	buffer = (unsigned char **)malloc((numblocks) * sizeof(unsigned char *));
	if (buffer == NULL) {
		printf("Error: malloc could not allocate buffer\n");
        abort();
	}
	bufferout = (unsigned char **)malloc((numblocks) * sizeof(unsigned char *));
	if (bufferout == NULL) {
		printf("Error: malloc could not allocate bufferout\n");
        abort();
	}

	/* for each pointer, allocate storage for an array of chars */
	for (i = 0; i < (numblocks); i++) {
		//buffer[i] = (unsigned char *)malloc(blocksize * sizeof(unsigned char));
		buffer[i] = (unsigned char *) CULZSSp_initCPUmem(blocksize * sizeof(unsigned char));
		if (buffer[i] == NULL) {printf ("Memory error, buffer"); exit (2);}
	}
	for (i = 0; i < (numblocks); i++) {
		//bufferout[i] = (unsigned char *)malloc(blocksize * 2 * sizeof(unsigned char));
		bufferout[i] = (unsigned char *) CULZSSp_initCPUmem(blocksize * 2 * sizeof(unsigned char));
		if (bufferout[i] == NULL) {printf ("Memory error, bufferout"); exit (2);}
	}

	q->buf = buffer;
	q->bufout = bufferout;

	q->headPG = 0;
	q->headGC = 0;
	q->headCS = 0;
	q->headSP = 0;

	q->outsize = (int *)malloc(sizeof(int)*numblocks);

	q->ledger = (int *)malloc((numblocks) * sizeof(int));
	if (q->ledger == NULL) {
		printf("Error: malloc could not allocate q->ledger\n");
        abort();
	}

	for (i = 0; i < (numblocks); i++) {
		q->ledger[i] = 0;
	}

	q->mut = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
	pthread_mutex_init (q->mut, NULL);

	q->produced = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
	pthread_cond_init (q->produced, NULL);
	q->compressed = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
	pthread_cond_init (q->compressed, NULL);
	q->sendready = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
	pthread_cond_init (q->sendready, NULL);
	q->sent = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
	pthread_cond_init (q->sent, NULL);

	return (q);
}


void CULZSSp_signalExitThreads()
{
	exit_signal++;
	while(! (exit_signal > 3));
}


void CULZSSp_queueDelete (queue *q)
{
	int i =0;

    CULZSSp_signalExitThreads();

	pthread_mutex_destroy (q->mut);
	free (q->mut);
	pthread_cond_destroy (q->produced);
	free (q->produced);
	pthread_cond_destroy (q->compressed);
	free (q->compressed);
	pthread_cond_destroy (q->sendready);
	free (q->sendready);
	pthread_cond_destroy (q->sent);
	free (q->sent);


	for (i = 0; i < (numblocks); i++) {
        CULZSSp_deleteCPUmem(q->bufout[i]);
        CULZSSp_deleteCPUmem(q->buf[i]);
	}

    CULZSSp_deleteGPUStreams();

	free(q->buf);
	free(q->bufout);
	free(q->ledger);
	free(q->outsize);
	free (q);


    CULZSSp_resetGPU();

}


void  CULZSSp_init_compression(queue * q,int maxiterations,int numblocks,int blocksize, void * out, unsigned int * book)
{
	maxiterations=maxiterations;
	numblocks=numblocks;
	blocksize=blocksize;
	out_stream = out;
	bookkeeping = book;
	printf("Initializing the GPU\n");
    CULZSSp_initGPU();
	//create consumer threades
	pthread_create (&congpu, NULL, gpu_consumer, q);
	pthread_create (&concpu, NULL, cpu_consumer, q);
	pthread_create (&consend, NULL, cpu_sender, q);
}

void CULZSSp_join_comp_threads()
{
	pthread_join (congpu, NULL);
	pthread_join (concpu, NULL);
	pthread_join (consend, NULL);
	exit_signal = 3;
}


size_t CULZSSp_last_compressed_size()
{
    return compressed_size;
}


uint64_t CULZSSp_last_compression_kernel_time_us()
{
    return kernel_time_us;
}
