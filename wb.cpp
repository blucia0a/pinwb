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


#define BLOCKSIZE 64/*64 Byte Blocks*/
#define BLOCKMASK 0xFFFFFFFFFFFFFFC0 /* ^0b11111111 */
#define INDEXMASK 0x3F /* 0b11111111 */

unsigned long numThreads;
PIN_LOCK numThreadsLock;

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

class unalignedRecord{
  public:
    bool valid;
    bool write;
    ADDRINT unalignedBase1;
    ADDRINT unalignedBase2;
    size_t unalignedSize1;
    size_t unalignedSize2;
    void *unalignedBlock;

};

class thread_data_t
{

public:
  /*max 4 memory operands?*/
  unalignedRecord unaligned[4];
  thread_data_t(){
    for(int i = 0; i < 4; i++){
      unaligned[i].valid = false;
      unaligned[i].write = false;
    }
  }

};

// key for accessing TLS storage in the threads. initialized once in main()
static  TLS_KEY tls_key;

// function to access thread-specific data
thread_data_t* get_tls(THREADID threadid)
{
    thread_data_t* tdata = 
          static_cast<thread_data_t*>(PIN_GetThreadData(tls_key, threadid));
    return tdata;
}



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

  ADDRINT handleUnaligned( THREADID tid, ADDRINT origEA, UINT32 access, UINT32 asize, ADDRINT instadd, UINT32 opno){

    unsigned long index = origEA & INDEXMASK;

    ADDRINT block1Addr = origEA & BLOCKMASK;

    ADDRINT block2Addr = (origEA + asize - 1) & BLOCKMASK;
    
    hash_map<ADDRINT, WriteBufferEntry *>::iterator block1i = this->map.find( block1Addr );
    hash_map<ADDRINT, WriteBufferEntry *>::iterator block2i = this->map.find( block2Addr );
   
    bool block1Found = block1i != this->map.end(); 
    bool block2Found = block2i != this->map.end(); 

    WriteBufferEntry *block1 = NULL;
    if( block1Found ){

      block1 = block1i->second;

    }
    
    WriteBufferEntry *block2 = NULL;
    if( block2Found ){

      block2 = block2i->second;

    }
   
    /*In all cases, we're returning a newly allocated block to the app*/
    ADDRINT retVal = (ADDRINT)malloc(asize);
    memset((void*)retVal,asize,0);
   
    
    /*Now define the base and size of the two regions we need to combine*/
    void *base1 = (void*)origEA;
    void *base2 = (void*)block2Addr;
    
    //size2: overhang into block 2
    //size1: part on the end of block 1 
    size_t size2 = (index + asize) - BLOCKSIZE;
    size_t size1 = asize - size2;

    fprintf(stderr,"Unaligned: %lu bytes @ %016llx + %lu bytes @ %016llx\n",size1,base1,size2,base2);

    /*There are 4 cases*/
    /*In each one we need to copy some bytes from b1 and some from b2*/
    if( !block1Found && !block2Found ){

      /*neither block is in the write buffer*/
      /*Copy from regular memory*/
      memcpy((void*)retVal,base1,size1);
      memcpy((void*)(retVal+size1),base2,size2);
      fprintf(stderr,"%016lx\n",*((unsigned long *)retVal));

    }else if(block1Found && block2Found){

      /*Both blocks are in the write buffer*/
      memcpy((void*)retVal,(void*)block1->realValue,size1);
      memcpy((void*)(retVal+size1),(void*)block2->realValue,size2);
      fprintf(stderr,"%016lx\n",*((unsigned long *)retVal));

    }else if(block1Found && !block2Found){

      memcpy((void*)retVal,(void*)block1->realValue,size1);
      memcpy((void*)(retVal+size1),base2,size2);
      fprintf(stderr,"%016lx\n",*((unsigned long *)retVal));

    }else if(!block1Found && block2Found){

      memcpy((void*)retVal,base1,size1);
      memcpy((void*)(retVal+size1),(void*)block2->realValue,size2);
      fprintf(stderr,"%016lx\n",*((unsigned long *)retVal));

    }

    /*Threads keep their last access so we can delete the block and 
      keep the actual memory up to date*/
    thread_data_t *tdata = get_tls(tid);

    tdata->unaligned[opno].unalignedBlock = (void*)retVal;

    tdata->unaligned[opno].valid = true;

    tdata->unaligned[opno].write = (access == WRITE || access == READWRITE);

    if( tdata->unaligned[opno].write){
      /*For writes, we need the info to copy the block back*/

      if( !block1Found ){
        /*create a new block*/
        /*1: Allocate write buffer block*/
        ADDRINT wbStorage = (ADDRINT)malloc(BLOCKSIZE); 
        
        /*2: Initialize the block.  Mark the asize bytes that will be written dirty*/
        /*Note: this happens in the WriteBufferEntry constructor*/
        WriteBufferEntry *wbe = new WriteBufferEntry(origEA, wbStorage, size1);

        /*3: Link the block into the write buffer structure*/ 
        this->map[ block1Addr ] = wbe;

        tdata->unaligned[opno].unalignedBase1 = wbe->realValue + index;

      }else{

        tdata->unaligned[opno].unalignedBase1 = block1->realValue + index;

      }
      tdata->unaligned[opno].unalignedSize1 = size1;

      if( !block2Found ){

        /*create a new block*/
        /*1: Allocate write buffer block*/
        ADDRINT wbStorage = (ADDRINT)malloc(BLOCKSIZE); 
        
        /*2: Initialize the block.  Mark the asize bytes that will be written dirty*/
        /*Note: this happens in the WriteBufferEntry constructor*/
        WriteBufferEntry *wbe = new WriteBufferEntry(block2Addr, wbStorage, size2);

        /*3: Link the block into the write buffer structure*/ 
        this->map[ block2Addr ] = wbe;
        
        tdata->unaligned[opno].unalignedBase2 = wbe->realValue;

      }else{

        tdata->unaligned[opno].unalignedBase2 = block2->realValue;

      }
      tdata->unaligned[opno].unalignedSize2 = size2;

      fprintf(stderr,"Unaligned: Copying back %lu bytes @ %016llx + %lu bytes @ %016llx\n",tdata->unaligned[opno].unalignedSize1,tdata->unaligned[opno].unalignedBase1,tdata->unaligned[opno].unalignedSize2,tdata->unaligned[opno].unalignedBase2);

    }
   
    return (ADDRINT)retVal;


  }

  ADDRINT handleAccess( THREADID tid, ADDRINT origEA, UINT32 access, UINT32 asize, ADDRINT instadd, UINT32 opno){


      /*Default Behavior - return origEA.*/
      ADDRINT retVal = origEA;

      ADDRINT blockAddr = origEA & BLOCKMASK;
      unsigned long index = origEA & INDEXMASK;

      /*If this fails, the access goes off the end of a block -- trouble!*/
      if( index + asize > BLOCKSIZE ){

        retVal = handleUnaligned(tid, origEA, access, asize, instadd, opno); 

        #ifdef TRACEOPS      
        fprintf(stderr,"UA %s: %016llx <=> B[%016llx] size %d @ %016llx\n",(access==READ?"R":(access==WRITE?"W":"R/W")),origEA,retVal,asize,instadd);
        #endif

        return retVal;

      }

      /*TODO: In the unaligned case, we need to allocate a new piece of memory, put the part of each of the blocks we need into it, pass it back to the program, and then after the access, deallocate it.  That is going to SUCK*/

      if( this->map.find( blockAddr ) == this->map.end() ){
        /*It was not in the write buffer*/

        if( access == READ ){
       
          /*strictly reading a block not in the write buffer -- we're good, just return origEA.  Use program addr.*/ 
          retVal = origEA;

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
        

        if( access == READ ){
          /*0: Consistency check -- better be all valid (in wb) or all invalid (not in wb)*/
          int status = -1;
          for(unsigned i = index; i < index + asize; i++){
            if( status == -1 ){
              status = wbe->valid[i] ? 1 : 0;
            }
            //assert( status == (wbe->valid[i] ? 1 : 0) );
          } 

          if( status == 0 ){

            retVal = origEA;

          }else{

            retVal = wbStorage + index;

          }

        }else{

          retVal = wbStorage + index;

          for(unsigned i = index; i < index + asize; i++){
            wbe->valid[i] = true;
          }

          if(access == READWRITE){

            /*5:Only if we're doing a read/write, copy the existing asize bytes into retVal*/
            memcpy((void*)retVal,(void*)origEA,asize);
            
          }

        }/*end not read*/

      }/*end in wb*/
      
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

VOID cleanupAccess( THREADID tid){

  thread_data_t *tdata = get_tls(tid);
  for(int i = 0; i < 4; i++){

    if(tdata->unaligned[i].valid){

      fprintf(stderr,"Thread %lu cleaning up an unaligned access\n",(unsigned long)tid);

      if(tdata->unaligned[i].write){

        fprintf(stderr,"It was a write!\n");
        memcpy(tdata->unaligned[i].unalignedBlock,
               (void*)(tdata->unaligned[i].unalignedBase1),
               tdata->unaligned[i].unalignedSize1);

        memcpy((void*)((ADDRINT)tdata->unaligned[i].unalignedBlock + 
                                tdata->unaligned[i].unalignedSize1),
               (void*)(tdata->unaligned[i].unalignedBase2),
               tdata->unaligned[i].unalignedSize2);
        
        fprintf(stderr,"Unaligned: Copied back %lu bytes @ %016llx + %lu bytes @ %016llx\n",
                tdata->unaligned[i].unalignedSize1,
                tdata->unaligned[i].unalignedBase1,
                tdata->unaligned[i].unalignedSize2,
                tdata->unaligned[i].unalignedBase2);
      }

      free(tdata->unaligned[i].unalignedBlock);
      tdata->unaligned[i].unalignedBlock = NULL;

    }
    tdata->unaligned[i].valid = false;
    tdata->unaligned[i].write = false;

  } 

}

ADDRINT handleAccess( THREADID tid, ADDRINT origEA, ADDRINT asize, UINT32 access, ADDRINT instadd, UINT32 opno){

   ADDRINT loc = bufs[ tid % NUMBUFS ]->handleAccess( tid, origEA, access, asize, instadd, opno);
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

        /*if(!IMG_IsMainExecutable(img)){

          #ifdef INST_VALID
          fprintf(stderr,".");
          #endif
          return;*/

        //}else{
          #ifdef INST_VALID 
          fprintf(stderr,"Instrumenting %s\n",IMG_Name(img).c_str());
          #endif
        //}

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
              
                //continue;

            }

            for (UINT32 op = 0; op<INS_MemoryOperandCount(ins); op++){

                           
              UINT32 access = 0;
                     access = (INS_MemoryOperandIsRead(ins,op)    ? READ : 0) |
                              (INS_MemoryOperandIsWritten(ins,op) ? WRITE : 0);


              if( INS_SegmentPrefix(ins) == true ){ 

                continue;

              }

              
           
              INS_InsertCall(ins, IPOINT_BEFORE,
                             AFUNPTR(handleAccess),
                             IARG_THREAD_ID,
                             IARG_MEMORYOP_EA,   op,
                             IARG_UINT32,  INS_MemoryOperandSize(ins,op),
                             IARG_UINT32, access,
                             IARG_INST_PTR,
                             IARG_UINT32, op,
                             IARG_RETURN_REGS,   REG_INST_G0 + op, 
                             IARG_END);
              INS_RewriteMemoryOperand(ins, op, REG(REG_INST_G0 + op) );
          
          
            }

            if( INS_HasFallThrough(ins) ){
              INS_InsertCall(ins, IPOINT_AFTER,
                             AFUNPTR(cleanupAccess),
                             IARG_THREAD_ID,
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

    GetLock(&numThreadsLock, threadid+1);
    numThreads++;
    ReleaseLock(&numThreadsLock);

    thread_data_t* tdata = new thread_data_t;

    PIN_SetThreadData(tls_key, tdata, threadid);


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

 // Initialize the lock
  InitLock(&numThreadsLock);

    // Obtain  a key for TLS storage.
  tls_key = PIN_CreateThreadDataKey(0);

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
