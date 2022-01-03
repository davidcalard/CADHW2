#include <errno.h>
#include <signal.h>
#include <sstream>
#include <fstream>
#include <queue>
#include <algorithm>
#include "hcm.h"
#include "flat.h"
#include "hcmvcd.h"
#include "hcmsigvec.h"

using namespace std;

bool verbose = false;

///////////////////helper function declarations////////////////////////////
void ff_event(vector<hcmNode*>& , hcmInstance* cur_inst, bool f);
void gate_eval(vector<hcmNode*> &events, hcmInstance* cur_inst, bool (*op)(bool x1,bool x2), bool n);

/*
Defininig gate functions
*/
bool and_g(bool x1, bool x2){
	return x1 && x2; 
}
bool or_g(bool x1, bool x2){
	return x1 || x2; 
}
bool xor_g(bool x1, bool x2){
	return x1 ^ x2; 
}
bool not_g(bool x1, bool x2){
	return !x2; // we want to invert our in_state
}
bool buffer_g(bool x1,  bool x2){
	return x2; 
}

/*
AFFECTED GATES
Given nodes in event queue finds hcmInstances or gates that are influenced by it 
We simulate the new EVENTS on these gates 
*/
void affected_gates(vector<hcmNode*> &events,vector<hcmInstance*> &gate_sim){
	// ensure we dont simulate same gates multiple times
	//-ie. multiple signal effect and change same output.. all simulate stabilized version????
	int prior = events.size();
	vector<hcmNode*>::iterator non_repeat = std::unique(events.begin(), events.end());
	events.resize(std::distance(events.begin(), non_repeat));
	int post = events.size();

	vector<hcmNode*>::iterator nI; 
	
	for(nI = events.begin(); nI != events.end(); nI++){
		map<string, hcmInstPort* > instp = (*nI)->getInstPorts(); //returns all instport of given node 
		map<string, hcmInstPort* >::iterator ipI; 
		//for all instports we find the effected instance or gate 
		for(ipI = instp.begin(); ipI != instp.end(); ipI++){
			//gates effected are gates for which this instport is an input
			hcmPort* cur_p = ipI->second->getPort(); 
			if(cur_p->getDirection() == IN){	
				//cout<< "THESE ARE THE AFFECTED GATES ----------------"<<endl;
				//cout<<ipI->second->getInst()->getName()<<endl;
				gate_sim.push_back(ipI->second->getInst());//returns Instance to which input is effecting		
			}
			
		}
	}
	
	
	
	
}
/*
Simulate GATES
Given gates in gate sim queue simulates gate of the given hcminstances with its current inputs by calling matching gate handler.
*/
void simulate(vector<hcmNode*> &events, vector<hcmInstance*> &gate_sim){
		vector<hcmInstance*>::iterator iI; 
		bool (*op)(bool x1,bool x2); 
		//find gate name for each instance and simulate it 
		for(iI=gate_sim.begin(); iI!=gate_sim.end(); iI++){
			hcmInstance* inst = (*iI);
			string cur_gate = inst->getName();
			string cur_gate_m = inst->masterCell()->getName();

			bool n = 0 ;
			//depending on stdcell.v used we can identify if cur instance is part of a DFF
			// by either inst name or master name
			if(string::npos != cur_gate.find("dff")||string::npos != cur_gate_m.find("dff")){
				continue; 
			}
			
			if(string::npos != cur_gate_m.find("and")){
				n=(string::npos != cur_gate_m.find("nand"));
				op = &and_g;
			}
			else if(string::npos != cur_gate_m.find("or")){
				n=(string::npos != cur_gate_m.find("nor"));
				op = &or_g;
			}
			else if(string::npos != cur_gate_m.find("xor")){
				op = &xor_g;
			}
			else if(string::npos != cur_gate_m.find("not")){
				op = &not_g;
			}
			else if(string::npos != cur_gate_m.find("buffer")){
				op = &buffer_g;
			}	
			
		gate_eval(events, inst, op, n);	
		}
	gate_sim.clear();
			
	
}
/*
Gate evaluation function-> evaluates the output of a given hcmInstance* 
if output value changes output node is added to event queue
->all primitives in std_cell have a given output- therefore we use this for simulation

*/
void gate_eval(vector<hcmNode*> &events, hcmInstance* cur_inst, bool (*op)(bool x1,bool x2), bool n){
	map<string, hcmInstPort*> inst_ports = cur_inst->getInstPorts();
	map<string, hcmInstPort*>::iterator ipI;
	hcmInstPort* out_inst;
	bool result; // initial value of result depends on gate? currently made for AND 
	bool in_state = 0;
	bool out_state = 0; 
	int count = 0 ; 
	
	for(ipI = inst_ports.begin(); ipI != inst_ports.end(); ipI++){
		hcmInstPort* cur_instp = ipI->second;
		
		//find direction
		hcmPort* cur_p = ipI->second->getPort(); 
		
		//calculate bool result 
		if(cur_p->getDirection() == IN){
			cur_instp->getNode()->getProp("cur_bool", in_state);//get input value 
			if(count == 0 ) {result = in_state;}
			else{result = op(result,in_state);}
		}	
		// obtain current output value 
		if(cur_p->getDirection() == OUT){
			out_inst = ipI->second;	
			out_inst->getNode()->getProp("cur_bool",out_state);
		}
		count ++; 
	}
	
	if(n){result = !result;}
	
	// check if output has changed- if yes add the output node to the event queue 
	if(out_state!= result){
		hcmNode * out_node = out_inst->getNode();
		out_node->setProp("cur_bool",result);
		events.push_back(out_node);
	} 
	
}
/*
FF_EVENT
function reviews states of all dff instances and asses whether for the current input there is a new event 
it will update the event queue (recieves a pointer to the event queue)
We asses input node D value and compare it to the output value- if there is a change depending on the state 
of the clock we update the dff output value and add the event for the output node.

When clock is 0 we want to update the output
When the clock is 1 we "Store" 
*/

