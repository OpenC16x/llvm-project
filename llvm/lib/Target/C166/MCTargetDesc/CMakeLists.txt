add_llvm_component_library(LLVMC166Desc
  C166AsmBackend.cpp
  C166ELFObjectWriter.cpp
  C166InstPrinter.cpp
  C166MCAsmInfo.cpp
  C166MCCodeEmitter.cpp
  C166MCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  C166Info
  Support

  ADD_TO_COMPONENT
  C166
  )
