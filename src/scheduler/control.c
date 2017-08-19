/**
*			Copyright (C) 2008-2015 HPDCS Group
*			http://www.dis.uniroma1.it/~hpdcs
*
*
* This file is part of ROOT-Sim (ROme OpTimistic Simulator).
*
* ROOT-Sim is free software; you can redistribute it and/or modify it under the
* terms of the GNU General Public License as published by the Free Software
* Foundation; either version 3 of the License, or (at your option) any later
* version.
*
* ROOT-Sim is distributed in the hope that it will be useful, but WITHOUT ANY
* WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
* A PARTICULAR PURPOSE. See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with
* ROOT-Sim; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*
* @file control.c
* @brief
* @author
*/


#include <stdbool.h>

#include <scheduler/scheduler.h>
#include <scheduler/process.h>
#include <core/core.h>
#include <communication/communication.h>
#include <mm/dymelor.h>
#include <datatypes/list.h>
#include <gvt/gvt.h>

#ifdef HAVE_CROSS_STATE
void unblock_synchronized_objects(unsigned int lid) {
	unsigned int i;
	msg_t control_msg;

	for(i = 1; i <= LPS[lid]->ECS_index; i++) {
		bzero(&control_msg, sizeof(msg_t));
		control_msg.sender = LidToGid(lid);
		control_msg.receiver = LidToGid(LPS[lid]->ECS_synch_table[i]);
		control_msg.type = RENDEZVOUS_UNBLOCK;
		control_msg.timestamp = lvt(lid);
		control_msg.send_time = lvt(lid);
		control_msg.message_kind = positive;
		control_msg.rendezvous_mark = LPS[lid]->wait_on_rendezvous;

		Send(&control_msg);
	}

	LPS[lid]->wait_on_rendezvous = 0;
	LPS[lid]->ECS_index = 0;
}
#endif


// Questa funzione serve a mandare in rollback qualcuno che mi
// aveva mandato un RENDEZVOUS_START. Viene invocata da rollback()
// e simtime è il tempo del bound, quindi il tempo dell'ultimo evento
// correttamente processato nella traiettoria speculativa.
// Ogni volta che ricevo un RENDEZVOUS_START da un altro processo
// copio il messaggio nella rendezvous_queue.
void rollback_control_message(unsigned int lid, simtime_t simtime) {
	msg_t control_antimessage;

	msg_t *msg, *msg_prev;

	if(list_empty(LPS[lid]->rendezvous_queue)) {
		return;
	}

	msg = list_tail(LPS[lid]->rendezvous_queue);
	while(msg != NULL && msg->timestamp > simtime) {

		// Control antimessage
		bzero(&control_antimessage, sizeof(msg_t));
		control_antimessage.type = RENDEZVOUS_ROLLBACK;
    control_antimessage.sender = msg->receiver;
    control_antimessage.receiver = msg->sender;
    control_antimessage.timestamp = msg->timestamp;
    control_antimessage.send_time = msg->send_time;
		control_antimessage.rendezvous_mark = msg->rendezvous_mark;
    control_antimessage.message_kind = control;

    Send(&control_antimessage);
    printf("Sending RENDEZVOUS_ROLLBACK for %llu STATE OF RECEIVER (%d) IS %llu, OF SENDER (%d) IS %llu\n",msg->rendezvous_mark,msg->sender,LPS[msg->sender]->state,msg->receiver,LPS[msg->receiver]->state);
		//LPS[msg->sender] = LP_STATE_READY;
    msg_prev = list_prev(msg);
		list_delete_by_content(lid, LPS[lid]->rendezvous_queue, msg);
		msg = msg_prev;
	}
}

