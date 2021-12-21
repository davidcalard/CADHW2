#include <errno.h>
#include <signal.h>
#include <sstream>
#include <fstream>
#include "hcm.h"
#include "flat.h"
#include "hcmvcd.h"
#include "hcmsigvec.h"

using namespace std;

bool verbose = false;

///////////////////helper function declarations////////////////////////////



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

  cout << "-I- Reading vectors ... " << endl;
  while (parser.readVector() == 0) {
	 for (set<string>::iterator I= sigs.begin(); I != sigs.end(); I++) {
		string name = (*I);
		bool val;
		parser.getSigValue(name, val);
		cout << "  " << name << " = " << (val? "1" : "0")  << endl;
	 }
	 cout << "-I- Reading next vectors ... " << endl;
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
  
  vcdFormatter vcd(topLevel + ".vcd", topCell, globalNodes);
  if (!vcd.good()) {
    printf("-E- Could not create vcdFormatter for cell: %s\n", 
           topLevel.c_str());
    exit(1);
  }
 hcmCell *flatCell = hcmFlatten(topLevel + string("_flat"), topCell, globalNodes);
 cout << "-I- Top cell flattened" << endl;
 
 // Initialization of all states of all nodes in Topcell- we initialize all to 0 
 
map<string, hcmNode* > topcell_nodes = flatCell->getNodes();  
map<string, hcmNode* >::iterator nI;

//We erase VDD and VSS from map of simulated nodes while assigning them to the desired value 
  for(nI = topcell_nodes.begin();nI != topcell_nodes.end(); nI++){
		hcmNode *node = (*nI).second;
		node->setProp("cur_bool", false);
		node->setProp("prev_bool", false);
		string nname = node->getName();
		
		if (nname == "VSS")
		{
			topcell_nodes.erase(nI);
			continue;
		}
		if (nname == "VDD")
		{
			node->setProp("cur_bool", true); // VDD is '1'
			node->setProp("prev_bool", true);
			topcell_nodes.erase(nI);
			continue;
		}
		
		
	}
	



 

}  