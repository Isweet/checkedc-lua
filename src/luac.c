/*
** $Id: luac.c,v 1.76 2018/06/19 01:32:02 lhf Exp $
** Lua compiler (saves bytecodes to files; also lists bytecodes)
** See Copyright Notice in lua.h
*/

#define luac_c
#define LUA_CORE

#include "lprefix.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "lobject.h"
#include "lstate.h"
#include "lundump.h"

static void PrintFunction(const Proto *f : itype(_Ptr<const Proto> ), int full);
#define luaU_print	PrintFunction

#define PROGNAME	"luac"		/* default program name */
#define OUTPUT		PROGNAME ".out"	/* default output file */

static int listing=0;			/* list bytecodes? */
static int dumping=1;			/* dump bytecodes? */
static int stripping=0;			/* strip debug information? */
static _Ptr<char> Output = { OUTPUT };	/* default output file name */
static const char* output=Output;	/* actual output file name */
static const char* progname=PROGNAME;	/* actual program name */

static void fatal(const char *message : itype(_Ptr<const char> ))
{
 fprintf(stderr,((const char *)"%s: %s\n"),progname,message);
 exit(EXIT_FAILURE);
}

static void cannot(_Ptr<const char> what)
{
 fprintf(stderr,((const char *)"%s: cannot %s %s: %s\n"),progname,what,output,strerror(errno));
 exit(EXIT_FAILURE);
}

static void usage(const char *message : itype(_Ptr<const char> ))
{
 if (*message=='-')
  fprintf(stderr,((const char *)"%s: unrecognized option '%s'\n"),progname,message);
 else
  fprintf(stderr,((const char *)"%s: %s\n"),progname,message);
 fprintf(stderr,
  ((const char *)"usage: %s [options] [filenames]\n"
  "Available options are:\n"
  "  -l       list (use -l -l for full listing)\n"
  "  -o name  output to file 'name' (default is \"%s\")\n"
  "  -p       parse only\n"
  "  -s       strip debug information\n"
  "  -v       show version information\n"
  "  --       stop handling options\n"
  "  -        stop handling options and process stdin\n")
  ,progname,Output);
 exit(EXIT_FAILURE);
}

#define IS(s)	(strcmp(argv[i],s)==0)

static int doargs(int argc, char *argv[])
{
 int i;
 int version=0;
 if (argv[0]!=NULL && *argv[0]!=0) progname=argv[0];
 for (i=1; i<argc; i++)
 {
  if (*argv[i]!='-')			/* end of options; keep it */
   break;
  else if (IS("--"))			/* end of options; skip it */
  {
   ++i;
   if (version) ++version;
   break;
  }
  else if (IS("-"))			/* end of options; use stdin */
   break;
  else if (IS("-l"))			/* list */
   ++listing;
  else if (IS("-o"))			/* output file */
  {
   output=argv[++i];
   if (output==NULL || *output==0 || (*output=='-' && output[1]!=0))
    usage("'-o' needs argument");
   if (IS("-")) output=NULL;
  }
  else if (IS("-p"))			/* parse only */
   dumping=0;
  else if (IS("-s"))			/* strip debug information */
   stripping=1;
  else if (IS("-v"))			/* show version */
   ++version;
  else					/* unknown option */
   usage(argv[i]);
 }
 if (i==argc && (listing || !dumping))
 {
  dumping=0;
  argv[--i]=Output;
 }
 if (version)
 {
  printf(((const char *)"%s\n"),LUA_COPYRIGHT);
  if (version==argc-1) exit(EXIT_SUCCESS);
 }
 return i;
}

#define FUNCTION "(function()end)();"

static const char * reader(lua_State *L, void *ud, _Ptr<size_t> size)
{
 UNUSED(L);
 if ((*(int*)ud)--)
 {
  *size=sizeof(FUNCTION)-1;
  return FUNCTION;
 }
 else
 {
  *size=0;
  return NULL;
 }
}

#define toproto(L,i) getproto(L->top+(i))

static const Proto * combine(lua_State *L, int n)
{
 if (n==1)
  return toproto(L,-1);
 else
 {
  Proto* f;
  int i=n;
  if (lua_load(L,reader,&i,((const char *)"=(" PROGNAME ")"),NULL)!=LUA_OK) fatal(lua_tostring(L,-1));
  f=toproto(L,-1);
  for (i=0; i<n; i++)
  {
   f->p[i]=toproto(L,i-n-1);
   if (f->p[i]->sizeupvalues>0) f->p[i]->upvalues[0].instack=0;
  }
  f->sizelineinfo=0;
  return f;
 }
}

static int writer(lua_State *L, const void *p, size_t size, void *u)
{
 UNUSED(L);
 return (fwrite(p,size,1,(FILE*)u)!=1) && (size!=0);
}

