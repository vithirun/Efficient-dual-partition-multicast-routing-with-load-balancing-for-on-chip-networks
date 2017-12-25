/*
 * Controller.cpp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Lavina Jain
 *
 */

//////////////////////////////////////////////////////////////////////////////////////////////////
/// \file Controller.cpp
/// \brief Implements routing
//////////////////////////////////////////////////////////////////////////////////////////////////

#include "Controller.h"
#include "../config/extern.h"
#include "../config/default1.h"
#include "NoC.h"
#include "NWTile.h"
////////////////////////
/// Constructor
////////////////////////
ULL t_p;

template<UI num_ip>
Controller<num_ip>::Controller(sc_module_name Controller): sc_module(Controller)
{

    void *hndl;
    void *mkr;


    sim_count=0;   

    string libname = string("./router/lib/");

    switch (RT_ALGO)
    {
    case YX: 
        libname = libname + string("YX_router.so");
        break;
    case XY:
        libname = libname + string("XY_router.so");
        break;
    case FTXY:
        libname=libname+ string("ftxy.so");
        break;
    }
    hndl = dlopen(libname.c_str(), RTLD_NOW);
    if (hndl == NULL)
    {
        cerr << dlerror() << endl;
        exit(-1);
    }
    mkr = dlsym(hndl, "maker");
    rtable = ((router*(*)())(mkr))();

    SC_THREAD(allocate_route);
    for (UI i = 0; i < num_ip; i++)
        sensitive << rtRequest[i];
    /*added */
    // process sensitive to power credit info, updates power info corresponding to the neighbouring tiles
    SC_THREAD(update_power_credit);
    for (UI i = 0; i < num_ip-1; i++)
    {
        sensitive << power_credit_in[i];
    }
    SC_THREAD(send_power_info);	// Thread sensitive to clock
    sensitive_pos << switch_cntrl;

    /*end*/
    //for Q-router
    //For Q-Routing
    // initialize VC request to false
    //vcRequest.initialize(false);

    // initialize virtual channels and buffers
    /*	ctrQ.num_bufs = ESTBUF;
    	ctrQ.pntr = 0;
    	ctrQ.full = false;
    	ctrQ.empty = true;*/
    /*	check = 0;
    pQtop = 0;*/

//qrt************************************************************
    rs.est_out = 0;
    rs.est_buffer = 0;

    for (UI i = 0; i < num_ip - 1; i++)
    {
        r_in[i].free = true;
    }

    SC_THREAD(transmitEst);
    sensitive_pos << switch_cntrl;

    SC_THREAD(rcv_estimate);
    for (UI i = 0; i < num_ip-1; i++)
        sensitive << estIn[i];

    rtable->num_nb = num_ip-1;
    //end for Q-ROuter
//qrt************************************************************
    /*added */
    for (int i=0; i < num_ip-1 ; i++)
    {
        powerCreditLine t;
        t.Power = 0;
        power_credit_out[i].initialize(t);
    }
    /*end*/
}

//added
///////////////////////////////////////////////////////////////////////////
//Process Sesitive to clock
//////////////////////////////////////////////////////////////////////////

template<UI num_ip>
void Controller<num_ip>::send_power_info()
{
    while (true)
    {
        wait();	// wait for the next clk cycle
        if (switch_cntrl.event())
        {
            powerCreditLine t;
            t.Power=P;
            for (int i=0; i<num_ip-1; i++)
                power_credit_out[i].write(t);
        }
    }
}


///////////////////////////////////////////////////////////////////////////
/// Process sensitive to incoming power credit information.
/// updates credit info (power values) of neighbor tiles
///////////////////////////////////////////////////////////////////////////
/*added*/
template<UI num_ip>
void Controller<num_ip>::update_power_credit()
{
    for (int i=0; i<4; i++)
    {
        rtable->power[i]=0;
    }
    while (true)
    {
        wait();	// wait until change in credit info
        powerCreditLine t;
        for (UI i = 0; i < num_ip-1; i++)
        {
            if (power_credit_in[i].event())
            {

                Pnb[i] = power_credit_in[i].read().Power;	// update credit

                UI dir=idToDir(i);
                rtable->update_power(dir,Pnb[i]);
            }
        }

    }//end while
}
//end