void ff_event(vector<hcmNode*>& events, hcmInstance* inst, bool f){
	map<string, hcmInstPort* > instp = inst->getInstPorts();  // get all the inst ports of this node
	map<string, hcmInstPort* >::iterator ipI;
	
	bool clk_state = false;
	bool D_state = false;
	hcmInstPort* out_inst;
	bool out_state = false; 
	
	for(ipI = instp.begin(); ipI != instp.end(); ipI++){
		hcmInstPort* cur_inst = ipI->second;
		
		//every instport represents a port in the mastercell (we use this to find direction)
		hcmPort* cur_p = ipI->second->getPort(); 
		
		if(cur_p->getDirection() == IN){
			if(cur_p->getName()=="D"){
				cur_inst->getNode()->getProp("cur_bool", D_state);// update current input to dff 
		}else if(cur_p->getName() == "CLK"){
				cur_inst->getNode()->getProp("cur_bool", clk_state);	
		}
		}
		
		if(cur_p->getDirection() == OUT){
			out_inst = ipI->second;	
			out_inst->getNode()->getProp("cur_bool",out_state);
		} 
	}
	
	if(f){
		cout << "WE ARE IN FIRST ROUND" <<endl; 
		hcmNode * out_node = out_inst->getNode();
		events.push_back(out_node);
	
	}else{
		if(clk_state == false){
			//check if output changes- if not no need to update anything
			if(D_state!=out_state){
				cout<<"old out-state"<<out_state<<endl;
				cout<<"new out-state"<<D_state<<endl;
				hcmNode * out_node = out_inst->getNode();
				out_node->setProp("cur_bool",D_state);
				events.push_back(out_node);
			}	
		}
	}	
	// if clock_state is true we store values
}

