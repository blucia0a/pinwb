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

#undef INST_VALID
#undef TRACEOPS
#undef NOFLUSH
#undef SHOWIMGLOAD
#define NUMBUFS 64

#define READ 1
#define WRITE 2
#define READWRITE 3

using __gnu_cxx::hash_map;

#ifdef __APPLE__ 
#ifdef __MACH__
namespace __gnu_cxx{
template<>
struct hash<unsigned long long> {
    public:
        size_t operator()(unsigned long long const& s) const {

          hash<unsigned long> hash_fn;
          return hash_fn((unsigned long)s);

        }
};
}
#endif
#endif


class WriteBufferEntry{

public:

  ADDRINT realAddr;
  ADDRINT realValue;
  UINT32 size;

  
  WriteBufferEntry(){
    this->realAddr = 0;
    this->realValue = 0;
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

  VOID flushwb( ){

    #ifdef NOFLUSH
    return;
    #endif

    for( hash_map<ADDRINT,WriteBufferEntry *>::iterator i = this->map.begin(), e = this->map.end(); i != e; i++ ){

      WriteBufferEntry *wbe = i->second;
      memcpy((void*)wbe->realAddr,(void*)wbe->realValue,wbe->size);
      delete wbe;

    }

    this->map.clear();

  }

  ADDRINT handleAccess( ADDRINT origEA, UINT32 access, UINT32 asize, ADDRINT instadd){


      //REG_SEG_GS/FS_BASE
      if( this->map.find( origEA ) != this->map.end() ){

        /*It is in the Write Buffer*/
        if( access == READ ){
        /*It was a read*/
        } else if ( access == WRITE ){
        /*It was a write*/
        } else if ( access == READWRITE ){
        /*It was a read/write*/
        }else{

          fprintf(stderr,"WARNING:Unknown access type %lu\n",(unsigned long)access);

        }
      

      } else {

        /*It was not in the write buffer*/
        if( access == READ || access == READWRITE ){

          return origEA;

        }

        ADDRINT wbStorage = (ADDRINT)malloc(asize); 
        
        WriteBufferEntry *wbe = new WriteBufferEntry(origEA, wbStorage, asize);
 
        this->map[ origEA ] = wbe;

      }

      #ifdef TRACEOPS      
      fprintf(stderr,"%s: %016llx <=> B[%016llx] size %d @ %016llx\n",(access==READ?"R":(access==WRITE?"W":"R/W")),origEA,this->map[ origEA ]->realValue,asize,instadd);
      #endif
      assert( asize <= this->map[ origEA ]->size );
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

VOID flushwb(THREADID tid){

  bufs[tid % NUMBUFS]->flushwb();

}

ADDRINT handleAccess( THREADID tid, ADDRINT origEA, ADDRINT asize, UINT32 access, ADDRINT instadd){

   ADDRINT loc = bufs[ tid % NUMBUFS ]->handleAccess( origEA, access, asize, instadd);
   return loc;

}


VOID instrumentRoutine(RTN rtn, VOID *v){

  //TODO: Add code to cause fences to flush the store buffer

  if( !RTN_Valid(rtn) ){ return; } 
  if( strstr( RTN_Name(rtn).c_str(),"pthread_mutex" ) != NULL ){
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, AFUNPTR(flushwb),
                 IARG_THREAD_ID, IARG_END);
    RTN_Close(rtn);
  }
  
  

}

BOOL reachedMain = false;
VOID instrumentTrace(TRACE trace, VOID *v){

  RTN rtn = TRACE_Rtn(trace);
  if( RTN_Valid(rtn) ){

    SEC sec = RTN_Sec(rtn);
    if( SEC_Valid(sec) ){

      IMG img = SEC_Img(sec);
      if( !IMG_Valid(img) ){

        #ifdef INST_VALID
        fprintf(stderr," (i) ");
        #endif
        return;

      }else{

        if(!IMG_IsMainExecutable(img)){

          #ifdef INST_VALID
          fprintf(stderr,".");
          #endif
          return;

        }else{

          #ifdef INST_VALID 
          fprintf(stderr,"Instrumenting %s\n",IMG_Name(img).c_str());
          #endif

        }

      }

    }else{

      #ifdef INST_VALID
      fprintf(stderr," (s) ");
      #endif
      return;

    }

  }else{

    #ifdef INST_VALID
    fprintf(stderr," (r) ");
    #endif
    return;

  }
  

  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {

        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {


 
            if( INS_IsStackRead( ins ) || INS_IsStackWrite( ins )){
              
                continue;

            }

            for (UINT32 op = 0; op<INS_MemoryOperandCount(ins); op++){

                           
              UINT32 access = 0;
                     access = (INS_MemoryOperandIsRead(ins,op)    ? READ : 0) |
                              (INS_MemoryOperandIsWritten(ins,op) ? WRITE : 0);


              if( INS_SegmentPrefix(ins) == true ){ 

                continue;

              } 

              
           
              INS_RewriteMemoryOperand(ins, op, REG(REG_INST_G0 + op) );
              INS_InsertCall(ins, IPOINT_BEFORE,
                             AFUNPTR(handleAccess),
                             IARG_THREAD_ID,
                             IARG_MEMORYOP_EA,   op,
                             IARG_UINT32,  INS_MemoryOperandSize(ins,op),
                             IARG_UINT32, access,
                             IARG_INST_PTR,
                             IARG_RETURN_REGS,   REG_INST_G0 + op, 
                             IARG_END);
          
          
            }
        }
    }
}


VOID instrumentImage(IMG img, VOID *v)
{

  #ifdef SHOWIMGLOAD
  fprintf(stderr,"Loading Image: %s\n",IMG_Name(img).c_str());
  #endif

  

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
  RTN_AddInstrumentFunction(instrumentRoutine,0);
  TRACE_AddInstrumentFunction(instrumentTrace,0);

  PIN_AddThreadStartFunction(threadBegin, 0);
  PIN_AddThreadFiniFunction(threadEnd, 0);
  
  PIN_AddFiniFunction(Fini, 0);
  PIN_StartProgram();
  return 0;
}
