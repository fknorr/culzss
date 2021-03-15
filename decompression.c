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

#include "decompression.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "deculzss.h"
#include <string.h>
#include <assert.h>


int bufsize=0;
int numbufs=0;


static const void *in_stream;
static size_t in_stream_size;
static void *out_stream;

static size_t in_stream_read(void *buffer, size_t size, size_t items, size_t *cursor) {
    assert(*cursor <= in_stream_size);
    size_t remaining_items = (in_stream_size - *cursor) / size;
    size_t read = items < remaining_items ? items : remaining_items;
    memcpy(buffer, (const char*) in_stream + *cursor, read * size);
    *cursor += read * size;
    return read;
}

void *receiver (void *q)
{
    size_t in_stream_cursor = 0;

	dequeue *fifo;
	int i;

	fifo = (dequeue *)q;

	int * readsize;
	readsize = (int *)malloc(sizeof(int)*numbufs);

	int size=0;

	in_stream_read (&size,sizeof(unsigned int), 1,&in_stream_cursor); //trash
	in_stream_read (&size,sizeof(unsigned int), 1,&in_stream_cursor); //trash
	in_stream_read (readsize,sizeof(unsigned int), numbufs,&in_stream_cursor);

	for (i = 0; i < numbufs; i++) {


		pthread_mutex_lock (fifo->mut);
		while (fifo->ledger[fifo->headRG]!=0) {
			//printf ("receiver: queue FULL.\n");
			pthread_cond_wait (fifo->wrote, fifo->mut);
		}

		size = (readsize[i] - ((i==0)?0:readsize[i-1]));

		int result = in_stream_read (fifo->buf[fifo->headRG],1,size,&in_stream_cursor);
		if (result != size) {
		    printf ("Reading error1, read %d ",result);
		    exit (3);
		}

		fifo->compsize[fifo->headRG]=size;

		fifo->ledger[fifo->headRG]++;
		fifo->headRG++;
		if (fifo->headRG == NUMBUF)
			fifo->headRG = 0;

		pthread_mutex_unlock (fifo->mut);
		pthread_cond_signal (fifo->rcvd);


	}
	return (NULL);
}

void decompression(const void *in, size_t stream_size, size_t buffer_size, void *out, uint64_t *out_kernel_time_ms)
{
	// gettimeofday(&tall_start,0);
	// double alltime;

	int padding=0;
	in_stream = in;
	in_stream_size = stream_size;
	out_stream = out;
	bufsize = buffer_size;

    size_t in_stream_cursor = 0;
	in_stream_read (&numbufs,sizeof(unsigned int), 1,&in_stream_cursor);
	in_stream_read (&padding,sizeof(unsigned int), 1,&in_stream_cursor);

	printf("Num bufs %d padding size %d bufsize %d\n", numbufs,padding,bufsize);

	pthread_t rce;

	dequeue *fifo = dequeueInit (bufsize,numbufs,padding);
	if (fifo ==  NULL) {
		fprintf (stderr, "main: Queue Init failed.\n");
        abort();
	}

	//init compression threads
	init_decompression(fifo,out_stream);

	//create receiver
	pthread_create (&rce, NULL, receiver, fifo);
	//start producing
	//and start signaling as we go

	//join all
	join_decomp_threads();
	//join receiver
	pthread_join (rce, NULL);

	dequeueDelete (fifo);

	if (out_kernel_time_ms) {
        *out_kernel_time_ms = last_decompression_kernel_time_us();
	}
}




