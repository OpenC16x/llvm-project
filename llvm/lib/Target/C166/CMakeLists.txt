add_llvm_component_group(C166)

set(LLVM_TARGET_DEFINITIONS C166.td)

tablegen(LLVM C166GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM C166GenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM C166GenCallingConv.inc -gen-callingconv)
tablegen(LLVM C166GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM C166GenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM C166GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM C166GenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM C166GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM C166GenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM C166GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(C166CommonTableGen)

add_llvm_target(C166CodeGen
  C166AsmPrinter.cpp
  C166FrameLowering.cpp
  C166ISelDAGToDAG.cpp
  C166ISelLowering.cpp
  C166InstrInfo.cpp
  C166MCInstLower.cpp
  C166FoldCompare.cpp
  C166LowerThreadLocal.cpp
  C166MACChain.cpp
  C166MergeExtend.cpp
  C166MachineFunctionInfo.cpp
  C166RegisterInfo.cpp
  C166SelectionDAGInfo.cpp
  C166Subtarget.cpp
  C166TargetMachine.cpp
  C166TargetObjectFile.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  C166Desc
  C166Info
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  C166
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