// return false if the antimessage is recognized (and processed) as a control antimessage
bool anti_control_message(msg_t * msg) {

	#ifndef HAVE_CROSS_STATE
	(void)msg;
	#else
	msg_t *old_rendezvous ;

	if(msg->type == RENDEZVOUS_ROLLBACK) {

		unsigned int lid_receiver = msg->receiver;

		//Check if a relative message exists
		//TODO non serve andare indietro più del tempo di rendezvous_rollback (VERO!!! Ma in quel caso devo uscire dal ciclo con old_rendezvous == NULL per cadere nell'if successivo)
		old_rendezvous = list_tail(LPS[lid_receiver]->queue_in);
		while(old_rendezvous != NULL && old_rendezvous->rendezvous_mark != msg->rendezvous_mark) {
			old_rendezvous = list_prev(old_rendezvous);
		}

		if(old_rendezvous == NULL) {
			return false;
		}

		//If this event is in the past
		if(old_rendezvous->timestamp <= lvt(lid_receiver)) {

			//Set LP->bound to the message that caused ECS
			LPS[lid_receiver]->bound = list_prev(old_rendezvous);
      while (LPS[lid_receiver]->bound != NULL && LPS[lid_receiver]->bound->timestamp >= old_rendezvous->timestamp) {
//        if(list_prev(LPS[lid_receiver]->bound) == NULL) {
//          break;
//        }
				LPS[lid_receiver]->bound = list_prev(LPS[lid_receiver]->bound);
      }

      LPS[lid_receiver]->state = LP_STATE_ROLLBACK;
		}

		old_rendezvous->rendezvous_mark = 0;



		//Reset ECS information
		if(LPS[lid_receiver]->wait_on_rendezvous == msg->rendezvous_mark) {
			LPS[lid_receiver]->ECS_index = 0;
			LPS[lid_receiver]->wait_on_rendezvous = 0;
		}

		return false;
	}
	#endif

	return true;
}

// return true if the control message should be reprocessed during silent exceution
bool reprocess_control_msg(msg_t *msg) {


	if(msg->type < MIN_VALUE_CONTROL) {
		return true;
	}

	return false;
}


// return true if the event must not be filtered here
bool receive_control_msg(msg_t *msg) {

	if(msg->type < MIN_VALUE_CONTROL || msg->type > MAX_VALUE_CONTROL) {
		return true;
	}

	#ifdef  HAVE_CROSS_STATE
	switch(msg->type) {
//	  msg_t control_msg;

		case RENDEZVOUS_START:
			return true;

		case RENDEZVOUS_ACK:

			//printf("(%d) matched mark:%llu WOR:%llu state:%d from %d\n",msg->receiver, msg->rendezvous_mark,LPS[msg->receiver]->wait_on_rendezvous, LPS[msg->receiver]->state, msg->sender);

			if(	LPS[msg->receiver]->state == LP_STATE_ROLLBACK ||LPS[msg->receiver]->state == LP_STATE_SILENT_EXEC) {
			  printf("LP %d is rollbacking/silentexec and received an ACK for rendezvous %llu. Discarding...\n",msg->receiver,msg->rendezvous_mark);
        fflush(stdout);
        break;
			}
			if(LPS[msg->receiver]->wait_on_rendezvous == msg->rendezvous_mark) {
				LPS[msg->receiver]->state = LP_STATE_READY_FOR_SYNCH;
        printf("LP %d received ack from LP %d for rendezvous %llu\n",msg->receiver,msg->sender,msg->rendezvous_mark);
        fflush(stdout);
			}

			break;

		case RENDEZVOUS_UNBLOCK:

      printf("---------> %llu - LP %d unblocking after ECS\n", CLOCK_READ(), msg->receiver);
      fflush(stdout);

      if(	LPS[msg->receiver]->state == LP_STATE_ROLLBACK ||
				LPS[msg->receiver]->state == LP_STATE_SILENT_EXEC
			)  {
				printf("%llu - (%d) discarding mark %llu\n",CLOCK_READ(), msg->receiver, msg->rendezvous_mark);
        fflush(stdout);
				break;
			}

			if(LPS[msg->receiver]->wait_on_rendezvous == msg->rendezvous_mark) {

				//printf("%llu - (%d) matched mark %llu from %d\n",CLOCK_READ(), msg->receiver, msg->rendezvous_mark, msg->sender);
        printf("LP %d being unblocked by LP %d\n",msg->receiver,msg->sender);
        fflush(stdout);
				LPS[msg->receiver]->wait_on_rendezvous = 0;
        LPS[msg->receiver]->state = LP_STATE_READY;

			}
       else {
				printf("%llu - (%d) wait on rendezvous: %llu - mark: %llu - unblock from %d\n",CLOCK_READ(), msg->receiver, LPS[msg->receiver]->wait_on_rendezvous, msg->rendezvous_mark, msg->sender);
			}
        current_lp = GidToLid(msg->receiver);
        //current_lp = msg->receiver;
  			current_lvt = msg->timestamp;
			  force_LP_checkpoint(current_lp);
		  	LogState(current_lp);
	  		current_lvt = INFTY;
  			current_lp = IDLE_PROCESS;
        printf("%llu - Lp %d has taken a snapshot, after concluding ECS at time %f\n", CLOCK_READ(), msg->receiver, msg->timestamp);

        fflush(stdout);

				/* current_lp = msg->receiver; */
				/* current_lvt = msg->timestamp; */
				/* force_LP_checkpoint(current_lp); */

				/* LogState(current_lp); */

				/* current_lvt = INFTY; */
				/* current_lp = IDLE_PROCESS; */

			break;

		case RENDEZVOUS_ROLLBACK:
			return true;

		default:
			rootsim_error(true, "Trying to handle a control message which is meaningless at receive time: %d\n", msg->type);

	}
	#endif

	return false;
}


