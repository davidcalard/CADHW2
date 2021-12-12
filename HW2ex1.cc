#include <errno.h>
#include <signal.h>
#include <sstream>
#include <fstream>
#include "hcm.h"
#include "flat.h"
#include "hcmvcd.h"

using namespace std;

bool verbose = false;

///////////////////helper function declarations////////////////////////////



///////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv) {
  string sigsFileName;
  string vecsFileName;
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
	sigsFileName = string(argv[argIdx++]);
	vecsFileName = string(argv[argIdx++]);
	
    if (!strcmp(argv[argIdx], "-s")) {
      argIdx++;
      shortRun = true;
    }

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
  
  set< string> globalNodes;
  globalNodes.insert("VDD");
  globalNodes.insert("VSS");

  hcmDesign* design = new hcmDesign("design");
  string cellName = vlgFiles[0];
  for (i = 1; i < vlgFiles.size(); i++) {
    printf("-I- Parsing verilog %s ...\n", vlgFiles[i].c_str());
    if (!design->parseStructuralVerilog(vlgFiles[i].c_str())) {
      cerr << "-E- Could not parse: " << vlgFiles[i] << " aborting." << endl;
      exit(1);
    }
  }

    hcmCell *topCell = design->getCell(cellName);
  if (!topCell) {
    printf("-E- could not find cell %s\n", cellName.c_str());
    exit(1);
  }
  
  vcdFormatter vcd(cellName + ".vcd", topCell, globalNodes);
  if (!vcd.good()) {
    printf("-E- Could not create vcdFormatter for cell: %s\n", 
           cellName.c_str());
    exit(1);
  }

































  set< string> globalNodes;
  globalNodes.insert("VDD");
  globalNodes.insert("VSS");
  
  hcmDesign* design = new hcmDesign("design");
  string cellName = vlgFiles[0];
  for (i = 1; i < vlgFiles.size(); i++) {
    printf("-I- Parsing verilog %s ...\n", vlgFiles[i].c_str());
    if (!design->parseStructuralVerilog(vlgFiles[i].c_str())) {
      cerr << "-E- Could not parse: " << vlgFiles[i] << " aborting." << endl;
      exit(1);
    }
  }

  hcmCell *topCell = design->getCell(cellName);
  if (!topCell) {
    printf("-E- could not find cell %s\n", cellName.c_str());
    exit(1);
  }
  
  vcdFormatter vcd(cellName + ".vcd", topCell, globalNodes);
  if (!vcd.good()) {
    printf("-E- Could not create vcdFormatter for cell: %s\n", 
           cellName.c_str());
    exit(1);
  }
}  