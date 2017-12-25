#include "App_send.h"
#include "../../config/extern.h"
#include "../../config/default1.h"
#include <stdio.h>
App_send::App_send(sc_module_name App_send): ipcore(App_send)
{
}

void App_send::send()
{
    flit *flit_out;	
    string data;
	int i=0;
    wait(WARMUP);	   
   string appln_filename = string("config/application.config");
    ifstream fil1;  
    fil1.open(appln_filename.c_str());
    if (!fil1.is_open())
    {
        cout<<"application.config does not exist"<<endl;
    }
    else
    {
        cout <<"Loading parameters from application.config file...";
        while (!fil1.eof())
        {
            string name;
	    int value;
	    
            fil1 >> value ;
		fil1 >> name;
            if (name=="App_recv.so")
            {
                d1[i++]=value;
            }

	}
    }
	length=i;
	cout<<"length_send"<<length<<endl;
for(int j=0;j<i;j++)
{
	for(int k=0;k<packets;k++)
	{	
    flit_out = create_head_flit(0,0,d1[j]);   
      
    flit_outport.write(*flit_out);
    
    cout<<"\ntime: "<<sc_time_stamp()<<" name: "<<this->name()<<" tile: "<<tileID<<" Generating flit at core "<<*flit_out;
        

   wait(100);	
    flit_out = create_data_flit(0,1);	
    flit_outport.write(*flit_out);	
    

    cout<<"\ntime: "<<sc_time_stamp()<<" name: "<<this->name()<<" tile: "<<tileID<<" Generating flit at core "<<*flit_out;
    
    wait(100);	
    flit_out = create_tail_flit(0,2);	
	flit_outport.write(*flit_out); 
cout<<"\ntime: "<<sc_time_stamp()<<" name: "<<this->name()<<" tile: "<<tileID<<" Generating flit at core "<<*flit_out;
    	wait(100);
}  
}
}

void App_send::recv()
{
 
}

void App_send::processing(flit newflit)
{

}

extern "C"
{
    ipcore *maker()
    {
        return new App_send("App_send");
    }
}