// return true if must be passed to the LP
bool process_control_msg(msg_t *msg) {

	#ifdef HAVE_CROSS_STATE
	msg_t control_msg;
	#endif

	if(msg->type < MIN_VALUE_CONTROL || msg->type > MAX_VALUE_CONTROL) {
		return true;
	}

	#ifdef HAVE_CROSS_STATE
	switch(msg->type) {

		case RENDEZVOUS_START:

			list_insert(msg->receiver, LPS[msg->receiver]->rendezvous_queue, timestamp, msg);
			// Place this into input queue
			LPS[msg->receiver]->wait_on_rendezvous = msg->rendezvous_mark;

			LPS[msg->receiver]->state = LP_STATE_WAIT_FOR_UNBLOCK;
			bzero(&control_msg, sizeof(msg_t));
			control_msg.sender = msg->receiver;
			control_msg.receiver = msg->sender;
			control_msg.type = RENDEZVOUS_ACK;
			control_msg.timestamp = msg->timestamp;
			control_msg.send_time = msg->timestamp;
			control_msg.message_kind = positive;
			control_msg.rendezvous_mark = msg->rendezvous_mark;
			Send(&control_msg);

      printf("LP %d is ACKing a rendezvous toward LP %d with mark %llu\n",control_msg.sender,control_msg.receiver,control_msg.rendezvous_mark);
      fflush(stdout);
			break;
/* //TODO MN DEBUG case RENDEZVOUS_ACK and UNBLOCK  was noted */
/* 		case RENDEZVOUS_ACK: */
/*       LPS[msg->receiver]->state = LP_STATE_READY_FOR_SYNCH; */
/* 			printf("WTF??\n"); */
/*       return true; */

/* 		case RENDEZVOUS_UNBLOCK: */
/*       LPS[msg->receiver]->state = LP_STATE_READY; */
/*       printf("WTF??\n"); */
/* 			break; */

		/* case NULL_LOG_MESSAGE: */
/* //			printf("[%d] process NULL_LOG_MESSAGE log-counter:%d \n",msg->receiver,GLPS[LPS[msg->receiver]->current_group]->counter_log); */
		/* 	break; */

		default:
			rootsim_error(true, "Trying to handle a control message which is meaningless at schedule time: %d\n", msg->type);

	}
	#endif

	return false;
}