static int pmain(lua_State *L)
{
 int argc=(int)lua_tointeger(L,1);
 char** argv=(char**)lua_touserdata(L,2);
 const Proto* f;
 int i;
 if (!lua_checkstack(L,argc)) fatal("too many input files");
 for (i=0; i<argc; i++)
 {
  const char* filename=IS("-") ? NULL : argv[i];
  if (luaL_loadfile(L,filename)!=LUA_OK) fatal(lua_tostring(L,-1));
 }
 f=combine(L,argc);
 if (listing) luaU_print(f,listing>1);
 if (dumping)
 {
  FILE* D= (output==NULL) ? stdout : fopen(output,((const char *)"wb"));
  if (D==NULL) cannot("open");
  lua_lock(L);
  luaU_dump(L,f,writer,D,stripping);
  lua_unlock(L);
  if (ferror(D)) cannot("write");
  if (fclose(D)) cannot("close");
 }
 return 0;
}

int main(int argc, char *argv[])
{
 lua_State* L;
 int i=doargs(argc,argv);
 argc-=i; argv+=i;
 if (argc<=0) usage("no input files given");
 L=luaL_newstate();
 if (L==NULL) fatal("cannot create state: not enough memory");
 lua_pushcfunction(L,&pmain);
 lua_pushinteger(L,argc);
 lua_pushlightuserdata(L,argv);
 if (lua_pcall(L,2,0,0)!=LUA_OK) fatal(lua_tostring(L,-1));
 lua_close(L);
 return EXIT_SUCCESS;
}

/*
** $Id: luac.c,v 1.76 2018/06/19 01:32:02 lhf Exp $
** print bytecodes
** See Copyright Notice in lua.h
*/

#include <ctype.h>
#include <stdio.h>

#define luac_c
#define LUA_CORE

#include "ldebug.h"
#include "lobject.h"
#include "lopcodes.h"

#define VOID(p)		((const void*)(p))

static void PrintString(const TString *ts)
{
 const char* s=getstr(ts);
 size_t i,n=tsslen(ts);
 printf(((const char *)"%c"),'"');
 for (i=0; i<n; i++)
 {
  int c=(int)(unsigned char)s[i];
  switch (c)
  {
   case '"':  printf(((const char *)"\\\"")); break;
   case '\\': printf(((const char *)"\\\\")); break;
   case '\a': printf(((const char *)"\\a")); break;
   case '\b': printf(((const char *)"\\b")); break;
   case '\f': printf(((const char *)"\\f")); break;
   case '\n': printf(((const char *)"\\n")); break;
   case '\r': printf(((const char *)"\\r")); break;
   case '\t': printf(((const char *)"\\t")); break;
   case '\v': printf(((const char *)"\\v")); break;
   default:	if (isprint(c))
   			printf(((const char *)"%c"),c);
		else
			printf(((const char *)"\\%03d"),c);
  }
 }
 printf(((const char *)"%c"),'"');
}

static void PrintConstant(_Ptr<const Proto> f, int i)
{
 _Ptr<const TValue> o = &f->k[i];
 switch (ttype(o))
 {
  case LUA_TNIL:
	printf(((const char *)"nil"));
	break;
  case LUA_TBOOLEAN:
	printf(bvalue(o) ? "true" : "false"));
	break;
  case LUA_TNUMFLT:
	{
	char buff[100];
	sprintf(buff,LUA_NUMBER_FMT,fltvalue(o));
	printf(((const char *)"%s"),buff);
	if (buff[strspn(buff,((const char *)"-0123456789"))]=='\0') printf(((const char *)".0"));
	break;
	}
  case LUA_TNUMINT:
	printf(LUA_INTEGER_FMT,ivalue(o));
	break;
  case LUA_TSHRSTR: case LUA_TLNGSTR:
	PrintString(tsvalue(o));
	break;
  default:				/* cannot happen */
	printf(((const char *)"? type=%d"),ttype(o));
	break;
 }
}

#define UPVALNAME(x) ((f->upvalues[x].name) ? getstr(f->upvalues[x].name) : "-")
#define MYK(x)		(-1-(x))