void printfunc(map<string, hcmNode* > &topcell_nodes){
	
	map<string, hcmNode* >::iterator nI; 
	bool node_val; 
	for(nI = topcell_nodes.begin();nI != topcell_nodes.end(); nI++){
		hcmNode *node = (*nI).second;
		node->getProp("cur_bool", node_val);
		string nname = node->getName();
		cout << "node name:" << nname << " node value: " << node_val <<endl; 		
	}
	cout<< "----------------------------------------------------------------------------"<<endl;
}



///////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv) {
  string sigsFileName;
  string vecsFileName;
  string topLevel;
  int argIdx = 1;
  int anyErr = 0;
  unsigned int i;
  vector<string> vlgFiles;
  bool shortRun = false;

  if (argc < 6) {
    anyErr++;
  } else {
    if (!strcmp(argv[argIdx], "-v")) {
      argIdx++;
      verbose = true;
    }
	
    if (!strcmp(argv[argIdx], "-s")) {
      argIdx++;
      shortRun = true;
    }
	topLevel = string(argv[argIdx++]);
	sigsFileName = string(argv[argIdx++]);
	vecsFileName = string(argv[argIdx++]);

    for (;argIdx < argc; argIdx++) {
      vlgFiles.push_back(argv[argIdx]);
    }
    
    if (vlgFiles.size() < 2) {
      cerr << "-E- At least top-level and single verilog file required for spec model" << endl;
      anyErr++;
    }
  }

  if (anyErr) {
    cerr << "Usage: " << argv[0] << "  [-v] [-s] top-cell sigs-file vecs-file file1.v [file2.v] ... \n";
    exit(1);
  }

  hcmSigVec parser(sigsFileName, vecsFileName, verbose);
  set<string> sigs;
  parser.getSignals(sigs);
  
  //TODO: debug delete later 
  for (set<string>::iterator I= sigs.begin(); I != sigs.end(); I++) {
	 cout << "SIG: " << (*I) << endl;
  }

  
  set<string> globalNodes;
  globalNodes.insert("VDD");
  globalNodes.insert("VSS");

  hcmDesign* design = new hcmDesign("design");
  //string cellName = vlgFiles[0];
  for (i = 0; i < vlgFiles.size(); i++) {
    printf("-I- Parsing verilog %s ...\n", vlgFiles[i].c_str());
    if (!design->parseStructuralVerilog(vlgFiles[i].c_str())) {
      cerr << "-E- Could not parse: " << vlgFiles[i] << " aborting." << endl;
      exit(1);
    }
  }

    hcmCell *topCell = design->getCell(topLevel);
  if (!topCell) {
    printf("-E- could not find cell %s\n", topLevel.c_str());
    exit(1);
  }
  
 
 hcmCell *flatCell = hcmFlatten(topLevel + string("_flat"), topCell, globalNodes);
 cout << "-I- Top cell flattened" << endl;
 
 vcdFormatter vcd(topLevel + ".vcd", flatCell, globalNodes);
  if (!vcd.good()) {
    printf("-E- Could not create vcdFormatter for cell: %s\n", topLevel.c_str());
    exit(1);
  }
 // Initialization of all states of all nodes in Topcell- we initialize all to 0 
 
map<string, hcmNode* > topcell_nodes = flatCell->getNodes();  
map<string, hcmNode* >::iterator nI;

//We erase VDD and VSS from map of simulated nodes while assigning them to the desired value 
 for(nI = topcell_nodes.begin();nI != topcell_nodes.end(); nI++){
		hcmNode *node = (*nI).second;
		node->setProp("cur_bool", false);
		string nname = node->getName();
		
		if (nname == "VSS")
		{
			topcell_nodes.erase(nI);
			continue;
		}
		if (nname == "VDD")
		{
			node->setProp("cur_bool", true); // VDD is '1'
			topcell_nodes.erase(nI);
			continue;
		}
			
	}
	