///////////////////////////////////////////////////////////////////////////
//Process Sesitive to clock
//////////////////////////////////////////////////////////////////////////
template<UI num_ip>
void Controller<num_ip>::transmitEst()
{

    int sim = 0;
    while (sim_count <SIM_NUM)
    {
        flit flit_out;
        wait(); //wait for clock
        //sim++;
        if (switch_cntrl.event() )  //&& (sim % 2)
        {
            sim_count++;
            /*	//		late();
            		//	cout<<this->name()<<":(trans) "<<sim_count<<endl;
            			if(ctrQ.empty == false) { //enter only if there is a estimate packet in the Q
            				flit_out = ctrQ.flit_out();  //get the packet
            				UI i;
            				switch(TOPO) { // decide the direction

            					case TORUS:
            						//#ifdef TORUS
            						i = flit_out.pkthdr.esthdr.dir;
            						//#endif
            						break;

            					case MESH:
            						//#ifdef MESH
            						UI dir = flit_out.pkthdr.esthdr.dir;
            						switch(dir) {
            							case N: i = portN;
            								break;
            							case S: i = portS;
            								break;
            							case E: i = portE;
            								break;
            							case W: i = portW;
            								break;
            							case C: i = num_ip - 1;
            								break;
            						}
            						//#endif
            						break;
            				}


            				if(ocReady_in[i].read() == true) {	// OC ready to recieve flit

            					if(LOG >= 4)
            					eventlog<<"\ntime: "<<sc_time_stamp()<<" name: "<<this->name()<<" Ctr (QRT): Attempting to forward  estimate flit: "<<flit_out;

            					// VC request
            					if(LOG >= 4)
            						eventlog<<"\ntime: "<<sc_time_stamp()<<" name: "<<this->name()<<" tile: "<<tileID <<"Controller Requesting VC for dir: "<<i;

            					vcRequest.write(true);
            					opRequest.write(i);
            					wait();	// wait for ready event from VC


            					if(vcReady.event()) {
            						if(LOG >= 4)
            							eventlog<<"\ntime: "<<sc_time_stamp()<<" name: "<<this->name()<<" Ctr: vcReady event..."<<endl;
            					}
            					else if(switch_cntrl.event()) {
            						sim_count++;
                                                        //    late();
            						if(LOG >= 4)
            							eventlog<<"\ntime: "<<sc_time_stamp()<<" name: "<<this->name()<<" Ctr: unknown clock event..."<<endl;
            					}
            					// read next VCid sent by VC
            					UI vc_next_id = nextVCID.read();

            					if(vc_next_id == NUM_VCS+1) {	// VC not granted
            						if(LOG >= 4)
            							eventlog<<"\ntime: "<<sc_time_stamp()<<" name: "<<this->name()<<" Ctr: No free next vc, pushing flit in Q" <<endl;
            						// push flit back in fifo
            						flit_out.simdata.num_waits++;
            						if(flit_out.simdata.num_waits < WAITS)
            							ctrQ.flit_push(flit_out);
            						vcRequest.write(false);
            						continue;
            					}
            						// write flit to output port
            					flit_out.simdata.num_sw++;
            					flit_out.simdata.ctime = sc_time_stamp();
            					flit_out.vcid = vc_next_id;

            					ocPort_out[i].write(flit_out);

            					if(LOG >= 2)
            						eventlog<<"\ntime: "<<sc_time_stamp()<<" name: "<<this->name()<<" tile: "<<tileID<<"Controller says  Transmitting estimate flit to output port: "<<i<<flit_out;


            					vcRequest.write(false);
            				} // end outReady == true
            				else {
            					if(LOG >= 4)
            						eventlog<<"\ntime: "<<sc_time_stamp()<<" name: "<<this->name()<<" Ctr: OC cannot accept flit!"<<endl;
            						flit_out.simdata.num_waits++;
            					//	if(flit_out.simdata.num_waits < WAITS)
            							ctrQ.flit_push(flit_out);
            				}
            			}  //end of if Q not empty
            			rs.est_buffer = ctrQ.pntr;*/
        } // end of if switch_cntrl
    }// end of while
} // end transmitest



///////////////////////////////////////////////////////////////////////////
//Process Sesitive to Estimate Inport
//////////////////////////////////////////////////////////////////////////
template<UI num_ip>
void Controller<num_ip>::rcv_estimate()
{
    while (true)
    {
        wait();
        //cout<<"Inpor Event!!"<<endl;
        for (UI i = 0; i < num_ip-1; i++)
        {
            if (estIn[i].event() )//&& r_in[i].free)
            {
                rs.est_buffer++;
                r_in[i].val = estIn[i].read();
                r_in[i].free = false;
                UI dest = r_in[i].val.pkthdr.esthdr.d ;
                UI est = r_in[i].val.pkthdr.esthdr.estimate;
                //		cout<<"Est received: for "<<r_in[i].val.pkthdr.esthdr.d<<" destination and value "<< r_in[i].val.pkthdr.esthdr.estimate<<endl;
                rtable->update_estimate(i, dest , est ,0);
                r_in[i].free = true;
            }
        }
    }
}



