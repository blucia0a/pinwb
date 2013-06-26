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
  UINT32 size;

  
  WriteBufferEntry(){
    this->realAddr = NULL;
    this->realValue = NULL;
    this->size = 0;
  }

  WriteBufferEntry(ADDRINT a, ADDRINT v, UINT32 size){
    this->realAddr = a;
    this->realValue = v;
    this->size = size;
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

  ADDRINT handleAccess( ADDRINT origEA, UINT32 access, UINT32 asize, ADDRINT instadd){

      //REG_SEG_GS/FS_BASE
      if( this->map.find( origEA ) != this->map.end() ){

        /*It is in the Write Buffer*/
        if( access == 1 ){
        /*It was a read*/
        } else if ( access == 2 ){
        /*It was a write*/
        } else if ( access == 3 ){
        /*It was a read/write*/
        }else{

          fprintf(stderr,"WARNING:Unknown access type %lu\n",(unsigned long)access);

        }
      

      } else {

        /*It was not in the write buffer*/
        if( access == 1 ){

          //fprintf(stderr,"Warning: Reading Uninitialized Memory! %016llx @ %016llx\n",origEA, instadd);
          return origEA;

        }

        ADDRINT wbStorage = (ADDRINT)malloc(asize); 
        
        WriteBufferEntry *wbe = new WriteBufferEntry(origEA, wbStorage, asize);
 
        this->map[ origEA ] = wbe;



      }
      
      assert( asize <= this->map[ origEA ]->size );
      //fprintf(stderr,"%s: %016llx <=> B[%016llx] size %d @ %016llx\n",(access==1?"R":(access==2?"W":"R/W")),origEA,this->map[ origEA ]->realValue,asize,instadd);
      return this->map[ origEA ]->realValue;


  }

};

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

ADDRINT handleAccess( THREADID tid, ADDRINT origEA, ADDRINT asize, UINT32 access, ADDRINT instadd){
   ADDRINT loc = bufs[ tid % NUMBUFS ]->handleAccess( origEA, access, asize, instadd);
   return loc;
}


VOID instrumentRoutine(RTN rtn, VOID *v){

  //TODO: Add code to cause fences to flush the store buffer


}

BOOL reachedMain = false;
VOID instrumentTrace(TRACE trace, VOID *v){

  if( !reachedMain && !strcmp(RTN_Name(TRACE_Rtn( trace )).c_str(),"main") ){
    reachedMain = true;
    fprintf(stderr,"Reached Main!  Instrumenting from now on!\n");
  }

  if( !reachedMain ){ return; }

  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {

        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {

            for (UINT32 op = 0; op<INS_MemoryOperandCount(ins); op++){

                           
              UINT32 access = 0;
                     access = (INS_MemoryOperandIsRead(ins,op)    ? 1 : 0) |
                              (INS_MemoryOperandIsWritten(ins,op) ? 2 : 0);


              if( INS_SegmentPrefix(ins) == true ){ 

                //continue;

              } 

              
           
              INS_InsertCall(ins, IPOINT_BEFORE,
                             AFUNPTR(handleAccess),
                             IARG_THREAD_ID,
                             IARG_MEMORYOP_EA,   op,
                             IARG_UINT32,  INS_MemoryOperandSize(ins,op),
                             IARG_UINT32, access,
                             IARG_INST_PTR,
                             IARG_RETURN_REGS,   REG_INST_G0 + op, 
                             IARG_END);
              INS_RewriteMemoryOperand(ins, op, REG(REG_INST_G0 + op) );
          
          
            }
        }
    }
}

VOID instrumentImage(IMG img, VOID *v)
{

  fprintf(stderr,"Loading Image: %s\n",IMG_Name(img).c_str());
  
  //if( !IMG_IsMainExecutable(img) ){ fprintf(stderr,"Returning Early\n"); return; }

/*  for( SEC sec= IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec) ){

    for( RTN rtn= SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn) ){

      RTN_Open(rtn);
      for( INS ins= RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins) ){

    
      }
      RTN_Close(rtn);

    }

  }*/

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
  
  IMG_AddInstrumentFunction(instrumentImage,0);
  TRACE_AddInstrumentFunction(instrumentTrace,0);

  PIN_AddThreadStartFunction(threadBegin, 0);
  PIN_AddThreadFiniFunction(threadEnd, 0);
  
  PIN_AddFiniFunction(Fini, 0);
  PIN_StartProgram();
  return 0;
}