static void PrintCode(_Ptr<const Proto> f)
{
 const Instruction* code=f->code;
 int pc,n=f->sizecode;
 for (pc=0; pc<n; pc++)
 {
  Instruction i=code[pc];
  OpCode o=GET_OPCODE(i);
  int a=GETARG_A(i);
  int b=GETARG_B(i);
  int c=GETARG_C(i);
  int ax=GETARG_Ax(i);
  int bx=GETARG_Bx(i);
  int sbx=GETARG_sBx(i);
  int line=getfuncline(f,pc);
  printf(((const char *)"\t%d\t"),pc+1);
  if (line>0) printf(((const char *)"[%d]\t"),line); else printf(((const char *)"[-]\t"));
  printf(((const char *)"%-9s\t"),luaP_opnames[o]);
  switch (getOpMode(o))
  {
   case iABC:
    printf(((const char *)"%d"),a);
    if (getBMode(o)!=OpArgN) printf(((const char *)" %d"),ISK(b) ? (MYK(INDEXK(b))) : b);
    if (getCMode(o)!=OpArgN) printf(((const char *)" %d"),ISK(c) ? (MYK(INDEXK(c))) : c);
    break;
   case iABx:
    printf(((const char *)"%d"),a);
    if (getBMode(o)==OpArgK) printf(((const char *)" %d"),MYK(bx));
    if (getBMode(o)==OpArgU) printf(((const char *)" %d"),bx);
    break;
   case iAsBx:
    printf(((const char *)"%d %d"),a,sbx);
    break;
   case iAx:
    printf(((const char *)"%d"),MYK(ax));
    break;
  }
  switch (o)
  {
   case OP_LOADK:
    printf(((const char *)"\t; ")); PrintConstant(f,bx);
    break;
   case OP_GETUPVAL:
   case OP_SETUPVAL:
    printf(((const char *)"\t; %s"),UPVALNAME(b));
    break;
   case OP_GETTABUP:
    printf(((const char *)"\t; %s"),UPVALNAME(b));
    if (ISK(c)) { printf(((const char *)" ")); PrintConstant(f,INDEXK(c)); }
    break;
   case OP_SETTABUP:
    printf(((const char *)"\t; %s"),UPVALNAME(a));
    if (ISK(b)) { printf(((const char *)" ")); PrintConstant(f,INDEXK(b)); }
    if (ISK(c)) { printf(((const char *)" ")); PrintConstant(f,INDEXK(c)); }
    break;
   case OP_GETTABLE:
   case OP_SELF:
    if (ISK(c)) { printf(((const char *)"\t; ")); PrintConstant(f,INDEXK(c)); }
    break;
   case OP_SETTABLE:
   case OP_ADD:
   case OP_SUB:
   case OP_MUL:
   case OP_MOD:
   case OP_POW:
   case OP_DIV:
   case OP_IDIV:
   case OP_BAND:
   case OP_BOR:
   case OP_BXOR:
   case OP_SHL:
   case OP_SHR:
   case OP_EQ:
   case OP_LT:
   case OP_LE:
    if (ISK(b) || ISK(c))
    {
     printf(((const char *)"\t; "));
     if (ISK(b)) PrintConstant(f,INDEXK(b)); else printf(((const char *)"-"));
     printf(((const char *)" "));
     if (ISK(c)) PrintConstant(f,INDEXK(c)); else printf(((const char *)"-"));
    }
    break;
   case OP_JMP:
   case OP_FORLOOP:
   case OP_FORPREP:
   case OP_TFORLOOP:
    printf(((const char *)"\t; to %d"),sbx+pc+2);
    break;
   case OP_CLOSURE:
    printf(((const char *)"\t; %p"),VOID(f->p[bx]));
    break;
   case OP_SETLIST:
    if (c==0) printf(((const char *)"\t; %d"),(int)code[++pc]); else printf(((const char *)"\t; %d"),c);
    break;
   case OP_EXTRAARG:
    printf(((const char *)"\t; ")); PrintConstant(f,ax);
    break;
   default:
    break;
  }
  printf(((const char *)"\n"));
 }
}

#define SS(x)	((x==1)?"":"s")
#define S(x)	(int)(x),SS(x)

static void PrintHeader(_Ptr<const Proto> f)
{
 const char* s=f->source ? getstr(f->source) : "=?";
 if (*s=='@' || *s=='=')
  s++;
 else if (*s==LUA_SIGNATURE[0])
  s="(bstring)";
 else
  s="(string)";
 printf(((const char *)"\n%s <%s:%d,%d> (%d instruction%s at %p)\n"),
 	(f->linedefined==0)?"main":"function",s,
	f->linedefined,f->lastlinedefined,
	S(f->sizecode),VOID(f));
 printf(((const char *)"%d%s param%s, %d slot%s, %d upvalue%s, "),
	(int)(f->numparams),f->is_vararg?"+":"",SS(f->numparams),
	S(f->maxstacksize),S(f->sizeupvalues));
 printf(((const char *)"%d local%s, %d constant%s, %d function%s\n"),
	S(f->sizelocvars),S(f->sizek),S(f->sizep));
}

static void PrintDebug(_Ptr<const Proto> f)
{
 int i,n;
 n=f->sizek;
 printf(((const char *)"constants (%d) for %p:\n"),n,VOID(f));
 for (i=0; i<n; i++)
 {
  printf(((const char *)"\t%d\t"),i+1);
  PrintConstant(f,i);
  printf(((const char *)"\n"));
 }
 n=f->sizelocvars;
 printf(((const char *)"locals (%d) for %p:\n"),n,VOID(f));
 for (i=0; i<n; i++)
 {
  printf(((const char *)"\t%d\t%s\t%d\t%d\n"),
  i,getstr(f->locvars[i].varname),f->locvars[i].startpc+1,f->locvars[i].endpc+1);
 }
 n=f->sizeupvalues;
 printf(((const char *)"upvalues (%d) for %p:\n"),n,VOID(f));
 for (i=0; i<n; i++)
 {
  printf(((const char *)"\t%d\t%s\t%d\t%d\n"),
  i,UPVALNAME(i),f->upvalues[i].instack,f->upvalues[i].idx);
 }
}

static void PrintFunction(const Proto *f : itype(_Ptr<const Proto> ), int full)
{
 int i,n=f->sizep;
 PrintHeader(f);
 PrintCode(f);
 if (full) PrintDebug(f);
 for (i=0; i<n; i++) PrintFunction(f->p[i],full);
}
