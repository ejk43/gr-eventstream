/* -*- c++ -*- */
/*
 * Copyright 2011 Free Software Foundation, Inc.
 * 
 * This file is part of gr-eventstream
 * 
 * gr-eventstream is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * gr-eventstream is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with gr-eventstream; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/*
 * config.h is generated by configure.  It contains the results
 * of probing for features, options etc.  It should be the first
 * file included in your .cc file.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <es/es_source.h>
#include <es/es_common.h>
#include <es/es.h>
#include <gr_io_signature.h>
#include <stdio.h>
#include <string.h>

//#define DEBUG(x) x
#define DEBUG(x)

/*
 * Create a new instance of es_source and return
 * a boost shared_ptr.  This is effectively the public constructor.
 */
es_source_sptr 
es_make_source (pmt_t arb, es_queue_sptr queue, gr_vector_int out_sig)
{
  return es_source_sptr (new es_source (arb,queue,out_sig));
}

/*
 * Specify constraints on number of input and output streams.
 * This info is used to construct the input and output signatures
 * (2nd & 3rd args to gr_block's constructor).  The input and
 * output signatures are used by the runtime system to
 * check that a valid number and type of inputs and outputs
 * are connected to this block.  In this case, we accept
 * only 1 input and 1 output.
 */
static const int MIN_IN = 0;	// mininum number of input streams
static const int MAX_IN = 0;	// maximum number of input streams

unsigned long long es_source::time(){
    return d_time;
}

/*
 * The private constructor
 */
es_source::es_source (pmt_t _arb, es_queue_sptr _queue, gr_vector_int out_sig)
  : gr_sync_block ("es_source",
    gr_make_io_signature (MIN_IN, MAX_IN, 0),
    es_make_io_signature (out_sig.size(), out_sig) ),
    event_queue(_queue), 
    arb(_arb),
    d_maxlen(ULLONG_MAX)
{
    // register native event types
    event_queue->register_event_type( es::event_type_gen_vector );
    event_queue->register_event_type( es::event_type_gen_vector_f );
    event_queue->register_event_type( es::event_type_gen_vector_c );

    // bind a handler for hold over events
//    pmt_t handler = make_handler_pmt( new es_handler_insert_vector() );
    es_handler_sptr handler = es_make_handler_insert_vector();

    // bind handlers for native event types
    event_queue->bind_handler( es::event_type_gen_vector, handler );
    event_queue->bind_handler( es::event_type_gen_vector_f, handler );
    event_queue->bind_handler( es::event_type_gen_vector_c, handler );

    d_time = 0;
}

// set a maximum number of items to produce (otherwise we will run forever and never mark finished)
void es_source::set_max(unsigned long long maxlen){
    d_maxlen = maxlen;
}

/*
 * Our virtual destructor.
 */
es_source::~es_source ()
{
}