vector<hcmInstance*> dff_instances; 
map<string, hcmInstance*> all_instances = flatCell->getInstances();
map<string, hcmInstance*>::iterator iI; 

//create vector of dff's for preliminary simulation 
for(iI = all_instances.begin();iI != all_instances.end();iI++){
	string cur_inst = iI->second->getName();
	string cur_inst_m = iI->second->masterCell()->getName();
	cout<<"current instance"<< cur_inst << endl;
	cout<< "current instance master"<< iI->second->masterCell()->getName()<<endl ; 
	
	if(string::npos != cur_inst.find("dff")||string::npos != cur_inst_m.find("dff")){
		dff_instances.push_back(iI->second);
	}
}

int stime = 0;  // holds the simulation time 
hcmNode* cur_n; //current node we operate on in the simulation 
bool prev_val;
bool cur_val; 
vector<hcmNode*> events; //vector which acts as an event queue 
vector<hcmInstance*> gates_tosim_queue; //vector which acts as an event queue 
bool f ;

//We simulate the circuit as long as there are new signal values  
while (parser.readVector() == 0) {
	vcd.changeTime(stime); 
	cout<<stime<<endl; 

	// evaluate the dff states (prevents loop and enables simulation of dff)
	// add any changes to the even queue, we do this prior to rest of the circuit 
	cout<< "NEW STIME"<<endl; 
	printfunc(topcell_nodes);
	vector<hcmInstance*>::iterator dI;
	
	for(dI= dff_instances.begin();dI!=dff_instances.end();dI++){
		hcmInstance* cur_FF = *dI; 
		ff_event(events, cur_FF, f); //passed by reference
	}
	
	
	//extract all signal values at current time 
	 for (set<string>::iterator I= sigs.begin(); I != sigs.end(); I++) {
		string name = (*I);
		bool val;
		parser.getSigValue(name, val);
		cur_n = topcell_nodes[name]; //extract pointer to the node signal represents 
		cur_n->getProp("cur_bool",cur_val);
		if(stime ==0 ){
			events.push_back(cur_n);
			cur_n->setProp("cur_bool", val);
			continue; 
		}
		if(cur_val == val) continue; //case where the node value stays the same
		else{ 
		events.push_back(cur_n);
		cur_n->setProp("cur_bool", val);
		}
			
	 }
	 
	 if(stime ==0){
		map<string, hcmNode* >::iterator nI;
	//We erase VDD and VSS from map of simulated nodes while assigning them to the desired value 
		 for(nI = topcell_nodes.begin();nI != topcell_nodes.end(); nI++){
				hcmNode *node = (*nI).second;
				events.push_back(node);
				cout<< node->getName() <<endl; 
				
			}
	 }
	 	cout<< "READ SIGNALS"<<endl; 

		printfunc(topcell_nodes);
	
	
	 int count = 0;
	 vector<hcmNode*>::iterator nI;
	//Find gates effects by signal value changes and simulate the circuit till we reach stability  
	while(events.size()){
		affected_gates(events, gates_tosim_queue);
		events.clear(); //we added all affected gates currently no more events 
		simulate(events, gates_tosim_queue);
		

	}


	
	//update VCD file (for all signals?)
	map<string, hcmNode* >::iterator nnI;
	list<const hcmInstance*> parents; 
	bool cur_state; 
	
	//We erase VDD and VSS from map of simulated nodes while assigning them to the desired value 
	for(nnI = topcell_nodes.begin();nnI != topcell_nodes.end(); nnI++){
		hcmNode* vcd_node = nnI->second;
		if(vcd_node->getName() == "VSS" || vcd_node->getName() == "VDD")
			continue;
		hcmNodeCtx* ctx = new hcmNodeCtx(parents, vcd_node);
		vcd_node->getProp("cur_bool",cur_state);
		vcd.changeValue(ctx, cur_state);
		
	}
	stime++; 
  }
	



 

}  