///////////////////////////////////////////////////////////////////////////
/// Process sensitive to route request
/// Calls routing algorithm
///////////////////////////////////////////////////////////////////////////


//mycode
template<UI num_ip>
void Controller<num_ip>::allocate_route()
{
    int flag=0;
	UI op_dir;
	int count=0,dup_length;
	sc_uint<ADDR_SIZE> src;
	int srcx,srcy,destx,desty;
	int NO[10],SO[10],EA[10],WE[10],NE[10],SE[10],NW[10],SW[10];
	int p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0;
	int NO_f=0,SO_f=0,EA_f=0,WE_f=0,NE_f=0,SE_f=0,NW_f=0,SW_f=0;
    while (true)
    {   
	wait();
	if (rtRequest[0].event() && rtRequest[0].read() == ROUTE)
	{
		src = sourceAddress[0].read();
	if(length>0)
	{
		for(int i=0;i<length;i++)
		{
			cout<<"dest: 	"<<d1[i]<<endl;
			srcx = src / num_cols;
    			srcy = src % num_cols;
    			destx = d1[i] / num_cols;
    			desty = d1[i] % num_cols;
//************************************DUAL PARTITIONING******************************************************************
			if(srcx<destx)
			{
				if(srcy<desty)
				{
					SE[u++]=d1[i];
					SE_f++;
				}
				else if(srcy>desty)
				{
					SW[w++]=d1[i];
					SW_f++;
				}
				else
				{
					SO[q++]=d1[i];
					SO_f++;
				}
			}
			else if(srcx>destx)
			{
				if(srcy<desty)
				{
					NE[t++]=d1[i];
					NE_f++;
				}
				else if(srcy>desty)
				{
					NW[v++]=d1[i];
					NW_f++;
				}
				else
				{
					NO[p++]=d1[i];
					NO_f++;
				}
			}
			else if(srcx==destx)
			{
				if(srcy<desty)
				{
					EA[r++]=d1[i];
					EA_f++;	
				}
				else
				{
					WE[s++]=d1[i];
					WE_f++;
				}
			}
		}
		dup_length=length;
		length=0;
		
		cout<<"North :";
	for(int i=0;i<p;i++)
		cout<<" "<<NO[i];
	cout<<endl;
	cout<<"South :";
	for(int i=0;i<q;i++)
		cout<<" "<<SO[i];
	cout<<endl;
	cout<<"East :";
	for(int i=0;i<r;i++)
		cout<<" "<<EA[i];
	cout<<endl;
	cout<<"West :";
	for(int i=0;i<s;i++)
		cout<<" "<<WE[i];
	cout<<endl;
	cout<<"North East :";
	for(int i=0;i<t;i++)
		cout<<" "<<NE[i];
	cout<<endl;
	cout<<"South East :";
	for(int i=0;i<u;i++)
		cout<<" "<<SE[i];
	cout<<endl;
	cout<<"North West:";
	for(int i=0;i<v;i++)
		cout<<" "<<NW[i];
	cout<<endl;
	cout<<"South West :";
	for(int i=0;i<w;i++)
		cout<<" "<<SW[i];
	cout<<endl;
	//	cout<<"Flags:"<<NO_f<<SO_f<<EA_f<<WE_f<<NE_f<<NW_f<<SE_f<<SW_f;	
	}
	}
	if (LOG >= 4)
            eventlog<<this->name()<<" Says ";
        
	for (UI i = 0; i <num_ip; i++)
        {
            if (LOG >= 4)
                eventlog<<" i = "<<i;
            if (rtRequest[i].event() && rtRequest[i].read() == ROUTE)
            {

                sc_uint<ADDR_SIZE> src = sourceAddress[i].read();
                sc_uint<ADDR_SIZE> dest = destRequest[i].read();
                sc_uint<64> timestamp = timestamp_ip[i].read();

                UI ip_dir = idToDir(i);
                UI temp = 0;
                    temp = ip_dir;
		if(count<=num_tiles/2)
		{
			count++;
// NORTH_LAST ROUTING WITH LOAD BALANCING
		if(NE_f)
		{
			op_dir=E;
			NE_f--;
		}
		else if(NW_f)
		{
			op_dir=W;
			NW_f--;
		}
		else if(SE_f)
		{
			SE_f--;
			if((!NE_f && !EA_f) && (SO_f || SW_f))
				op_dir=S;
			else
				op_dir=E;
		}
		else if(SW_f)
		{
			SW_f--;
			if((!NW_f && !WE_f) && (SO_f || SE_f))
				op_dir=S;
			else
				op_dir=W;
		}
		else
		{
			op_dir = rtable->calc_next_ftxy(temp, src, dest);
		}
                }
		else if(count>num_tiles/2)
		{
			count++;
			if(count > num_tiles )
				count=0;
// WEST_LAST ROUTING WITH LOAD BALANCING
			if(NW_f)
			{
				op_dir=N;
				NW_f--;
			}
			else if(SW_f)
			{
				op_dir=S;
				SW_f--;
			}
			else if(NE_f)
			{
				NE_f--;
				if((!NW_f && !NO_f) && (SE_f || EA_f))
					op_dir=E;
				else
					op_dir=N;
			}
			else if(SE_f)
			{
				SE_f--;
				if((!SW_f && !SO_f) && (NE_f || EA_f))
					op_dir=E;
				else
					op_dir=S;
			}		
			else	
				op_dir = rtable->calc_next_ftyx(temp, src, dest);
		}
		
#define iprint(value)	" " #value "= "<<value

		if(op_dir==5)
		{
			break;
		}
                if (LOG == 10)
                    cout<<this->name()<<iprint(num_tiles)<<": "<<iprint(temp)<<iprint(ip_dir)<<iprint(op_dir)<<iprint(dest)<<iprint(src)<<iprint(i)<<iprint(timestamp)<<endl;
                rtReady[i].write(true);
                nextRt[i].write(op_dir);

            }
            if (rtRequest[i].event() && rtRequest[i].read() == UPDATE)
            {
                sc_uint<ADDR_SIZE> src = sourceAddress[i].read();
                sc_uint<ADDR_SIZE> dest = destRequest[i].read();
                rtReady[i].write(true);
            }
            if (rtRequest[i].event() && rtRequest[i].read() == NONE)
            {
                rtReady[i].write(false);
            }
        }
        if (LOG >= 4)
            eventlog<<endl;
	//wait();
    }// end while
}// end allocate_route

