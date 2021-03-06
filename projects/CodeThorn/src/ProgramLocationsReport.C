#include "sage3basic.h"
#include "ProgramLocationsReport.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include "CodeThornException.h"
#include "SgNodeHelper.h"

using namespace std;
using namespace CodeThorn;

void CodeThorn::ProgramLocationsReport::setAllLocationsOfInterest(LabelSet loc) {
  allLocations=loc;
}

LabelSet CodeThorn::ProgramLocationsReport::verifiedLocations(){
  LabelSet a=allLocations;
  a-=definitiveLocations;
  a-=potentialLocations;
  return a;
}

LabelSet CodeThorn::ProgramLocationsReport::falsifiedLocations() {
  return definitiveLocations;
}

LabelSet CodeThorn::ProgramLocationsReport::unverifiedLocations() {
  return potentialLocations-definitiveLocations;
}

LabelSet CodeThorn::ProgramLocationsReport::verifiedFunctions(Labeler* labeler) {
  return filterFunctionEntryLabels(labeler,verifiedLocations());
}

LabelSet CodeThorn::ProgramLocationsReport::falsifiedFunctions(Labeler* labeler) {
  return filterFunctionEntryLabels(labeler,falsifiedLocations());
}

LabelSet CodeThorn::ProgramLocationsReport::unverifiedFunctions(Labeler* labeler) {
  return filterFunctionEntryLabels(labeler,unverifiedLocations());
}

LabelSet CodeThorn::ProgramLocationsReport::filterFunctionEntryLabels(Labeler* labeler, LabelSet labSet) {
  LabelSet functionEntryLabels;
  for(auto lab : labSet) {
    if(labeler->isFunctionEntryLabel(lab)) {
      functionEntryLabels.insert(lab);
    }
  }
  return functionEntryLabels;
}

bool CodeThorn::ProgramLocationsReport::hasSourceLocation(SgStatement* stmt) {
  ROSE_ASSERT(stmt);
  Sg_File_Info* fi=stmt->get_file_info();
  return fi->get_line()>0;
}

string CodeThorn::ProgramLocationsReport::programLocation(Labeler* labeler, Label lab) {
  SgNode* node=labeler->getNode(lab);
  ROSE_ASSERT(node);
  // find non-transformation file info
  SgNode* parent=node->get_parent();
  // if node is inside expression, search for statement node
  while(!isSgStatement(node)) {
    node=node->get_parent();
    if(node==nullptr)
      return "[unresolved source location]";
  }
  SgStatement* stmt=isSgStatement(node);
  ROSE_ASSERT(stmt);
  while(!hasSourceLocation(stmt)) {
    stmt=SageInterface::getPreviousStatement(stmt);
    if(!stmt)
      return "[unresolved source location]";
  }
  node=stmt;
  ROSE_ASSERT(stmt);
  // return fileinfo as formatted string
  return SgNodeHelper::sourceLineColumnToString(node)+","+SgNodeHelper::sourceFilenameToString(node);
}

string CodeThorn::ProgramLocationsReport::sourceCodeAtProgramLocation(Labeler* labeler, Label lab) {
  SgNode* node=labeler->getNode(lab);
  ROSE_ASSERT(node);
  return SgNodeHelper::doubleQuotedEscapedString(node->unparseToString());
}

bool CodeThorn::ProgramLocationsReport::isNonRecordedLocation(Label lab) {
  return !definitiveLocations.isElement(lab)&&!potentialLocations.isElement(lab);
}

LabelSet CodeThorn::ProgramLocationsReport::determineRecordFreeFunctions(CFAnalysis& cfAnalyzer, Flow& flow) {
  LabelSet funEntries=cfAnalyzer.functionEntryLabels(flow);
  LabelSet verifiedFunctions;
  for (Label entryLab : funEntries) {
    bool noLocationsRecorded=true;
    LabelSet funLabelSet=cfAnalyzer.functionLabelSet(entryLab,flow);
    // determine whether all labels are verified
    for (Label funLab : funLabelSet) {
      if(!isNonRecordedLocation(funLab)) {
        noLocationsRecorded=false;
        break;
      }
    }
    if(noLocationsRecorded) {
      // entire function has no violations
      verifiedFunctions.insert(entryLab);
    }
  }
  return verifiedFunctions;
}

void ProgramLocationsReport::recordDefinitiveLocation(CodeThorn::Label lab) {
#pragma omp critical(definitiveproglocrecording)
  {
    definitiveLocations.insert(lab);
  }
}
void ProgramLocationsReport::recordPotentialLocation(CodeThorn::Label lab) {
#pragma omp critical(potentialproglocrecording)
  {
    potentialLocations.insert(lab);
  }
}
 
