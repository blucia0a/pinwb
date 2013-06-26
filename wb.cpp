#include "pin.H"

#include <signal.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <list>
#include <set>
#include <ext/hash_map>
#include <assert.h>

using __gnu_cxx::hash_map;

#define NUMBUFS 4
class WriteBufferEntry{

public:

  ADDRINT realAddr;
  ADDRINT realValue;
  ADDRINT timestamp;

  
  WriteBufferEntry(){
    this->realAddr = NULL;
    this->realValue = NULL;
    this->timestamp = 0;
  }

  WriteBufferEntry(ADDRINT a, ADDRINT v, unsigned long t){
    this->realAddr = a;
    this->realValue = v;
    this->timestamp = t;
  }

  ADDRINT getAddr(){
    return this->realAddr; 
  }
  
  ADDRINT getValue(){
    return this->realValue; 
  }
  
  void setAddr(ADDRINT a){
    this->realAddr = a; 
  }
  
  void setValue(ADDRINT v){
    this->realValue = v; 
  }

};


namespace __gnu_cxx {
template<> struct hash<ADDRINT> {
  size_t operator()(const ADDRINT& k) const {
    hash<unsigned long> hash_fn;
    return hash_fn( (unsigned long) k);
  }
};
}

class WriteBuffer{

  unsigned long timestamp;
  hash_map<ADDRINT, WriteBufferEntry *> map;
  set<ADDRINT> bufferLocations;
  int size;

public:
  WriteBuffer(int s){

    this->size = s;
    this->map = hash_map<ADDRINT, WriteBufferEntry *>();
    this->bufferLocations = set<ADDRINT>();
    this->timestamp = 0;

  }

  ADDRINT handleAccess( ADDRINT origEA, UINT32 access, ADDRINT instadd ){

      fprintf(stderr,"ADDR = %016llx \n", origEA);
      /*TODO: Need to decide here if this address is one of ours or one from
              the program.  Condition on whether the address is found in a set
              of all addresses that make up the write buffer?  Better way?  

              Question: Does Pin use a different bunch of addresses than the
                        program uses? 
      */
      if( this->bufferLocations.find( origEA ) != this->bufferLocations.end() ){
        fprintf(stderr,"The program took a reference to the buffer location at %llx\n",origEA);
      }
     
      if( this->map.find( origEA ) != this->map.end() ){
        fprintf(stderr,"found it!\n");

        /*It is in the Write Buffer*/
        if( access == 1 ){
        /*It was a read*/
  
            fprintf(stderr,"[0x%016llx] = 0x%016llx\n",origEA,this->map[ origEA ]->realValue);
            return (ADDRINT)(&(this->map[ origEA ]->realValue));

        } else if ( access == 2 ){
        /*It was a write*/
            return (ADDRINT)(&(this->map[ origEA ]->realValue));
            //return origEA; 

        } else if ( access == 3 ){
        /*It was a read/write*/
            return (ADDRINT)(&(this->map[ origEA ]->realValue));
            //return origEA; 
   
        }else{

          fprintf(stderr,"Unknown access type %lu\n",(unsigned long)access);
          return (ADDRINT)(&(this->map[ origEA ]->realValue));

        }

      } else {

        fprintf(stderr,"did not find it!\n");

        /*It was not in the write buffer*/
        WriteBufferEntry *wbe = new WriteBufferEntry(origEA, *((ADDRINT*)origEA), this->timestamp++);
        
        fprintf(stderr,"made an entry!\n");

        this->map[ origEA ] = wbe;
 
        fprintf(stderr,"(uninit) [0x%016llx] = 0x%016llx\n",origEA,*((ADDRINT*)origEA));

        ADDRINT bufferLocation = (ADDRINT)(&(this->map[ origEA ]->realValue));
        this->bufferLocations.insert( bufferLocation ); 

        return bufferLocation;

      }

  fprintf(stderr,"GOT HERE.  Shouldn't have.  Going to abort.\n");
  return (ADDRINT)NULL;
  }

};

REG regs[3];
WriteBuffer **bufs;
#define MAX_NTHREADS 256
bool instrumentationOn[MAX_NTHREADS];
KNOB<int> KnobNumWBs(KNOB_MODE_WRITEONCE, "pintool",
			  "numwbs", "4", "Number of write buffers");