/*template<UI num_ip>
void Controller<num_ip>::timeEst(UI timestamp, UI dir,UI src,UI pktid, UI dest){
	if(pQtop < 100){
	//	cout<< "inside timeEst "<<endl;
		pQ[pQtop].estid = pktid;
		pQ[pQtop].timestamp = timestamp;
		pQ[pQtop].dir = dir;
		pQ[pQtop].dest = dest;
		pQ[pQtop].src = src;
		pQtop++;
	}
}*/


/*template<UI num_ip>
void Controller<num_ip>::late() {
	int i = 0;
	while(i < pQtop)
	{
		if(pQ[i].timestamp < sim_count)
			i++;
		else
			break;
	}
	//pQtop = pQtop - i;
        cout<<"Timereceived = "<<sim_count<<endl;
	for(int j = 0;j < i;j++)
	{
		int dir = pQ[j].dir;
		int dest = pQ[j].dest;
		rtable->update_estimate(dir,dest,penalty,0);
//		cout<<"src = "<<pQ[j].src<<"estid = "<<pQ[j].estid<<"sim_count = "<<sim_count<<endl;
		if(WAITS == 1001)
                    cout<<"     p added: Pktid = "<<pQ[j].estid<<" eT = "<<pQ[j].timestamp<<" amt: "<<penalty<<endl;
	}
	for(int k=0,j = i;j < pQtop;k++,j++){
		pQ[k].estid = pQ[j].estid;
		pQ[k].timestamp = pQ[j].timestamp;
		pQ[k].dir = pQ[j].dir;
		pQ[k].dest = pQ[j].dest;
		pQ[k].src = pQ[j].src;
	}
	pQtop -= i;
//}

template<UI num_ip>
void Controller<num_ip>::updateInfo(UI src,UI pktid) {
	int i = 0;
	while(i < pQtop){
		if((pQ[i].estid == pktid && pQ[i].src == src) )//|| (pktid % 2)
                {
                        if(WAITS == 1001)
                            cout<<"Pktid = "<<pktid<<" TR = "<<sim_count<<" eT = "<<pQ[i].timestamp<<endl;
			break;
                }
		else
			i++;
	}
        if(i == pQtop){
                cout << "Est pkt arrived, Info not avilable , pktID :"<<pktid<<" Src:"<<src<<endl;
		return ;
        }
	while(i < pQtop-1 ){
		pQ[i].estid = pQ[i+1].estid;
		pQ[i].timestamp = pQ[i].timestamp;
		pQ[i].dir = pQ[i+1].dir;
		pQ[i].dest = pQ[i+1].dest;
		pQ[i].src = pQ[i+1].src;
		i++;
	}
	pQtop--;
}*/

