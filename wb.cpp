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
#define TRACEOPS
#undef NOFLUSH
#undef SHOWIMGLOAD
#define NUMBUFS 64

#define READ 1
#define WRITE 2
#define READWRITE 3


#define BLOCKSIZE 256 /*64 Byte Blocks*/
#define BLOCKMASK 0xFFFFFFFFFFFFFF00 /* ^0b11111111 */
#define INDEXMASK 0xFF /* 0b11111111 */

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
  bool valid[BLOCKSIZE];

  
  WriteBufferEntry(){
    this->realAddr = 0;
    this->realValue = 0;
    this->size = 0;
    for(int i = 0; i < BLOCKSIZE; i++){
      this->valid[i] = false;
    }
  }

  WriteBufferEntry(ADDRINT a, ADDRINT v, UINT32 size){
    this->realAddr = a & BLOCKMASK;
    this->realValue = v;
    this->size = size;

    for(int i = 0; i < BLOCKSIZE; i++){
      this->valid[i] = false;
    }

    unsigned index = a & INDEXMASK;
    if( index + size > BLOCKSIZE ){
      fprintf(stderr,"%u + %u > %d\n",index,size,BLOCKSIZE);
      assert( "Unaligned Access!" && ((index + size) <= BLOCKSIZE) );
    }
    for(unsigned i = index; i < index + size; i++){
      this->valid[i] = true;
    }

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
      for(int v = 0; v < BLOCKSIZE; v++){
        if( wbe->valid[v] ){
          memcpy((void*)(wbe->realAddr + v), (void*)(wbe->realValue + v), 1);
        }
      }
      delete wbe;

    }

    this->map.clear();

  }

  ADDRINT handleAccess( ADDRINT origEA, UINT32 access, UINT32 asize, ADDRINT instadd){


      /*Default Behavior - return origEA.*/
      ADDRINT retVal = origEA;

      
      ADDRINT blockAddr = origEA & BLOCKMASK;
      unsigned long index = origEA & INDEXMASK;

      /*If this fails, the access goes off the end of a block -- trouble!*/
      if( index + asize > BLOCKSIZE ){
        fprintf(stderr,"%lu + %u > %d\n",index,asize,BLOCKSIZE);
        assert( "Unaligned Access!" && ((index + asize) <= BLOCKSIZE) );
      }

      if( this->map.find( blockAddr ) == this->map.end() ){
        /*It was not in the write buffer*/

        if( access == READ ){
        
          /*strictly reading a block not in the write buffer -- we're good, just return origEA.  Use program addr.*/ 
          return origEA;

        }else {

          /*It was a Write*/

          /*1: Allocate write buffer block*/
          ADDRINT wbStorage = (ADDRINT)malloc(BLOCKSIZE); 
        
          /*2: Initialize the block.  Mark the asize bytes that will be written dirty*/
          /*Note: this happens in the WriteBufferEntry constructor*/
          WriteBufferEntry *wbe = new WriteBufferEntry(origEA, wbStorage, asize);

          /*3: Link the block into the write buffer structure*/ 
          this->map[ blockAddr ] = wbe;

          /*4: Get the pointer into the wbStorage that we need to return*/
          retVal = wbStorage + index; 
         
          if(access == READWRITE){

            /*5:Only if we're doing a read/write, copy the existing asize bytes into retVal*/
            memcpy((void*)retVal,(void*)origEA,asize);
            
          }

        }


      }else{

        /*This case means we're accessing something for 
          which the block was found in the write buffer*/
        WriteBufferEntry *wbe = this->map[ blockAddr ];
        ADDRINT wbStorage = wbe->realValue;
        
        /*0: Consistency check -- better be all valid (in wb) or all invalid (not in wb)*/
        int status = -1;
        for(unsigned i = index; i < index + asize; i++){
          if( status == -1 ){
            status = wbe->valid[i] ? 1 : 0;
          }
          assert( status == (wbe->valid[i] ? 1 : 0) );
        } 

        /*If this fails, the access goes off the end of a block -- trouble!*/
        if( index+asize > BLOCKSIZE ){
          fprintf(stderr,"%lu + %u > %d\n",index,asize,BLOCKSIZE);
          assert( "Unaligned Access!" && ((index + asize) <= BLOCKSIZE) );
        }

        retVal = wbStorage + index;

      }
      
      #ifdef TRACEOPS      
      fprintf(stderr,"%s: %016llx <=> B[%016llx] size %d @ %016llx\n",(access==READ?"R":(access==WRITE?"W":"R/W")),origEA,retVal,asize,instadd);
      #endif
      return retVal;


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
  fprintf(stderr,"Oooooooooooooo!\n");
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
