#pragma once
#include <stdbool.h>
#include <ROOT-Sim.h>

#define REGION_IN 1
#define REGION_OUT 2
#define UPDATE_NEIGHBOURS 3
#define TIME_STEP 5.0
#define EXECUTION_TIME 10000000 //dummy: it can be modified with a specific parameter 

#ifndef NUM__OCCUPIED_CELLS
    #define NUM_OCCUPIED_CELLS 1  //default number of occupied cells (i.e.: number of bugs)
#endif
#ifndef BUG_PER_CELL
    #define BUG_PER_CELL 1 //at most one bug per cell at a time
#endif


typedef struct _event_content_type { 
	    unsigned int cell; //cell's ID
	    int present; //was a bug inside this cell?
	   	unsigned int bug_size;	
} event_content_type; 
 
typedef struct _lp_state_type{ 
	    int present; //number of bugs actually present in this cell 
		int neighbour_bugs[4]; //each entry memorizes the number of bugs of the surrounding cells (i.e.: N,S,O,W cells)
		simtime_t lvt; //the lvt of this cell
		unsigned int explored; //was this cell already explored?
		unsigned int bug_size; //how big is the bug?
} lp_state_type;
