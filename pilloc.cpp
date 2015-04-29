#include "pin.H"
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <syscall.h>

/* ===================================================================== */
/* Names of malloc and free */
/* ===================================================================== */
#define CALLOC "calloc"
#define MALLOC "malloc"
#define FREE "free"
#define REALLOC "realloc"

using namespace std;

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

class State;
class Block;
class Empty;

void generate_html(void);

string ADDRINTToString (ADDRINT a)
{
    ostringstream temp;
    temp << "0x" << hex <<a;
    return temp.str();
}

string IntToString (int a)
{
    ostringstream temp;
    temp<<a;
    return temp.str();
}

int RandU(int nMin, int nMax)
{
    return nMin + (int)((double)rand() / (RAND_MAX) * (nMax-nMin+1));
}

std::ofstream TraceFile;

int unit_width = 10;
set<ADDRINT> boundaries;
vector<State> timeline;

class Block
{
public:
    Block();
    ~Block();
    ADDRINT start();
    ADDRINT end();
    string gen_html(int);
    string color;
    string get_color();
    int header;
    int footer;
    int round;
    int minsz;
    bool error;
    ADDRINT addr;
    ADDRINT size;
};

Block::Block()
{
    this->header = 8;
    this->footer = 0;
    this->round = 0x10;
    this->minsz = 0x20;
    this->error = false;
    this->color = this->get_color();
    this->size = 0;
    this->addr = 0;
}

Block::~Block()
{

}

ADDRINT Block::start()
{
    return this->addr - this->header;
}

ADDRINT Block::end()
{
    ADDRINT size = max((ADDRINT) this->minsz, this->size + this->header + this->footer);
    ADDRINT rsize = size + (this->round - 1);
    rsize = rsize - (rsize % this->round);
    return this->addr - this->header + rsize;
}

string Block::get_color()
{
    string color = "background-color: rgb(";
    color += IntToString(RandU(0,255));
    color+=", ";
    color+= IntToString(RandU(0,255));
    color+=", ";
    color += IntToString(RandU(0,255));
    color+=");";
    return color;
}

string Block::gen_html(int width)
{
    string out = "";
    string color = this->color;
    if(this->error){
        color += "background-image: repeating-linear-gradient(120deg, transparent, transparent 1.40em, #A85860 1.40em, #A85860 2.80em);";
    }

    out+="<div class=\"block normal\" style=\"width: ";
    out+= IntToString(unit_width * width);
    out+="em;";
    out+=color;
    out+=";\">";
    out+="<strong>";
    out+= ADDRINTToString(this->start());
    out+="</strong><br />";
    out+="+ ";
    out+= ADDRINTToString(this->end() - this->start());
    out+=" (";
    out+= ADDRINTToString(this->size);
    out+=")";

    out+="</div>\n";
    return out;
}


class State
{
public:
    State(vector<Block>* old_blocks);
    ~State();
    set<ADDRINT>* boundaries();
    vector<Block>* blocks;
    string errors;
    string info;
    ADDRINT toRealloc;
};

State::State(vector<Block>* old_blocks)
{
    this->blocks = new vector<Block>();
    if(!old_blocks->empty()){
        this->blocks->insert(this->blocks->end(),old_blocks->begin(),old_blocks->end());
    }
    this->toRealloc = 0;
    this->info = "";
    this->errors = "";
}

State::~State()
{

}

set<ADDRINT>* State::boundaries()
{
    set<ADDRINT>* bounds = new set<ADDRINT>();
    for(vector<Block>::iterator block = this->blocks->begin(); block != this->blocks->end(); ++block){
        bounds->insert(block->start());
        bounds->insert(block->end());
    }
    return bounds;
}


class Empty
{
public:
    Empty();
    ADDRINT start;
    ADDRINT end;
    bool display;
    string gen_html(int);
};

Empty::Empty(){

}

string Empty::gen_html(int width){
    string out = "<div class=\"";
    out += "block empty\" style=\"width: ";
    out += IntToString(10);
    out+="em; ";
    out+=";\">";
    out+= "<strong>";
    out+=ADDRINTToString(this->start);
    out+="</strong><br />";
    out += "+ " + ADDRINTToString(this->end - this->start);
    out+="</div>";
    return out;
}
/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "malloctrace.out", "specify trace file name");

/* ===================================================================== */

Block* MatchPtr(State state, ADDRINT addr,int *s)
{
    *s = 0;
    if(!addr){
        //set everything NULL
        return NULL;
    }
    Block* match = NULL;
    int i = 0;
    for(vector<Block>::iterator block = state.blocks->begin(); block != state.blocks->end(); ++block){
        if(block->addr == addr){
            if(match == NULL or match->size >= block->size){
                match = &(*block);
                *s = i;
            }
        }
        i++;
    }

    if(!match){
        state.errors.append("Couldn't find block at " + ADDRINTToString(addr));
    }
    return match;

}