//template<UI num_ip>
//flit* Controller<num_ip>::create_est_flit(UI estimate, UI dirtoRoute, UI src, UI d, UI i) {




//qrt************************************************************
template<UI num_ip>
void Controller<num_ip>::create_est_flit(UI estimate,UI d, flit *est_out )   //CODEFLIT
{

//	flit_out->src = tileID;//CODENR
//	flit_out->pkthdr.esthdr.dir = dirtoRoute ;//CODENR

//	flit_out->pkthdr.esthdr.pktID = est_pktID[i].read();//CODEPEN
//	flit_out->pkthdr.esthdr.src = src;//CODEPEN

    est_out->simdata.gtime = sc_time_stamp(); //CODENR
    /*	flit_out->simdata.ctime = sc_time_stamp();//CODENR
    	flit_out->simdata.atime = SC_ZERO_TIME;//CODENR
    	flit_out->simdata.atimestamp = 0;//CODENR
    	flit_out->simdata.num_waits = 0;//CODENR
    	flit_out->simdata.num_sw = 0;*///CODENR
    est_out->pkthdr.esthdr.d = d;
    est_out->pkthdr.esthdr.estimate = estimate;
}



/*template<UI num_ip>
void Controller<num_ip>::store_flit(flit *flit_in) {
	if(ctrQ.full == true) {
		if(LOG >= 2)
			eventlog<<"\ntime: "<<sc_time_stamp()<<" name: "<<this->name()<<" Error(QRT): DATA has arrived. ctrQ is full!"<<endl;
                cout<<"Controller Est Dropped Queue Full: estpkt id :"<<flit_in->pkthdr.esthdr.pktID <<"Src :"<<flit_in->pkthdr.esthdr.src<<endl;
        }
	else
	{
		ctrQ.flit_in(*flit_in);
		rs.est_out++;
	}
}*/


//qrt************************************************************

template<UI num_ip>
UI Controller<num_ip>::get_avgest()
{
    ULL total_est = 0;
    int i;
    for (i = 0; i < rtable->num_tiles; i++)
    {
        total_est += rtable->get_estimate(i);
    }

    return (double)total_est/i;

}


///Returns the reverse direction
template<UI num_ip>
UI Controller<num_ip>::reverseDir(UI dir)
{
    if (dir == N)
        return S;
    else if (dir == S)
        return N;
    else if (dir == E)
        return W;
    else if (dir == W)
        return E;
}

//qrt************************************************************




///////////////////////////////////////////////////////////////////////////
/// Method to assign tile IDs and port IDs
///////////////////////////////////////////////////////////////////////////
template<UI num_ip>
void Controller<num_ip>::setTileID(UI id, UI port_N, UI port_S, UI port_E, UI port_W)
{
    tileID = id;
    portN = port_N;
    portS = port_S;
    portE = port_E;
    portW = port_W;
    rtable->setID(id);
}


/////////////////////////////////////////////////////////////////////
/// Returns direction (N, S, E, W) corresponding to a given port id
////////////////////////////////////////////////////////////////////
template<UI num_ip>
UI Controller<num_ip>::idToDir(UI port_id)
{
    if (port_id == portN)
        return N;
    else if (port_id == portS)
        return S;
    else if (port_id == portE)
        return E;
    else if (port_id == portW)
        return W;
    return C;
}

/////////////////////////////////////////////////////////////////////
/// Returns id corresponding to a given port direction (N, S, E, W)
////////////////////////////////////////////////////////////////////
template<UI num_ip>
UI Controller<num_ip>::dirToId(UI port_dir)
{
    switch (port_dir)
    {
    case N:
        return portN;
        break;
    case S:
        return portS;
        break;
    case E:
        return portE;
        break;
    case W:
        return portW;
        break;
    case C:
        return num_ip - 1;
        break;
    }
    return num_ip - 1;
}

template struct Controller<NUM_IC>;
template struct Controller<NUM_IC_B>;
template struct Controller<NUM_IC_C>;