int 
es_source::work (int noutput_items,
			gr_vector_const_void_star &input_items,
			gr_vector_void_star &output_items)
{
  char *out = (char *) output_items[0];
   
  unsigned long long max_time = d_time + noutput_items;
  unsigned long long min_time = d_time;


//  printf("es_source::work()\n");

  es_eh_pair* eh = NULL;
  
  // zero our output buffers before doing anything to them
  //memset( out, 0x00, noutput_items*itemsize );
  for(int i=0; i<output_items.size(); i++ ){
    memset( output_items[i], 0x00, noutput_items*d_output_signature->sizeof_stream_item(i) );
  }

  while( event_queue->fetch_next_event2( min_time, max_time, &eh ) ){

    //printf("es_source::got event back -- \n");
    //event_print(eh->event);

    if(eh == NULL){ 
        printf("eh==NULL==%x!\n",eh); 
        assert(0); 
    }

    int buffer_offset = eh->time() - d_time;

    assert( pmt_is_msg_accepter( eh->handler ) );
    
/* 
    DEBUG(printf("    assert( pmt_is_msg_accepter( eh_pair_handler( eh ) ) ) --- ok \n");)
    DEBUG(printf("event_length = %lld\n", eh->length());)
    DEBUG(printf("buffer_offset = %d\n", buffer_offset);)
    DEBUG(printf("buffer_max_needed = %lld\n", eh->length()+buffer_offset);)
    DEBUG(printf("buffer_max_len = %d\n", noutput_items);)
  */  
    int buffer_event_max = eh->length()+buffer_offset;
    bool use_inplace_buffer = true;

    // If the event's max length will not fit in the buffer we will need to create a temporary one.
    if(buffer_event_max >= noutput_items){
        use_inplace_buffer = false;
    }
    
    // Call the event handler with either an in-place or out-of-place buffer   
    if(use_inplace_buffer){   
        DEBUG(printf("using in place buffer!!!\n");)
        gr_vector_void_star event_bufs;
        for(int i=0; i<output_items.size(); i++ ){
            void* bufloc = (void*) ((uint64_t)output_items[i] + (d_output_signature->sizeof_stream_item(i) * buffer_offset));
            event_bufs.push_back( bufloc );
        }

        pmt_t event = register_buffer( eh->event, event_bufs );
        DEBUG(event_print( event );)

        eh->event = event;
        // run the event handler
        eh->run();      

    } else {    // use_inplace_buffer = false

        // allocate temporary buffers.
        gr_vector_void_star bufs;
        for(int i=0; i<output_items.size(); i++ ){
            int itemsize = d_output_signature->sizeof_stream_item(i);
            int bufsize = itemsize * eh->length();
            bufs.push_back( (void*) new uint8_t[bufsize ] );
            DEBUG(printf("allocced buffer of size %d\n", bufsize );)
            }

        DEBUG(printf("using out of place buffer!!!\n");)
        
        // register buffers with eh pair and post 
        pmt_t event = register_buffer( eh->event, bufs);
        eh->event = event;
        DEBUG(event_print( event );)

        DEBUG(printf("run() with out of place buffer firing.\n");)

        // run the event handler
        eh->run();

        DEBUG(printf("run returned.\n");)

        // divide output buffer into partitions
        int usable_items = noutput_items - buffer_offset;
        int leftover_items = eh->length() - usable_items;

        // copy first partition to output
        for(int i=0; i<output_items.size(); i++){
//            printf("es.source producing %d items on stream %d\n", usable_items, i );
            memcpy( (void*)((uint64_t)output_items[i] + (buffer_offset * d_output_signature->sizeof_stream_item(i) )),
                    bufs[i],
                    usable_items * d_output_signature->sizeof_stream_item(i));
        }

        // AAA update this 
        // copy second partition to new insertion event and add to queue
        pmt_t buf1 = pmt_init_u8vector( d_output_signature->sizeof_stream_item(0) * leftover_items,  ((const uint8_t*) bufs[0]) + usable_items*d_output_signature->sizeof_stream_item(0) );
        pmt_t leftover_buf_pmt = pmt_list1( buf1 );

        for(int i=1; i<output_items.size(); i++){
            pmt_t buf_n = pmt_init_u8vector( d_output_signature->sizeof_stream_item(i) * leftover_items,  ((const uint8_t*) bufs[i]) + usable_items*d_output_signature->sizeof_stream_item(i) );
            leftover_buf_pmt = pmt_list_add(leftover_buf_pmt, buf_n);
        }

        DEBUG(printf("spawning holdover event ***\n");)
        //pmt_t new_event = event_create_gen_vector(max_time, leftover_buf_pmt, d_output_signature);
        pmt_t new_event = event_create_gen_vector(eh->time() + usable_items, leftover_buf_pmt, d_output_signature);
        event_queue->add_event(new_event);


        // get rid of temporary buffers.
        for(int i=0; i<output_items.size(); i++ ){
            delete bufs[i];
        }

    }

  }

  // Tell runtime system how many output items we produced.
  noutput_items = (unsigned int) std::min((unsigned long long)noutput_items, d_maxlen - d_time);
  d_time += noutput_items;
  DEBUG(if(noutput_items == 0){ printf("es_source::work() d_time = %llu returning 0 in source block.\n", d_time); })
  return noutput_items;
}