/* ===================================================================== */
/* Analysis routines                                                     */
/* ===================================================================== */

VOID update_boundaries(State* state){
    set<ADDRINT>* bounds = state->boundaries();
    boundaries.insert(bounds->begin(),bounds->end());
}

VOID SysBefore(ADDRINT ip, ADDRINT num, ADDRINT arg0, ADDRINT arg1, ADDRINT arg2, ADDRINT arg3, ADDRINT arg4, ADDRINT arg5)
{
    if(num == 12){
        printf("brk(addr=0x%lx)",
            (unsigned long)arg0);
    } else if(num == 9){
        printf("mmap(addr=0x%lx,len=0x%lx,prot=0x%lx,flags=0x%lx,fd=0x%lx,off=0x%lx)",
            arg0,
            arg1,
            arg2,
            arg3,
            arg4,
            arg5);
    }
}

// Print the return value of the system call
VOID SysAfter(ADDRINT ret)
{
    printf("returns: 0x%lx\n", (unsigned long)ret);
}

VOID SyscallEntry(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v)
{
    SysBefore(PIN_GetContextReg(ctxt, REG_INST_PTR),
        PIN_GetSyscallNumber(ctxt, std),
        PIN_GetSyscallArgument(ctxt, std, 0),
        PIN_GetSyscallArgument(ctxt, std, 1),
        PIN_GetSyscallArgument(ctxt, std, 2),
        PIN_GetSyscallArgument(ctxt, std, 3),
        PIN_GetSyscallArgument(ctxt, std, 4),
        PIN_GetSyscallArgument(ctxt, std, 5));
}

VOID SyscallExit(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v)
{
    SysAfter(PIN_GetSyscallReturn(ctxt, std));
}

VOID BeforeMalloc(CHAR * name, ADDRINT size)
{
    State* state = new State(timeline.back().blocks);
    Block* b = new Block();
    b->size = size;
    state->blocks->push_back(*b);
    timeline.push_back(*state);
}

VOID BeforeFree(CHAR * name, ADDRINT addr)
{
    if(!addr) return;
    State* state = new State(timeline.back().blocks);
    int* s = (int*) malloc(sizeof(int));
    Block* match = MatchPtr(*state,addr,s);
    if(match){
        state->blocks->erase(state->blocks->begin()+*s);
    }
    state->info.append("free(" + ADDRINTToString(addr) +")");
    timeline.push_back(*state);
    update_boundaries(state);
}

VOID BeforeCalloc(CHAR * name, ADDRINT num, ADDRINT size)
{
    vector<Block>* blocks = timeline.back().blocks;
    State* state = new State(blocks);
    Block* b = new Block();
    b->size = num * size;
    state->blocks->push_back(*b);
    timeline.push_back(*state);
}

VOID MallocAfter(ADDRINT ret)
{
    State* state = &timeline.back();
    Block *b = &(state->blocks->back());
    if(!ret){
        state->errors+= "Failed to allocate " + ADDRINTToString(b->size) + " bytes.";
        state->blocks->erase(state->blocks->end());
    } else {
        b->addr = ret;
    }
    state->info.append("malloc(" + ADDRINTToString(b->size) +") = " + ADDRINTToString(ret));
    update_boundaries(state);
}

VOID CallocAfter(ADDRINT ret)
{
    State* state = &timeline.back();
    Block *b = &(state->blocks->back());
    if(!ret){
        state->errors+= "Failed to allocate " + ADDRINTToString(b->size) + " bytes.";
        state->blocks->erase(state->blocks->end());
    } else {
        b->addr = ret;
        state->info.append("calloc(" + ADDRINTToString(b->size) +") = " + ADDRINTToString(ret));
    }
    update_boundaries(state);
}

VOID BeforeRealloc(CHAR * name, ADDRINT addr, ADDRINT size)
{
    if(!addr){ //effectively a malloc
        BeforeMalloc(name,size);
    } else if(!size){ //effectively a free
        BeforeFree(name,addr);
    } else {
        State* state = new State(timeline.back().blocks);
        timeline.push_back(*state);
        state->toRealloc = addr;
        Block* block = new Block();
        block->size = size;
        state->blocks->push_back(*block);
    }
}