KNOB<int> KnobWBSize(KNOB_MODE_WRITEONCE, "pintool",
			  "wbsize", "6", "Size of each");

INT32 usage()
{

    cerr << KNOB_BASE::StringKnobSummary();
    cerr << endl;
    return -1;

}


VOID TurnInstrumentationOn(THREADID tid){
  instrumentationOn[tid] = true;
}

VOID TurnInstrumentationOff(THREADID tid){
  instrumentationOn[tid] = false;
}

ADDRINT handleAccess( THREADID tid, ADDRINT origEA, UINT32 access ){
   ADDRINT loc = bufs[ tid % NUMBUFS ]->handleAccess( origEA, access, 0);
   fprintf(stderr,"Returning %16llx to the rewriter\n",loc);
   return loc;
}

VOID instrumentRoutine(RTN rtn, VOID *v){

  //TODO: Add code to cause fences to flush the store buffer


}

VOID instrumentImage(IMG img, VOID *v)
{

  fprintf(stderr,"Loading Image: %s\n",IMG_Name(img).c_str());
  
  if( strstr(IMG_Name(img).c_str(),"dyld") != NULL ){ fprintf(stderr,"Not instrumenting the linker\n"); return; }

  //if( !IMG_IsMainExecutable(img) ){ fprintf(stderr,"Returning Early\n"); return; }

  for( SEC sec= IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec) ){

    for( RTN rtn= SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn) ){

      RTN_Open(rtn);
      for( INS ins= RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins) ){

        if( INS_IsStackRead( ins ) ||
            INS_IsStackWrite( ins )
            ){

          continue;

        } 
        string s = hexstr(INS_Address(ins));
      
        for (UINT32 op = 0; op<INS_MemoryOperandCount(ins); op++){
                       
          UINT32 access = 0;
                 access = (INS_MemoryOperandIsRead(ins,op)    ? 1 : 0) |
                          (INS_MemoryOperandIsWritten(ins,op) ? 2 : 0);
       
          INS_InsertCall(ins, IPOINT_BEFORE,
                         AFUNPTR(handleAccess),
                         IARG_THREAD_ID,
                         IARG_MEMORYOP_EA,   op,
                         IARG_UINT32, access,
                         IARG_RETURN_REGS,   regs[op], 
                         IARG_END);
      
          INS_RewriteMemoryOperand(ins, op, REG(regs[op]));
      
        }
    
      }
      RTN_Close(rtn);

    }

  }

}


VOID instrumentInstruction(INS ins, VOID *v){
   
}

VOID threadBegin(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v){
  instrumentationOn[threadid] = true;
}
    
VOID threadEnd(THREADID threadid, const CONTEXT *ctxt, INT32 code, VOID *v)
{

}

VOID dumpInfo(){
/*This is probably useful to call in Fini if you're aggregating data in memory instead of directly to a file*/

}

VOID Fini(INT32 code, VOID *v)
{
  

}

BOOL segvHandler(THREADID threadid,INT32 sig,CONTEXT *ctx,BOOL hasHndlr,VOID*v){
  return TRUE;//let the program's handler run too
}

BOOL termHandler(THREADID threadid,INT32 sig,CONTEXT *ctx,BOOL hasHndlr,VOID*v){
  return TRUE;//let the program's handler run too
}

int main(int argc, char *argv[])
{
  PIN_InitSymbols();
  if( PIN_Init(argc,argv) ) {
    return usage();
  }

  int wbSize = KnobWBSize.Value();
  bufs = new WriteBuffer*[NUMBUFS];
  for(int i = 0; i < NUMBUFS; i++){
    bufs[i] = new WriteBuffer(wbSize);
  }
  for(int i = 0; i < 3; i++){
    regs[i] = PIN_ClaimToolRegister();
    assert( regs[i] != REG_INVALID() );
  }
  
  IMG_AddInstrumentFunction(instrumentImage,0);
  //RTN_AddInstrumentFunction(instrumentRoutine,0);
  //INS_AddInstrumentFunction(instrumentInstruction, 0);

  PIN_AddThreadStartFunction(threadBegin, 0);
  PIN_AddThreadFiniFunction(threadEnd, 0);
  
  PIN_AddFiniFunction(Fini, 0);
  PIN_StartProgram();
  return 0;
}
