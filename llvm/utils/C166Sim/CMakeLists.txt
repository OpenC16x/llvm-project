set(LLVM_LINK_COMPONENTS
  C166Desc
  C166Disassembler
  C166Info
  MC
  MCDisassembler
  Object
  DebugInfoDWARF
  Support
  )

add_llvm_utility(c166-sim
  c166-sim.cpp
  Machine.cpp
  Execute.cpp
  Unwind.cpp
  )