VOID ReallocAfter(ADDRINT ret)
{
    State* state = &timeline.back();
    Block* block = &(state->blocks->back());
    if(!state->toRealloc){
        MallocAfter(ret);
    } else if(block->size) {
        int* s = (int*) malloc(sizeof(int));
        Block* match = MatchPtr(*state,state->toRealloc,s);
        state->toRealloc = 0;
        if(!match){
            state->blocks->erase(state->blocks->end());
            return;
        }
        if(!ret){
            block->addr = match->addr;
            block->size = match->size;
            block->error = true;
            block->color = match->color;
            state->errors.append("failed to realloc(" + ADDRINTToString(match->addr) + "," + ADDRINTToString(match->size) + ")");
        } else {
            block->addr = ret;
            Block newBlock = state->blocks->at(*s);
            newBlock.size = block->size;
            newBlock.addr=ret;
            newBlock.color = match->color;
            state->blocks->erase(state->blocks->end());
            state->info.append("realloc(" + ADDRINTToString(match->addr) + "," + ADDRINTToString(newBlock.size) +") = " + ADDRINTToString(ret));
        }
        update_boundaries(state);
    }
}

/* ===================================================================== */
/* Instrumentation routines                                              */
/* ===================================================================== */
   
VOID Image(IMG img, VOID *v)
{
    // Instrument the malloc() and free() functions.  Print the input argument
    // of each malloc() or free(), and the return value of malloc().
    //
    //  Find the malloc() function.
    RTN mallocRtn = RTN_FindByName(img, MALLOC);
    if (RTN_Valid(mallocRtn))
    {
        RTN_Open(mallocRtn);

        // Instrument malloc() to print the input argument value and the return value.
        RTN_InsertCall(mallocRtn, IPOINT_BEFORE, (AFUNPTR)BeforeMalloc,
                       IARG_ADDRINT, MALLOC,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_END);
        RTN_InsertCall(mallocRtn, IPOINT_AFTER, (AFUNPTR)MallocAfter,
                       IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);

        RTN_Close(mallocRtn);
    }

    // Find the free() function.
    RTN freeRtn = RTN_FindByName(img, FREE);
    if (RTN_Valid(freeRtn))
    {
        RTN_Open(freeRtn);
        // Instrument free() to print the input argument value.
        RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR)BeforeFree,
                       IARG_ADDRINT, FREE,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_END);

        RTN_Close(freeRtn);
    }

    //Find the calloc() function
    RTN callocRtn = RTN_FindByName(img, CALLOC);
    if (RTN_Valid(callocRtn))
    {
        RTN_Open(callocRtn);

        // Instrument callocRtn to print the input argument value and the return value.
        RTN_InsertCall(callocRtn, IPOINT_BEFORE, (AFUNPTR)BeforeCalloc,
                       IARG_ADDRINT, CALLOC,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                       IARG_END);
        RTN_InsertCall(callocRtn, IPOINT_AFTER, (AFUNPTR)CallocAfter,
                       IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);

        RTN_Close(callocRtn);
    }
    //Find the realloc() function
    RTN reallocRtn = RTN_FindByName(img, REALLOC);
    if (RTN_Valid(reallocRtn))
    {
        RTN_Open(reallocRtn);

        // Instrument malloc() to print the input argument value and the return value.
        RTN_InsertCall(reallocRtn, IPOINT_BEFORE, (AFUNPTR)BeforeRealloc,
                       IARG_ADDRINT, REALLOC,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                       IARG_END);
        RTN_InsertCall(reallocRtn, IPOINT_AFTER, (AFUNPTR)ReallocAfter,
                       IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);

        RTN_Close(reallocRtn);
    }
}

/* ===================================================================== */

