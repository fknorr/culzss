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

#ifndef CULZSS_H
#define CULZSS_H

//#define LOOP 100
//#define NUMBUF 4
//#define BUFSIZE	134217728
//134217728

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
	unsigned char ** buf;
	unsigned char ** bufout;
	
	unsigned char * in_d;	
	unsigned char * out_d;	
	
	int headPG;
	int headGC;
	int headSC;
	int headCS;
	int headSP;
	int * ledger;
	
	int * outsize;
	//int full, empty;
	pthread_mutex_t *mut;
	pthread_cond_t *produced, *compressed, *streamready, *sendready, *sent;
} queue;


//gpu functions
extern int  CULZSSp_compression_kernel_wrapper(unsigned char * in_host_buffer, int in_buffer_size,unsigned char * out_host_buffer, int compression_type, int wsize, int block_size, int nstreams, int stream_block_index,unsigned char * in_device_buffer,unsigned char * out_device_buffer);
extern int CULZSSp_aftercompression_wrapper(unsigned char * buffer, int buf_length, unsigned char * bufferout, int * comp_length);
extern unsigned char * CULZSSp_initGPUmem( int buf_length);
extern unsigned char * CULZSSp_initCPUmem( int buf_length);
extern void CULZSSp_deleteGPUmem(unsigned char * mem_d);
extern void CULZSSp_deleteCPUmem(unsigned char * mem_d);
extern void CULZSSp_initGPU();
extern void CULZSSp_resetGPU();
extern int CULZSSp_streams_in_GPU();
extern int CULZSSp_onestream_finish_GPU(int index);
extern void CULZSSp_deleteGPUStreams();
extern void CULZSSp_signalExitThreads();

//Queue functions
queue *CULZSSp_queueInit (int maxiterations,int numblocks,int blocksize);
void CULZSSp_queueDelete (queue *q);
void CULZSSp_queueAdd (queue *q, int in);

void CULZSSp_init_compression(queue *q,int maxiterations,int numblocks,int blocksize,void *out,unsigned int * book);
void CULZSSp_join_comp_threads();

int CULZSSp_getloopcount();

size_t CULZSSp_last_compressed_size();
uint64_t CULZSSp_last_compression_kernel_time_us();

#endif
