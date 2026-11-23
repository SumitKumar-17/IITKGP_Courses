#include "out.h"



int _init(EVP_PKEY_CTX *ctx)

{
  int iVar1;
  
  iVar1 = __gmon_start__();
  return iVar1;
}



void FUN_00102020(void)

{
  (*(code *)(undefined *)0x0)();
  return;
}



void __cxa_finalize(void)

{
  __cxa_finalize();
  return;
}



void __thiscall std::allocator<char>::~allocator(allocator<char> *this)

{
  ~allocator(this);
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

void std::string::compare(char *param_1)

{
  compare(param_1);
  return;
}



void __cxa_begin_catch(void)

{
  __cxa_begin_catch();
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

size_t strlen(char *__s)

{
  size_t sVar1;
  
  sVar1 = strlen(__s);
  return sVar1;
}



void __thiscall std::string::~string(string *this)

{
  ~string(this);
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

void std::string::_M_set_length(ulong param_1)

{
  _M_set_length(param_1);
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

void std::__throw_logic_error(char *param_1)

{
  __throw_logic_error(param_1);
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

void std::string::_M_local_data(void)

{
  _M_local_data();
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

void std::string::_M_data(char *param_1)

{
  _M_data(param_1);
  return;
}



void __cxa_atexit(void)

{
  __cxa_atexit();
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

ostream * std::operator<<(ostream *param_1,string *param_2)

{
  ostream *poVar1;
  
  poVar1 = operator<<(param_1,param_2);
  return poVar1;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

ostream * std::operator<<(ostream *param_1,char *param_2)

{
  ostream *poVar1;
  
  poVar1 = operator<<(param_1,param_2);
  return poVar1;
}



void __thiscall std::ostream::operator<<(ostream *this,_func_ostream_ptr_ostream_ptr *param_1)

{
  operator<<(this,param_1);
  return;
}



void __thiscall
std::string::_Alloc_hider::_Alloc_hider(_Alloc_hider *this,char *param_1,allocator *param_2)

{
  _Alloc_hider(this,param_1,param_2);
  return;
}



void __thiscall std::allocator<char>::~allocator(allocator<char> *this)

{
  ~allocator(this);
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

void std::string::_M_data(void)

{
  _M_data();
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

void std::string::_S_copy_chars(char *param_1,char *param_2,char *param_3)

{
  _S_copy_chars(param_1,param_2,param_3);
  return;
}



void __stack_chk_fail(void)

{
                    // WARNING: Subroutine does not return
  __stack_chk_fail();
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

void std::string::_M_dispose(void)

{
  _M_dispose();
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

istream * std::operator>>(istream *param_1,string *param_2)

{
  istream *piVar1;
  
  piVar1 = operator>>(param_1,param_2);
  return piVar1;
}



void __thiscall std::string::string(string *this)

{
  string(this);
  return;
}



void __cxa_rethrow(void)

{
  __cxa_rethrow();
  return;
}



void __thiscall std::ios_base::Init::Init(Init *this)

{
  Init(this);
  return;
}



void __cxa_end_catch(void)

{
  __cxa_end_catch();
  return;
}



void _Unwind_Resume(void)

{
                    // WARNING: Subroutine does not return
  _Unwind_Resume();
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

void std::allocator<char>::allocator(void)

{
  allocator();
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

void std::string::_M_create(ulong *param_1,ulong param_2)

{
  _M_create(param_1,param_2);
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked

void std::string::_M_capacity(ulong param_1)

{
  _M_capacity(param_1);
  return;
}



void processEntry _start(undefined8 param_1,undefined8 param_2)

{
  undefined1 auStack_8 [8];
  
  __libc_start_main(main,param_2,&stack0x00000008,0,0,param_1,auStack_8);
  do {
                    // WARNING: Do nothing block with infinite loop
  } while( true );
}



// WARNING: Removing unreachable block (ram,0x00102403)
// WARNING: Removing unreachable block (ram,0x0010240f)

void deregister_tm_clones(void)

{
  return;
}



// WARNING: Removing unreachable block (ram,0x00102444)
// WARNING: Removing unreachable block (ram,0x00102450)

void register_tm_clones(void)

{
  return;
}



void __do_global_dtors_aux(void)

{
  if (completed_0 != '\0') {
    return;
  }
  __cxa_finalize(__dso_handle);
  deregister_tm_clones();
  completed_0 = 1;
  return;
}



void frame_dummy(void)

{
  register_tm_clones();
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked
// helper_func()

void helper_func(void)

{
  ostream *poVar1;
  long in_FS_OFFSET;
  allocator local_49;
  string local_48 [40];
  long local_20;
  
  local_20 = *(long *)(in_FS_OFFSET + 0x28);
  poVar1 = std::operator<<((ostream *)std::cout,"mr. robot encrypted string : ");
  std::ostream::operator<<(poVar1,std::endl<char,std::char_traits<char>>);
  std::allocator<char>::allocator();
                    // try { // try from 00102511 to 00102515 has its CatchHandler @ 00102567
  std::string::string<std::allocator<char>>
            (local_48,"sfbbguzajvaot{qswisrdcrwhosreaafty}",&local_49);
  std::allocator<char>::~allocator((allocator<char> *)&local_49);
                    // try { // try from 00102533 to 00102549 has its CatchHandler @ 00102585
  poVar1 = std::operator<<((ostream *)std::cout,local_48);
  std::ostream::operator<<(poVar1,std::endl<char,std::char_traits<char>>);
  std::string::~string(local_48);
  if (local_20 != *(long *)(in_FS_OFFSET + 0x28)) {
                    // WARNING: Subroutine does not return
    __stack_chk_fail();
  }
  return;
}



undefined8 main(void)

{
  bool bVar1;
  ostream *poVar2;
  long in_FS_OFFSET;
  string local_48 [40];
  long local_20;
  
  local_20 = *(long *)(in_FS_OFFSET + 0x28);
  poVar2 = std::operator<<((ostream *)std::cout,"provide password: ");
  std::ostream::operator<<(poVar2,std::endl<char,std::char_traits<char>>);
  std::string::string(local_48);
                    // try { // try from 00102612 to 00102662 has its CatchHandler @ 00102687
  std::operator>>((istream *)std::cin,local_48);
  bVar1 = std::operator==(local_48,"hello!!world");
  if (bVar1) {
    helper_func();
  }
  else {
    poVar2 = std::operator<<((ostream *)std::cout,"wrong password, try again !!!");
    std::ostream::operator<<(poVar2,std::endl<char,std::char_traits<char>>);
  }
  std::string::~string(local_48);
  if (local_20 != *(long *)(in_FS_OFFSET + 0x28)) {
                    // WARNING: Subroutine does not return
    __stack_chk_fail();
  }
  return 0;
}



// __static_initialization_and_destruction_0(int, int)

void __static_initialization_and_destruction_0(int param_1,int param_2)

{
  if ((param_1 == 1) && (param_2 == 0xffff)) {
    std::ios_base::Init::Init((Init *)&std::__ioinit);
    __cxa_atexit(std::ios_base::Init::~Init,&std::__ioinit,&__dso_handle);
  }
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked
// helper_func()

void _GLOBAL__sub_I_helper_func(void)

{
  __static_initialization_and_destruction_0(1,0xffff);
  return;
}



// WARNING: Removing unreachable block (ram,0x00102740)
// std::char_traits<char>::length(char const*)

void std::char_traits<char>::length(char *param_1)

{
  strlen(param_1);
  return;
}



// char_traits<char>::eq(char const&, char const&)

bool char_traits<char>::eq(char *param_1,char *param_2)

{
  return *param_1 == *param_2;
}



// char_traits<char>::length(char const*)

long char_traits<char>::length(char *param_1)

{
  char cVar1;
  long in_FS_OFFSET;
  char local_19;
  long local_18;
  long local_10;
  
  local_10 = *(long *)(in_FS_OFFSET + 0x28);
  local_18 = 0;
  while( true ) {
    local_19 = '\0';
    cVar1 = eq(param_1 + local_18,&local_19);
    if (cVar1 == '\x01') break;
    local_18 = local_18 + 1;
  }
  if (local_10 != *(long *)(in_FS_OFFSET + 0x28)) {
                    // WARNING: Subroutine does not return
    __stack_chk_fail();
  }
  return local_18;
}



// std::string::_Alloc_hider::~_Alloc_hider()

void __thiscall std::string::_Alloc_hider::~_Alloc_hider(_Alloc_hider *this)

{
  std::allocator<char>::~allocator((allocator<char> *)this);
  return;
}



// std::string::string<std::allocator<char> >(char const*, std::allocator<char> const&)

void __thiscall
std::string::string<std::allocator<char>>(string *this,char *param_1,allocator *param_2)

{
  long lVar1;
  char *pcVar2;
  long lVar3;
  long in_FS_OFFSET;
  
  lVar1 = *(long *)(in_FS_OFFSET + 0x28);
  pcVar2 = (char *)std::string::_M_local_data();
  std::string::_Alloc_hider::_Alloc_hider((_Alloc_hider *)this,pcVar2,param_2);
  if (param_1 == (char *)0x0) {
    pcVar2 = (char *)0x1;
  }
  else {
                    // try { // try from 0010286b to 00102898 has its CatchHandler @ 0010289b
    lVar3 = char_traits<char>::length(param_1);
    pcVar2 = param_1 + lVar3;
  }
  _M_construct<char_const*>(this,param_1,pcVar2);
  if (lVar1 != *(long *)(in_FS_OFFSET + 0x28)) {
                    // WARNING: Subroutine does not return
    __stack_chk_fail();
  }
  return;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked
// bool std::TEMPNAMEPLACEHOLDERVALUE(std::string const&, char const*)

bool std::operator==(string *param_1,char *param_2)

{
  int iVar1;
  
  iVar1 = std::string::compare((char *)param_1);
  return iVar1 == 0;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked
// std::iterator_traits<char const*>::difference_type std::distance<char const*>(char const*, char
// const*)

void std::distance<char_const*>(char *param_1,char *param_2)

{
  char *local_10;
  
  local_10 = param_1;
  __iterator_category<char_const*>(&local_10);
  __distance<char_const*>(local_10,param_2);
  return;
}



// void std::string::_M_construct<char const*>(char const*, char const*, std::forward_iterator_tag)

void std::string::_M_construct<char_const*>(ulong *param_1,char *param_2,char *param_3)

{
  bool bVar1;
  char *pcVar2;
  long in_FS_OFFSET;
  ulong local_28;
  long local_20;
  
  local_20 = *(long *)(in_FS_OFFSET + 0x28);
  bVar1 = __is_null_pointer<char_const>(param_2);
  if ((bVar1) && (param_2 != param_3)) {
    bVar1 = true;
  }
  else {
    bVar1 = false;
  }
  if (bVar1) {
    std::__throw_logic_error("basic_string::_M_construct null not valid");
  }
  local_28 = distance<char_const*>(param_2,param_3);
  if (0xf < local_28) {
    std::string::_M_create(param_1,(ulong)&local_28);
    std::string::_M_data((char *)param_1);
    std::string::_M_capacity((ulong)param_1);
  }
                    // try { // try from 001029fc to 00102a00 has its CatchHandler @ 00102a3c
  pcVar2 = (char *)std::string::_M_data();
  std::string::_S_copy_chars(pcVar2,param_2,param_3);
  std::string::_M_set_length((ulong)param_1);
  if (local_20 != *(long *)(in_FS_OFFSET + 0x28)) {
                    // WARNING: Subroutine does not return
    __stack_chk_fail();
  }
  return;
}



// bool __is_null_pointer<char const>(char const*)

bool __is_null_pointer<char_const>(char *param_1)

{
  return param_1 == (char *)0x0;
}



// WARNING: Unknown calling convention -- yet parameter storage is locked
// std::iterator_traits<char const*>::iterator_category std::__iterator_category<char const*>(char
// const* const&)

void std::__iterator_category<char_const*>(char **param_1)

{
  return;
}



// std::iterator_traits<char const*>::difference_type std::__distance<char const*>(char const*, char
// const*, std::random_access_iterator_tag)

long std::__distance<char_const*>(long param_1,long param_2)

{
  return param_2 - param_1;
}



void _fini(void)

{
  return;
}