VOID Fini(INT32 code, VOID *v)
{
    generate_html();
    TraceFile.close();
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
   
INT32 Usage()
{
    cerr << "This tool produces a trace of calls to malloc." << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

void print_state(State state)
{
    TraceFile << "<div class=\"state ";
    if(state.errors.size()){
        TraceFile << "error";
    }
    TraceFile << "\">\n" << endl;

    set<int> known_stops;

    vector<Block> todo;
    todo.insert(todo.end(),state.blocks->begin(),state.blocks->end());

    TraceFile << "<div class=\"line\" style=\"\">\n" << endl;
    Block* current = NULL;
    int i = 0;
    int last = 0;

    for(set<ADDRINT>::iterator b = boundaries.begin(); b!=boundaries.end();++b){
        if(!current){
            for(vector<Block>::iterator block = state.blocks->begin();block != state.blocks->end();++block){
                if(block->start() == *b){
                    current = &(*block);
                    if(last != i){
                        Empty* emp = new Empty();
                        set<ADDRINT>::iterator it = boundaries.begin();
                        advance(it, last);
                        emp->start = *it;
                        emp->end = *b;
                        TraceFile << emp->gen_html(i - last) << endl;
                        delete emp;
                        last = i;
                    }
                }
            }
        }
        if(!current){
            //don't care
        }else if(current->end() == *b){
            TraceFile << current->gen_html(i - last) << endl;
            last = i;
            current = NULL;
        }
        i++;
    }
    TraceFile << "</div>\n" << endl;
    TraceFile << "<div class=\"log\">" << endl;
    //TODO html escape
    TraceFile << "<p>" << state.info << "</p>" << endl;
    
    TraceFile << "<p>" << state.errors << "</p>" << endl;

    TraceFile << "</div>\n" << endl;

    TraceFile << "</div>\n" << endl;
}
void generate_html()
{
    TraceFile << "<style>" << endl;
    TraceFile << "body {\n"
"font-size: 12px;\n"
"background-color: #EBEBEB;\n"
"font-family: \"Lucida Console\", Monaco, monospace;\n"
"width: "
<< IntToString((boundaries.size() - 1) * (unit_width+1)) <<
"em;\n"
"}"
<< endl;

    TraceFile << "p {\n"
"margin: 0.8em 0 0 0.1em;\n"
"}" << endl;

    TraceFile << ".block {\n"
"float: left;\n"
"padding: 0.5em 0;\n"
"text-align: center;\n"
"color: black;\n"
"}" << endl;

    TraceFile << ".normal {\n"
"-webkit-box-shadow: 2px 2px 4px 0px rgba(0,0,0,0.80);\n"
"-moz-box-shadow: 2px 2px 4px 0px rgba(0,0,0,0.80);\n"
"box-shadow: 2px 2px 4px 0px rgba(0,0,0,0.80);\n"
"}" << endl;

    TraceFile << ".empty + .empty {\n"
"border-left: 1px solid gray;\n"
"margin-left: -1px;\n"
"}" << endl;

    TraceFile << ".empty {\n"
"color: gray;\n"
"}" << endl;

    TraceFile << ".line {  }" << endl;

    TraceFile << ".line:after {\n"
  "content:\"\";\n"
  "display:table;\n"
  "clear:both;\n"
"}" << endl;

    TraceFile << ".state {\n"
"margin: 0.5em; padding: 0;\n"
"background-color: white;\n"
"border-radius: 0.3em;\n"
"-webkit-box-shadow: inset 2px 2px 4px 0px rgba(0,0,0,0.80);\n"
"-moz-box-shadow: inset 2px 2px 4px 0px rgba(0,0,0,0.80);\n"
"box-shadow: inset 2px 2px 4px 0px rgba(0,0,0,0.80);\n"
"padding: 0.5em;\n"
"}" << endl;

    TraceFile << ".log {"
"}" << endl;

    TraceFile << ".error {"
"color: white;\n"
"background-color: #8b1820;\n"
"}" << endl;

    TraceFile << ".error .empty {\n"
"color: white;\n"
"}" << endl;

    TraceFile << "</style>\n" << endl;

    TraceFile << "<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/2.1.3/jquery.min.js\"></script>" << endl;
    TraceFile << "<script>"
"var scrollTimeout = null;\n"
"$(window).scroll(function(){\n"
    "if (scrollTimeout) clearTimeout(scrollTimeout);\n"
    "scrollTimeout = setTimeout(function(){\n"
    "$('.log').stop();\n"
    "$('.log').animate({\n"
     "   'margin-left': $(this).scrollLeft()\n"
    "}, 100);\n"
    "}, 200);\n"
"});\n"
"</script>"<< endl;

    TraceFile << "<body>\n" << endl;

    TraceFile << "<div class=\"timeline\">\n" << endl;
    printf("Timeline length:%lu\n",timeline.size());
    for(vector<State>::const_iterator state = timeline.begin(); state != timeline.end(); ++state){
        print_state(*state);
    }

    TraceFile << "</div>\n" << endl;

    TraceFile << "</body>\n" << endl;

}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[])
{
    // Initialize pin & symbol manager
    PIN_InitSymbols();
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }
    
    // Write to a file since cout and cerr maybe closed by the application
    TraceFile.open(KnobOutputFile.Value().c_str());
    TraceFile << hex;
    TraceFile.setf(ios::showbase);
    vector<Block>* blocks = new std::vector<Block>();
    State* initial = new State(blocks);
    timeline.push_back(*initial);
    // Register Image to be called to instrument functions.
    PIN_AddSyscallEntryFunction(SyscallEntry, 0);
    PIN_AddSyscallExitFunction(SyscallExit, 0);

    IMG_AddInstrumentFunction(Image, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();
    
    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
