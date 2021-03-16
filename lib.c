/***************************************************************************
 *  Lempel, Ziv, Storer, and Szymanski Encoding and Decoding on CUDA
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
#include <stdlib.h>
#include "culzss.h"
#include <string.h>
#include <assert.h>
#include "decompression.h"
#include "deculzss.h"

#define BUFSIZE 1048576//65536 //1MB size for buffers

static queue *fifo;
static int maxiters=0;
static int padding=0;
static int numbls=0;
static int blsize=0;
static int in_stream_size=0;
static unsigned int * bookkeeping;


static const void *in_stream;

static size_t in_stream_read(void *buffer, size_t size, size_t items, size_t *cursor) {
    assert(*cursor <= in_stream_size);
    size_t remaining_items = (in_stream_size - *cursor) / size;
    size_t read = items < remaining_items ? items : remaining_items;
    memcpy(buffer, (const char*) in_stream + *cursor, read * size);
    *cursor += read * size;
    return read;
}


static void *producer (void *q)
{
	int i;

	size_t in_stream_cursor = 0;

	for (i = 0; i < maxiters; i++) {

		pthread_mutex_lock (fifo->mut);
		while (fifo->ledger[fifo->headPG]!=0) {
			//printf ("producer: queue FULL.\n");
			pthread_cond_wait (fifo->sent, fifo->mut);
		}

		int result = in_stream_read (fifo->buf[fifo->headPG],1,blsize,&in_stream_cursor);
		if (result != blsize )
		{
			if(i!=maxiters-1)
			{
				printf ("Reading error1, expected size %d, read size %d ", blsize,result);
                abort();
			}
		}

		fifo->ledger[fifo->headPG]++;
		fifo->headPG++;
		if (fifo->headPG == numbls)
			fifo->headPG = 0;

		pthread_mutex_unlock (fifo->mut);
		pthread_cond_signal (fifo->produced);
	}

	return (NULL);
}


size_t CULZSS_decompress(const void *in, size_t in_size, void *out, uint64_t *kernel_time_us) {
    CULZSSp_decompression(in,in_size,BUFSIZE, out);
    if (kernel_time_us) {
        *kernel_time_us = CULZSSp_last_decompression_kernel_time_us();
    }
    return CULZSSp_last_decompressed_size();
}


size_t CULZSS_compress(const void *in, size_t in_size, void *out, uint64_t *kernel_time_us) {
    in_stream = in;
    in_stream_size = in_size;

    if (in_stream_size < BUFSIZE) {
        // too small to benefit from GPU
        return in_size;
    } else {
        maxiters = in_stream_size / BUFSIZE + (((in_stream_size % BUFSIZE) > 0) ? 1 : 0);
        padding = in_stream_size % BUFSIZE;
        padding = (padding) ? (BUFSIZE - padding) : 0;
        blsize = BUFSIZE;
    }
    // printf(" eachblock_size:%d num_of_blocks:%d padding:%d \n", blsize, maxiters, padding);

    bookkeeping = (unsigned int *) malloc(
        sizeof(int) * (maxiters + 2)); //# of blocks, each block size, padding size
    bookkeeping[0] = maxiters;
    bookkeeping[1] = padding;

    pthread_t pro;

    fifo = CULZSSp_queueInit(maxiters, numbls, blsize);
    if (fifo == NULL) {
        fprintf(stderr, "Queue Init failed.\n");
        abort();
    }

    //init compression threads
    CULZSSp_init_compression(fifo, maxiters, numbls, blsize, out, bookkeeping);

    //create producer
    pthread_create(&pro, NULL, producer, fifo);

    //join all
    CULZSSp_join_comp_threads();
    //join producer
    pthread_join(pro, NULL);
    CULZSSp_queueDelete(fifo);

    free(bookkeeping);

    if (kernel_time_us) {
        *kernel_time_us = CULZSSp_last_compression_kernel_time_us();
    }
    return CULZSSp_last_compressed_size();
}