size_t ProgramLocationsReport::numDefinitiveLocations() {
  return definitiveLocations.size();
}
 size_t ProgramLocationsReport::numPotentialLocations() {
  return potentialLocations.size();
}
size_t ProgramLocationsReport::numTotalLocations() {
  return definitiveLocations.size()+potentialLocations.size();
}

void CodeThorn::ProgramLocationsReport::writeResultFile(string fileName, CodeThorn::Labeler* labeler) {
  std::ofstream myfile;
  myfile.open(fileName.c_str(),std::ios::out);
  if(myfile.good()) {
    for(auto lab : definitiveLocations) {
      myfile<<"definitive,"<<programLocation(labeler,lab);
      myfile<<","<<sourceCodeAtProgramLocation(labeler,lab);
      myfile<<endl;
    }
    for(auto lab : potentialLocations) {
      myfile<<"potential,"<<programLocation(labeler,lab);
      myfile<<","<<sourceCodeAtProgramLocation(labeler,lab);
      myfile<<endl;
    }
    myfile.close();
  } else {
    throw CodeThorn::Exception("Error: could not open file "+fileName+".");
  }
}

void CodeThorn::ProgramLocationsReport::writeResultToStream(std::ostream& stream, CodeThorn::Labeler* labeler) {
  writeAllDefinitiveLocationsToStream(stream,labeler,true,true,true);
  writeAllPotentialLocationsToStream(stream,labeler,true,true,true);
}

void CodeThorn::ProgramLocationsReport::writeAllDefinitiveLocationsToStream(std::ostream& stream, CodeThorn::Labeler* labeler, bool qualifier, bool programLocation, bool sourceCode) {
  writeLocationsToStream(stream,labeler,definitiveLocations,"definitive",true,true);
}
void CodeThorn::ProgramLocationsReport::writeAllPotentialLocationsToStream(std::ostream& stream, CodeThorn::Labeler* labeler, bool qualifier, bool programLocation, bool sourceCode) {
  writeLocationsToStream(stream,labeler,definitiveLocations,"definitive",true,true);
}

void CodeThorn::ProgramLocationsReport::writeLocationsToStream(std::ostream& stream, CodeThorn::Labeler* labeler, LabelSet& set, string qualifier, bool programLocation, bool sourceCode) {
  for(auto lab : set) {
    if(qualifier.size()>0)
      stream<<qualifier;
    if(qualifier.size()>0&&(programLocation||sourceCode))
      stream<<": ";
    if(programLocation)
      stream<<this->programLocation(labeler,lab);
    if(programLocation&&sourceCode)
      stream<<": ";
    if(sourceCode)
      stream<<sourceCodeAtProgramLocation(labeler,lab);
    stream<<endl;
  }
}

void ProgramLocationsReport::writeLocationsVerificationReport(std::ostream& os, Labeler* labeler) {
  int int_n=allLocations.size();
  double n=(double)int_n;
  LabelSet verified=verifiedLocations();
  int v=verified.size();
  LabelSet falsified=falsifiedLocations();
  int f=falsified.size();
  LabelSet unverified=unverifiedLocations();
  int u=unverified.size();
  os<<std::fixed<<std::setprecision(2);
  os<<"Proven     locations: "<<setw(6)<<f+v<<" ["<<setw(6)<<(f+v)/n*100.0<<"%]"<<endl;
  os<<" Verified  locations: "<<setw(6)<< v <<" ["<<setw(6)<<v/n*100.0<<"%]"<<endl;
  os<<" Falsified locations: "<<setw(6)<< f <<" ["<<setw(6)<<f/n*100.0<<"%]"<<endl;
  os<<"Unproven   locations: "<<setw(6)<< u <<" ["<<setw(6)<<u/n*100.0<<"%]"<<endl;
  os<<"Total      locations: "<<setw(6)<<int_n<<endl;
#if 0
  os<<"Detected Errors:"<<endl;
  if(f==0) {
    cout<<"None."<<endl;
  } else {
    for(auto lab:definitiveLocations) {
      os<<sourceCodeAtProgramLocation(labeler,lab)<<endl;
    }
  }
#endif
}

void ProgramLocationsReport::writeFunctionsVerificationReport(std::ostream& os, Labeler* labeler) {
  cerr<<"Error: writeFunctionsVerificationReport not implemented yet."<<endl;
  exit(1);
}
