//
// Copyright (c) 2012, University of Erlangen-Nuremberg
// Copyright (c) 2012, Siemens AG
// Copyright (c) 2010, ARM Limited
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

//===--- CreateHostStrings.cpp - Runtime string creator for the Rewriter --===//
//
// This file implements functionality for printing HIPAcc runtime code to
// strings.
//
//===----------------------------------------------------------------------===//

#include "hipacc/Rewrite/CreateHostStrings.h"

using namespace clang;
using namespace hipacc;


void CreateHostStrings::writeHeaders(std::string &resultStr) {
  switch (options.getTargetCode()) {
    case TARGET_C:
      resultStr += "#include \"hipacc_cpu.hpp\"\n\n";
      break;
    case TARGET_Vivado:
      // do nothing here, we add this and forward declarations just before
      // writing the file @see Rewrite::HandleTranslationUnit().
      break;
    case TARGET_CUDA:
      resultStr += "#include \"hipacc_cu.hpp\"\n\n";
      break;
    case TARGET_OpenCLACC:
    case TARGET_OpenCLCPU:
    case TARGET_OpenCLGPU:
      resultStr += "#include \"hipacc_cl.hpp\"\n\n";
      break;
    case TARGET_Renderscript:
    case TARGET_Filterscript:
      resultStr += "#include \"hipacc_rs.hpp\"\n\n";
      break;
  }
}


void CreateHostStrings::writeInitialization(std::string &resultStr) {
  switch (options.getTargetCode()) {
    case TARGET_C:
    case TARGET_Vivado:
      break;
    case TARGET_CUDA:
      resultStr += "hipaccInitCUDA();\n";
      resultStr += indent;
      break;
    case TARGET_OpenCLACC:
    case TARGET_OpenCLCPU:
    case TARGET_OpenCLGPU:
      resultStr += "hipaccInitPlatformsAndDevices(";
      if (options.emitOpenCLACC()) {
        resultStr += "CL_DEVICE_TYPE_ACCELERATOR";
      } else if (options.emitOpenCLCPU()) {
        resultStr += "CL_DEVICE_TYPE_CPU";
      } else {
        resultStr += "CL_DEVICE_TYPE_GPU";
      }
      resultStr += ", ALL);\n";
      resultStr += indent + "hipaccCreateContextsAndCommandQueues();\n\n";
      resultStr += indent;
      break;
    case TARGET_Renderscript:
    case TARGET_Filterscript:
      resultStr += "hipaccInitRenderScript(";
      resultStr += RS_TARGET_API;
      resultStr += ");\n";
      resultStr += indent;
      break;
  }
}


void writeCLCompilation(std::string fileName, std::string kernelName,
    std::string includes, std::string &resultStr, std::string suffix="") {
  resultStr += "cl_kernel " + kernelName + suffix;
  resultStr += " = hipaccBuildProgramAndKernel(";
  resultStr += "\"" + fileName + ".cl\", ";
  resultStr += "\"" + kernelName + suffix + "\", ";
  resultStr += "true, false, false, \"-I " + includes + "\");\n";
}


void CreateHostStrings::writeKernelCompilation(HipaccKernel *K,
    std::string &resultStr) {
  switch (options.getTargetCode()) {
    case TARGET_C:
    case TARGET_Vivado:
    case TARGET_CUDA:
      break;
    case TARGET_Renderscript:
    case TARGET_Filterscript:
      resultStr += "ScriptC_" + K->getFileName() + " " + K->getKernelName();
      resultStr += " = hipaccInitScript<ScriptC_" + K->getFileName() + ">();\n";
      resultStr += indent;
      if (K->getKernelClass()->getReduceFunction()) {
        resultStr += "ScriptC_" + K->getFileName() + " " + K->getReduceName();
        resultStr += " = hipaccInitScript<ScriptC_" + K->getFileName() + ">();\n";
        resultStr += indent;
      }
      break;
    case TARGET_OpenCLACC:
    case TARGET_OpenCLCPU:
    case TARGET_OpenCLGPU:
      writeCLCompilation(K->getFileName(), K->getKernelName(),
          HipaccDevice(options).getCLIncludes(), resultStr);
      if (K->getKernelClass()->getReduceFunction()) {
        resultStr += indent;
        writeCLCompilation(K->getFileName(), K->getReduceName(),
            HipaccDevice(options).getCLIncludes(), resultStr, "2D");
        resultStr += indent;
        writeCLCompilation(K->getFileName(), K->getReduceName(),
            HipaccDevice(options).getCLIncludes(), resultStr, "1D");
      }
      break;
  }
}


void CreateHostStrings::addReductionArgument(HipaccKernel *K, std::string
    device_name, std::string host_name, std::string &resultStr) {
  resultStr += "_args" + K->getReduceName();
  resultStr += ".push_back(hipacc_script_arg<ScriptC_" + K->getFileName();
  resultStr += ">(&ScriptC_" + K->getFileName();
  resultStr += "::set_" + device_name + ", &" + host_name + "));\n";
  resultStr += indent;
}


void CreateHostStrings::writeReductionDeclaration(HipaccKernel *K, std::string
    &resultStr) {
  HipaccAccessor *Acc = K->getIterationSpace()->getAccessor();
  HipaccImage *Img = Acc->getImage();

  switch (options.getTargetCode()) {
    case TARGET_C:
    case TARGET_Vivado:
    case TARGET_CUDA:
    case TARGET_OpenCLACC:
    case TARGET_OpenCLCPU:
    case TARGET_OpenCLGPU:
      break;
    case TARGET_Renderscript:
    case TARGET_Filterscript:
      resultStr += "\n" + indent;
      resultStr += "std::vector<hipacc_script_arg<ScriptC_" + K->getFileName();
      resultStr += "> > _args" + K->getReduceName() + ";\n";
      resultStr += indent;

      // store reduction arguments
      std::stringstream LSS;
      LSS << literal_count++;
      resultStr += "sp<Allocation> alloc_" + LSS.str() + " = (Allocation  *)";
      resultStr += Img->getName() + ".mem;\n" + indent;
      addReductionArgument(K, "_red_Input", "alloc_" + LSS.str(), resultStr);
      addReductionArgument(K, "_red_stride", Img->getName() + ".stride",
          resultStr);

      // print optional offset_x/offset_y and iteration space width/height
      if (K->getIterationSpace()->isCrop()) {
        addReductionArgument(K, "_red_offset_x", Acc->getName() + ".offset_x",
            resultStr);
        addReductionArgument(K, "_red_offset_y", Acc->getName() + ".offset_y",
            resultStr);
        addReductionArgument(K, "_red_is_height", Acc->getName() + ".height",
            resultStr);
        addReductionArgument(K, "_red_num_elements", Acc->getName() + ".width",
            resultStr);
      } else {
        addReductionArgument(K, "_red_is_height", Img->getName() + ".height",
            resultStr);
        addReductionArgument(K, "_red_num_elements", Img->getName() + ".width",
            resultStr);
      }
      break;
  }
}


void CreateHostStrings::writeMemoryAllocation(std::string memName, std::string
    type, std::string width, std::string height, std::string &resultStr,
    HipaccDevice &targetDevice) {
  resultStr += "HipaccImage " + memName + " = ";
  switch (options.getTargetCode()) {
    case TARGET_C:
    case TARGET_Vivado:
      resultStr += "hipaccCreateMemory<" + type + ">(";
      break;
    case TARGET_CUDA:
      // texture is bound at kernel launch
      if (options.useTextureMemory() && options.getTextureType()==Array2D) {
        resultStr += "hipaccCreateArray2D<" + type + ">(";
      } else {
        resultStr += "hipaccCreateMemory<" + type + ">(";
      }
      break;
    case TARGET_Renderscript:
    case TARGET_Filterscript:
      resultStr += "hipaccCreateAllocation((" + type + "*)";
      break;
    case TARGET_OpenCLACC:
    case TARGET_OpenCLCPU:
    case TARGET_OpenCLGPU:
      if (options.useTextureMemory()) {
        resultStr += "hipaccCreateImage<" + type + ">(";
      } else {
        resultStr += "hipaccCreateBuffer<" + type + ">(";
      }
      break;
  }
  resultStr += "NULL, " + width + ", " + height;
  if (options.useTextureMemory() && options.getTextureType()==Array2D) {
    // OpenCL Image objects and CUDA Arrays don't support padding
  } else {
    if (options.emitPadding() &&
        !options.emitVivado()) {
      std::stringstream alignment;
      alignment << targetDevice.alignment;
      resultStr += ", " + alignment.str();
    }
  }
  resultStr += ");";
}


void CreateHostStrings::writeMemoryAllocationConstant(std::string memName,
    std::string type, std::string width, std::string height, std::string
    &resultStr) {

  resultStr += "HipaccImage " + memName + " = ";
  switch (options.getTargetCode()) {
    case TARGET_C:
    case TARGET_Vivado:
      resultStr += "hipaccCreateMemory<" + type + ">(";
      resultStr += "NULL, " + width + ", " + height + ");";
      break;
    case TARGET_CUDA:
      assert(0 && "constant memory allocation not required in CUDA!");
      break;
    case TARGET_Renderscript:
    case TARGET_Filterscript:
      resultStr += "hipaccCreateAllocation((" + type + "*)";
      resultStr += "NULL, " + width + ", " + height + ");";
      break;
    case TARGET_OpenCLACC:
    case TARGET_OpenCLCPU:
    case TARGET_OpenCLGPU:
      resultStr += "hipaccCreateBufferConstant<" + type + ">(";
      resultStr += width + ", " + height + ");";
      break;
  }
}


void CreateHostStrings::writeMemoryTransfer(HipaccImage *Img, std::string mem,
    MemoryTransferDirection direction, std::string &resultStr) {
  switch (direction) {
    case HOST_TO_DEVICE:
      resultStr += "hipaccWriteMemory(";
      resultStr += Img->getName();
      resultStr += ", " + mem + ");";
      break;
    case DEVICE_TO_HOST:
      resultStr += "hipaccReadMemory";
      if (!options.emitVivado()) {
        resultStr += "<" + Img->getTypeStr() + ">(";
      } else {
        resultStr += "(" + mem + ", ";
      }
      resultStr += Img->getName() + ");";
      break;
    case DEVICE_TO_DEVICE:
      resultStr += "hipaccCopyMemory(";
      resultStr += mem + ", ";
      resultStr += Img->getName() + ");";
      break;
    case HOST_TO_HOST:
      assert(0 && "Unsupported memory transfer direction!");
      break;
  }
}


void CreateHostStrings::writeMemoryTransfer(
    HipaccPyramid *Pyr, std::string idx, std::string mem,
    MemoryTransferDirection direction, std::string &resultStr) {
  switch (direction) {
    case HOST_TO_DEVICE:
      resultStr += "hipaccWriteMemory(";
      resultStr += Pyr->getName() + "(" + idx + ")";
      resultStr += ", " + mem + ");";
      break;
    case DEVICE_TO_HOST:
      resultStr += "hipaccReadMemory<" + Pyr->getTypeStr() + ">(";
      resultStr += Pyr->getName() + "(" + idx + "));";
      break;
    case DEVICE_TO_DEVICE:
      resultStr += "hipaccCopyMemory(";
      resultStr += mem + ", ";
      resultStr += Pyr->getName() + "(" + idx + "));";
      break;
    case HOST_TO_HOST:
      assert(0 && "Unsupported memory transfer direction!");
      break;
  }
}


void CreateHostStrings::writeMemoryTransferRegion(std::string dst, std::string
    src, std::string &resultStr) {
  resultStr += "hipaccCopyMemoryRegion(";
  resultStr += src + ", " + dst + ");";
}


void CreateHostStrings::writeMemoryTransferSymbol(HipaccMask *Mask, std::string
    mem, MemoryTransferDirection direction, std::string &resultStr) {
  switch (options.getTargetCode()) {
    case TARGET_CUDA: {
        size_t i = 0;
        for (auto kernel : Mask->getKernels()) {
          if (i++) resultStr += "\n" + indent;

          switch (direction) {
            case HOST_TO_DEVICE:
              resultStr += "hipaccWriteSymbol<" + Mask->getTypeStr() + ">(";
              resultStr += "(const void *)&";
              resultStr += Mask->getName() + kernel->getName() + ", ";
              resultStr += "(" + Mask->getTypeStr() + " *)" + mem;
              resultStr += ", " + Mask->getSizeXStr() + ", " + Mask->getSizeYStr() + ");";
              break;
            case DEVICE_TO_HOST:
              resultStr += "hipaccReadSymbol<" + Mask->getTypeStr() + ">(";
              resultStr += "(" + Mask->getTypeStr() + " *)" + mem;
              resultStr += "(const void *)&";
              resultStr += Mask->getName() + kernel->getName() + ", ";
              resultStr += ", " + Mask->getSizeXStr() + ", " + Mask->getSizeYStr() + ");";
              break;
            case DEVICE_TO_DEVICE:
              resultStr += "writeMemoryTransferSymbol(todo, todo, DEVICE_TO_DEVICE);";
              break;
            case HOST_TO_HOST:
              resultStr += "writeMemoryTransferSymbol(todo, todo, HOST_TO_HOST);";
              break;
          }
        }
      }
      break;
    case TARGET_C:
    case TARGET_Vivado:
    case TARGET_Renderscript:
    case TARGET_Filterscript:
    case TARGET_OpenCLACC:
    case TARGET_OpenCLCPU:
    case TARGET_OpenCLGPU:
      resultStr += "hipaccWriteMemory(" + Mask->getName();
      resultStr += ", (" + Mask->getTypeStr() + " *)" + mem + ");";
      break;
  }
}


void CreateHostStrings::writeMemoryTransferDomainFromMask(
    HipaccMask *Domain, HipaccMask *Mask, std::string &resultStr) {
  switch (options.getTargetCode()) {
    case TARGET_CUDA: {
        size_t i = 0;
        for (auto kernel : Mask->getKernels()) {
          if (i++) resultStr += "\n" + indent;
          resultStr += "hipaccWriteDomainFromMask<";
          resultStr += Mask->getTypeStr() + ">(";
          resultStr += "(const void *)&";
          resultStr += Domain->getName() + kernel->getName() + ", ";
          resultStr += "(" + Mask->getTypeStr() + " *)";
          resultStr += Mask->getHostMemName();
          resultStr += ", " + Mask->getSizeXStr();
          resultStr += ", " + Mask->getSizeYStr() + ");";
        }
      }
      break;
    case TARGET_C:
    case TARGET_Vivado:
    case TARGET_Renderscript:
    case TARGET_Filterscript:
    case TARGET_OpenCLACC:
    case TARGET_OpenCLCPU:
    case TARGET_OpenCLGPU:
      resultStr += "hipaccWriteDomainFromMask<" + Mask->getTypeStr() + ">(";
      resultStr += Domain->getName() + ", (" + Mask->getTypeStr() + "*)";
      resultStr += Mask->getHostMemName() + ");";
      break;
  }
}


void CreateHostStrings::writeMemoryRelease(HipaccMemory *Mem,
    std::string &resultStr, bool isPyramid) {
  // The same runtime call for all targets, just distinguish between Pyramids
  // and 'normal' memory like Images and Masks.
  if (isPyramid) {
    resultStr += "hipaccReleasePyramid(";
  } else {
    resultStr += "hipaccReleaseMemory(";
  }
  resultStr += Mem->getName() + ");\n";
  resultStr += indent;
}


void CreateHostStrings::writeKernelCall(std::string kernelName,
    HipaccKernelClass *KC, HipaccKernel *K, std::string &resultStr) {
  auto argTypeNames = K->getArgTypeNames();
  auto deviceArgNames = K->getDeviceArgNames();
  auto hostArgNames = K->getHostArgNames();

  std::stringstream LSS;
  std::stringstream PPTSS;
  std::stringstream cX, cY;
  LSS << literal_count++;
  PPTSS << K->getPixelsPerThread();
  cX << K->getNumThreadsX();
  cY << K->getNumThreadsY();
  std::string blockStr, gridStr, offsetStr, infoStr;
  std::stringstream maxSizeXStr, maxSizeYStr;
  maxSizeXStr << K->getMaxSizeX();
  maxSizeYStr << K->getMaxSizeY();

  switch (options.getTargetCode()) {
    case TARGET_C:
        break;
    case TARGET_Vivado:
        return;
        break;
    case TARGET_CUDA:
      blockStr = "block" + LSS.str();
      gridStr = "grid" + LSS.str();
      offsetStr = "offset" + LSS.str();
      break;
    case TARGET_Renderscript:
    case TARGET_Filterscript:
      blockStr = "work_size" + LSS.str();
      gridStr = K->getIterationSpace()->getAccessor()->getImage()->getName();
      break;
    case TARGET_OpenCLACC:
    case TARGET_OpenCLCPU:
    case TARGET_OpenCLGPU:
      blockStr = "local_work_size" + LSS.str();
      gridStr = "global_work_size" + LSS.str();
      break;
  }
  infoStr = K->getInfoStr();

  if (options.exploreConfig() || options.timeKernels()) {
    inc_indent();
    resultStr += "{\n";
    switch (options.getTargetCode()) {
      case TARGET_C:
      case TARGET_Vivado:
        break;
      case TARGET_CUDA:
        if (options.exploreConfig()) {
          resultStr += indent + "std::vector<void *> _args" + kernelName + ";\n";
          resultStr += indent + "std::vector<hipacc_const_info> _consts" + kernelName + ";\n";
          resultStr += indent + "std::vector<hipacc_tex_info*> _texs" + kernelName + ";\n";
        } else {
          resultStr += indent + "std::vector<std::pair<size_t, void *> > _args" + kernelName + ";\n";
        }
        break;
      case TARGET_OpenCLACC:
      case TARGET_OpenCLCPU:
      case TARGET_OpenCLGPU:
        resultStr += indent + "std::vector<std::pair<size_t, void *> > _args" + kernelName + ";\n";
        break;
      case TARGET_Renderscript:
      case TARGET_Filterscript:
        resultStr += indent + "std::vector<hipacc_script_arg<ScriptC_" + K->getFileName() + "> >";
        resultStr += " _args" + kernelName + ";\n";
    }
    resultStr += indent + "std::vector<hipacc_smem_info> _smems" + kernelName + ";\n";
    resultStr += indent;
  }

  if (!options.emitC() && !options.emitVivado()) {
    // hipacc_launch_info
    resultStr += "hipacc_launch_info " + infoStr + "(";
    resultStr += maxSizeXStr.str() + ", ";
    resultStr += maxSizeYStr.str() + ", ";
    resultStr += K->getIterationSpace()->getName() + ", ";
    resultStr += PPTSS.str() + ", ";
    if (K->vectorize()) {
      // TODO set and calculate per kernel simd width ...
      resultStr += "4);\n";
    } else {
      resultStr += "1);\n";
    }
    resultStr += indent;
  }

  if (!options.exploreConfig()) {
    switch (options.getTargetCode()) {
      case TARGET_C:
      case TARGET_Vivado:
          break;
      case TARGET_CUDA:
        // dim3 block
        resultStr += "dim3 " + blockStr + "(" + cX.str() + ", " + cY.str() + ");\n";
        resultStr += indent;

        // dim3 grid & hipaccCalcGridFromBlock
        resultStr += "dim3 " + gridStr + "(hipaccCalcGridFromBlock(";
        resultStr += infoStr + ", ";
        resultStr += blockStr + "));\n\n";
        resultStr += indent;

        // hipaccPrepareKernelLaunch
        resultStr += "hipaccPrepareKernelLaunch(";
        resultStr += infoStr + ", ";
        resultStr += blockStr + ");\n";
        resultStr += indent;

        // hipaccConfigureCall
        resultStr += "hipaccConfigureCall(";
        resultStr += gridStr;
        resultStr += ", " + blockStr;
        resultStr += ");\n\n";
        resultStr += indent;

        // offset parameter
        if (!options.timeKernels()) {
          resultStr += "size_t " + offsetStr + " = 0;\n";
          resultStr += indent;
        }
        break;
      case TARGET_Renderscript:
      case TARGET_Filterscript:
        // size_t work_size
        resultStr += "size_t " + blockStr + "[2];\n";
        resultStr += indent + blockStr + "[0] = " + cX.str() + ";\n";
        resultStr += indent + blockStr + "[1] = " + cY.str() + ";\n";
        resultStr += indent;

        // hipaccPrepareKernelLaunch
        resultStr += "hipaccPrepareKernelLaunch(";
        resultStr += infoStr + ", ";
        resultStr += blockStr + ");\n\n";
        resultStr += indent;
        break;
      case TARGET_OpenCLACC:
      case TARGET_OpenCLCPU:
      case TARGET_OpenCLGPU:
        // size_t block
        resultStr += "size_t " + blockStr + "[2];\n";
        resultStr += indent + blockStr + "[0] = " + cX.str() + ";\n";
        resultStr += indent + blockStr + "[1] = " + cY.str() + ";\n";
        resultStr += indent;

        // size_t grid
        resultStr += "size_t " + gridStr + "[2];\n\n";
        resultStr += indent;

        // hipaccCalcGridFromBlock
        resultStr += "hipaccCalcGridFromBlock(";
        resultStr += infoStr + ", ";
        resultStr += blockStr + ", ";
        resultStr += gridStr + ");\n";
        resultStr += indent;

        // hipaccPrepareKernelLaunch
        resultStr += "hipaccPrepareKernelLaunch(";
        resultStr += infoStr + ", ";
        resultStr += blockStr + ");\n\n";
        resultStr += indent;
        break;
    }
  }


  // bind textures and get constant pointers
  size_t num_arg = 0;
  for (auto arg : K->getDeviceArgFields()) {
    size_t i = num_arg++;

    // skip unused variables
    if (!K->getUsed(K->getDeviceArgNames()[i])) continue;

    HipaccAccessor *Acc = K->getImgFromMapping(arg);
    if (Acc) {
      if (options.emitCUDA() && K->useTextureMemory(Acc)) {
        if (KC->getImgAccess(arg)==READ_ONLY &&
            // no texture required for __ldg() intrinsic
            !(K->useTextureMemory(Acc) == Ldg)) {
          // bind texture
          if (options.exploreConfig()) {
            LSS.str(std::string());
            LSS.clear();
            LSS << literal_count++;
            resultStr += "hipacc_tex_info tex_info" + LSS.str();
            resultStr += "(std::string(\"_tex" + deviceArgNames[i] + K->getName() + "\"), ";
            resultStr += K->getImgFromMapping(arg)->getImage()->getTextureType() + ", ";
            resultStr += hostArgNames[i] + ", ";
            switch (K->useTextureMemory(Acc)) {
              case Linear1D: resultStr += "Linear1D"; break;
              case Linear2D: resultStr += "Linear2D"; break;
              case Array2D:  resultStr += "Array2D";  break;
              default: assert(0 && "unsupported texture type!");
            }
            resultStr += ");\n";
            resultStr += indent;
            resultStr += "_texs" + kernelName + ".push_back(";
            resultStr += "&tex_info" + LSS.str() + ");\n";
          } else {
            resultStr += "cudaGetTextureReference(&_tex";
            resultStr += deviceArgNames[i] + K->getName() + "Ref, &_tex";
            resultStr += deviceArgNames[i] + K->getName() + ");\n";
            resultStr += indent;
            resultStr += "hipaccBindTexture<" + argTypeNames[i] + ">(";
            switch (K->useTextureMemory(Acc)) {
              case Linear1D: resultStr += "Linear1D"; break;
              case Linear2D: resultStr += "Linear2D"; break;
              case Array2D:  resultStr += "Array2D";  break;
              default: assert(0 && "unsupported texture type!");
            }
            resultStr += ", _tex" + deviceArgNames[i] + K->getName() + "Ref, ";
            resultStr += hostArgNames[i] + ");\n";
          }
          resultStr += indent;
        }
      }

      if (options.exploreConfig() && K->useLocalMemory(Acc)) {
        // store local memory size information for exploration
        resultStr += "_smems" + kernelName + ".push_back(";
        resultStr += "hipacc_smem_info(" + Acc->getSizeXStr() + ", ";
        resultStr += Acc->getSizeYStr() + ", ";
        resultStr += "sizeof(" + Acc->getImage()->getTypeStr() + ")));\n";
        resultStr += indent;
      }
    }

    if (options.emitCUDA() && options.exploreConfig()) {
      HipaccMask *Mask = K->getMaskFromMapping(arg);
      if (Mask && !Mask->isConstant()) {
        // get constant pointer
        resultStr += "_consts" + kernelName + ".push_back(";
        resultStr += "hipacc_const_info(std::string(\"" + Mask->getName() + K->getName() + "\"), ";
        resultStr += "(void *)" + Mask->getHostMemName() + ", ";
        resultStr += "sizeof(" + Mask->getTypeStr() + ")*" + Mask->getSizeXStr() + "*" + Mask->getSizeYStr() + "));\n";
        resultStr += indent;
      }
    }
  }


  // check if Array2D memory is required for the iteration space
  if (options.emitCUDA() && options.useTextureMemory() &&
      options.getTextureType()==Array2D) {
    // bind surface
    if (options.exploreConfig()) {
      LSS.str(std::string());
      LSS.clear();
      LSS << literal_count++;
      resultStr += "hipacc_tex_info tex_info" + LSS.str();
      resultStr += "(std::string(\"_surfOutput" + K->getName() + "\"), ";
      resultStr += K->getIterationSpace()->getAccessor()->getImage()->getTextureType() + ", ";
      resultStr += K->getIterationSpace()->getAccessor()->getImage()->getName() + ", Surface);\n";
      resultStr += indent;
      resultStr += "_texs" + kernelName + ".push_back(";
      resultStr += "&tex_info" + LSS.str() + ");\n";
    } else {
      resultStr += "cudaGetSurfaceReference(&_surfOutput" + K->getName() + "Ref, ";
      resultStr += "&_surfOutput" + K->getName() + ");\n";
      resultStr += indent;
      resultStr += "hipaccBindSurface<";
      resultStr += K->getIterationSpace()->getAccessor()->getImage()->getTypeStr();
      resultStr += ">(_surfOutput" + K->getName() + "Ref, ";
      resultStr += K->getIterationSpace()->getAccessor()->getImage()->getName() + ");\n";
    }
    resultStr += indent;
  }

  #if 0
  for (auto img : KC->getImgFields()) {
    HipaccAccessor *Acc = K->getImgFromMapping(img);
    // emit assertion
    resultStr += "assert(" + Acc->getName() + ".width==" + K->getIterationSpace()->getName() + ".width && \"Acc width != IS width\");\n" + indent;
    resultStr += "assert(" + Acc->getName() + ".height==" + K->getIterationSpace()->getName() + ".height && \"Acc height != IS height\");\n" + indent;
  }
  #endif


  // parameters
  size_t cur_arg = 0;
  num_arg = 0;
  for (auto arg : K->getDeviceArgFields()) {
    size_t i = num_arg++;

    // skip unused variables
    if (!K->getUsed(K->getDeviceArgNames()[i])) continue;

    HipaccMask *Mask = K->getMaskFromMapping(arg);
    if (Mask) {
      if (options.emitCUDA()) {
        Mask->addKernel(K);
        continue;
      } else {
        if (Mask->isConstant()) continue;
      }
    }

    if (options.emitCUDA() && i==0 && options.useTextureMemory() &&
        options.getTextureType()==Array2D) {
      // surface is handled separately
      continue;
    }

    HipaccAccessor *Acc = K->getImgFromMapping(arg);
    if (options.emitCUDA() && Acc && K->useTextureMemory(Acc) &&
        KC->getImgAccess(arg)==READ_ONLY &&
        // no texture required for __ldg() intrinsic
        !(K->useTextureMemory(Acc) == Ldg)) {
      // textures are handled separately
      continue;
    }
    std::string img_mem;
    if (Acc || i==0 || Mask) img_mem = ".mem";

    if (options.exploreConfig() || options.timeKernels()) {
      // add kernel argument
      switch (options.getTargetCode()) {
        case TARGET_C:
        case TARGET_Vivado:
          break;
        case TARGET_CUDA:
          resultStr += "_args" + kernelName + ".push_back(";
          if (options.exploreConfig()) {
            resultStr += "(void *)&" + hostArgNames[i] + img_mem + ");\n";
          } else {
            resultStr += "std::make_pair(sizeof(" + argTypeNames[i];
            resultStr += "), (void *)&" + hostArgNames[i] + img_mem + "));\n";
          }
          resultStr += indent;
          break;
        case TARGET_OpenCLACC:
        case TARGET_OpenCLCPU:
        case TARGET_OpenCLGPU:
          resultStr += "_args" + kernelName + ".push_back(";
          resultStr += "std::make_pair(sizeof(" + argTypeNames[i];
          resultStr += "), (void *)&" + hostArgNames[i] + img_mem + "));\n";
          resultStr += indent;
          break;
        case TARGET_Renderscript:
        case TARGET_Filterscript:
          if (Acc || Mask || i==0) {
            LSS.str(std::string());
            LSS.clear();
            LSS << literal_count++;
            resultStr += "sp<Allocation> alloc_" + LSS.str() + " = (Allocation  *)";
            resultStr += hostArgNames[i] + img_mem + ";\n" + indent;
          }
          resultStr += "_args" + kernelName + ".push_back(";
          resultStr += "hipacc_script_arg<ScriptC_" + K->getFileName() + ">(";
          resultStr += "&ScriptC_" + K->getFileName();
          resultStr += "::set_" + deviceArgNames[i] + ", ";
          if (Acc || Mask || i==0) {
            resultStr += "&alloc_" + LSS.str() + "));\n";
          } else {
            resultStr += "(" + argTypeNames[i] + "*)&" + hostArgNames[i] + "));\n";
          }
          resultStr += indent;
          break;
      }
    } else {
      // set kernel arguments
      switch (options.getTargetCode()) {
        case TARGET_C:
        case TARGET_Vivado:
          if (i==0) {
            resultStr += "hipaccStartTiming();\n";
            resultStr += indent;
            resultStr += kernelName + "(";
          } else {
            resultStr += ", ";
          }
          if (i==0) {
            HipaccImage *Img =
              K->getIterationSpace()->getAccessor()->getImage();
            resultStr += "(" + Img->getTypeStr();
            resultStr += "(*)[" + Img->getSizeXStr() + "])";
          }
          if (Acc) {
            resultStr += "(" + Acc->getImage()->getTypeStr();
            resultStr += "(*)[" + Acc->getImage()->getSizeXStr() + "])";
          }
          if (Mask) {
            resultStr += "(" + argTypeNames[i] + ")";
          }
          resultStr += hostArgNames[i] + img_mem;
          break;
        case TARGET_CUDA:
          resultStr += "hipaccSetupArgument(&";
          resultStr += hostArgNames[i] + img_mem;
          resultStr += ", sizeof(" + argTypeNames[i] + "), ";
          resultStr += offsetStr;
          resultStr += ");\n";
          resultStr += indent;
          break;
        case TARGET_OpenCLACC:
        case TARGET_OpenCLCPU:
        case TARGET_OpenCLGPU:
          LSS.str(std::string());
          LSS.clear();
          LSS << cur_arg++;

          resultStr += "hipaccSetKernelArg(";
          resultStr += kernelName;
          resultStr += ", ";
          resultStr += LSS.str();
          resultStr += ", sizeof(" + argTypeNames[i] + "), ";
          resultStr += "&" + hostArgNames[i] + img_mem;
          resultStr += ");\n";
          resultStr += indent;
          break;
        case TARGET_Renderscript:
        case TARGET_Filterscript:
          resultStr += "hipaccSetScriptArg(&" + kernelName + ", ";
          resultStr += "&ScriptC_" + K->getFileName();
          resultStr += "::set_" + deviceArgNames[i] + ", ";
          if (Acc || Mask || i==0) {
            resultStr += "sp<Allocation>(((Allocation *)" + hostArgNames[i];
            resultStr += img_mem + ")));\n";
          } else {
            resultStr += "(" + argTypeNames[i] + ")" + hostArgNames[i] + img_mem + ");\n";
          }
          resultStr += indent;
          break;
      }
    }
  }
  if (options.getTargetCode()==TARGET_C) {
    // close parenthesis for function call
    resultStr += ");\n";
    resultStr += indent;
    resultStr += "hipaccStopTiming();\n";
    resultStr += indent;
  }
  resultStr += "\n" + indent;

  // launch kernel
  if (options.exploreConfig() || options.timeKernels()) {
    std::stringstream max_threads_per_block, max_threads_for_kernel, warp_size, max_shared_memory_per_block;
    max_threads_per_block << K->getMaxThreadsPerBlock();
    max_threads_for_kernel << K->getMaxThreadsForKernel();
    warp_size << K->getWarpSize();
    max_shared_memory_per_block << K->getMaxTotalSharedMemory();

    switch (options.getTargetCode()) {
      case TARGET_C:
      case TARGET_Vivado:
        break;
      case TARGET_CUDA:
        if (options.timeKernels()) {
          resultStr += "hipaccLaunchKernelBenchmark((const void *)&";
          resultStr += kernelName + ", \"";
          resultStr += kernelName + "\"";
        } else {
          resultStr += "hipaccKernelExploration(\"" + K->getFileName() + ".cu\", \"" + kernelName + "\"";
        }
        break;
      case TARGET_Renderscript:
      case TARGET_Filterscript:
        if (options.timeKernels()) {
          resultStr += "hipaccLaunchScriptKernelBenchmark(&" + kernelName;
        } else {
          resultStr += "ScriptC_" + K->getFileName() + " " + kernelName + " = ";
          resultStr += "hipaccInitScript<ScriptC_" + K->getFileName() + ">();\n";
          resultStr += indent + "hipaccLaunchScriptKernelExploration<";
          resultStr += "ScriptC_" + K->getFileName() + ", ";
          resultStr += K->getIterationSpace()->getImage()->getTypeStr();
          resultStr += ">(&" + kernelName;
        }
        break;
      case TARGET_OpenCLACC:
      case TARGET_OpenCLCPU:
      case TARGET_OpenCLGPU:
        if (options.timeKernels()) {
          resultStr += "hipaccEnqueueKernelBenchmark(" + kernelName;
        } else {
          resultStr += "hipaccKernelExploration(\"" + K->getFileName() + ".cl\", \"" + kernelName + "\"";
        }
        break;
    }
    resultStr += ", _args" + kernelName;
    if (0 != (options.getTargetCode() & (TARGET_Renderscript |
                                         TARGET_Filterscript))) {
        resultStr += ", &ScriptC_" + K->getFileName() + "::forEach_" + kernelName;
    }
    // additional parameters for exploration
    if (options.exploreConfig()) {
      resultStr += ", _smems" + kernelName;
      if (options.emitCUDA()) {
        resultStr += ", _consts" + kernelName;
        resultStr += ", _texs" + kernelName;
      }
      resultStr += ", " + infoStr;
      resultStr += ", " + warp_size.str();
      resultStr += ", " + max_threads_per_block.str();
      resultStr += ", " + max_threads_for_kernel.str();
      resultStr += ", " + max_shared_memory_per_block.str();
      resultStr += ", " + cX.str();
      resultStr += ", " + cY.str();
      if (options.emitCUDA()) {
        std::stringstream cc_string;
        cc_string << options.getTargetDevice();
        resultStr += ", " + cc_string.str();
      }
      if (0 != (options.getTargetCode() & (TARGET_Renderscript |
                                           TARGET_Filterscript))) {
        resultStr += ", " + gridStr;
      }
    } else {
      resultStr += ", " + gridStr;
      resultStr += ", " + blockStr;
      resultStr += ", true";
    }
    resultStr += ");\n";
    dec_indent();
    resultStr += indent + "}\n";
  } else {
    switch (options.getTargetCode()) {
      case TARGET_C:
      case TARGET_Vivado:
        break;
      case TARGET_CUDA:
        resultStr += "hipaccLaunchKernel((const void *)&";
        resultStr += kernelName + ", \"";
        resultStr += kernelName + "\"";
        break;
      case TARGET_Renderscript:
      case TARGET_Filterscript:
        resultStr += "hipaccLaunchScriptKernel(&" + kernelName + ", ";
        resultStr += "&ScriptC_" + K->getFileName() + "::forEach_" + kernelName;
        resultStr += ", " + gridStr;
        resultStr += ", " + blockStr + ");";
        break;
      case TARGET_OpenCLACC:
      case TARGET_OpenCLCPU:
      case TARGET_OpenCLGPU:
        resultStr += "hipaccEnqueueKernel(";
        resultStr += kernelName;
        break;
    }
    if (0 == (options.getTargetCode() & (TARGET_C | TARGET_Renderscript |
                                         TARGET_Filterscript))) {
      resultStr += ", " + gridStr;
      resultStr += ", " + blockStr;
      resultStr += ");";
    }
  }
}


void CreateHostStrings::writeReduceCall(HipaccKernelClass *KC, HipaccKernel *K,
    std::string &resultStr) {
  std::string typeStr(K->getIterationSpace()->getImage()->getTypeStr());
  std::string red_decl(typeStr + " " + K->getReduceStr() + " = ");

  // print runtime function name plus name of reduction function
  switch (options.getTargetCode()) {
    case TARGET_C:
    case TARGET_Vivado:
      break;
    case TARGET_CUDA:
      if (!options.exploreConfig()) {
        // first get texture reference
        resultStr += "cudaGetTextureReference(";
        resultStr += "&_tex" + K->getIterationSpace()->getImage()->getName() + K->getName() + "Ref, ";
        resultStr += "&_tex" + K->getIterationSpace()->getImage()->getName() + K->getName() + ");\n";
        resultStr += indent;
      }
      resultStr += red_decl;
      if (options.getTargetDevice() >= FERMI_20 && !options.exploreConfig()) {
        resultStr += "hipaccApplyReductionThreadFence<" + typeStr + ">(";
        resultStr += "(const void *)&" + K->getReduceName() + "2D, ";
        resultStr += "\"" + K->getReduceName() + "2D\", ";
      } else {
        if (options.exploreConfig()) {
          resultStr += "hipaccApplyReductionExploration<" + typeStr + ">(";
          resultStr += "\"" + K->getFileName() + ".cu\", ";
          resultStr += "\"" + K->getReduceName() + "2D\", ";
          resultStr += "\"" + K->getReduceName() + "1D\", ";
        } else {
          resultStr += "hipaccApplyReduction<" + typeStr + ">(";
          resultStr += "(const void *)&" + K->getReduceName() + "2D, ";
          resultStr += "\"" + K->getReduceName() + "2D\", ";
          resultStr += "(const void *)&" + K->getReduceName() + "1D, ";
          resultStr += "\"" + K->getReduceName() + "1D\", ";
        }
      }
      break;
    case TARGET_Renderscript:
    case TARGET_Filterscript:
      // no exploration supported atm since this involves lots of memory
      // reallocations in Renderscript
      resultStr += red_decl;
      resultStr += "hipaccApplyReduction<ScriptC_" + K->getFileName() + ", ";
      resultStr += typeStr + ">(&" + K->getReduceName() + ", ";
      resultStr += "&ScriptC_" + K->getFileName() + "::forEach_" +
        K->getReduceName() + "2D, ";
      resultStr += "&ScriptC_" + K->getFileName() + "::forEach_" +
        K->getReduceName() + "1D, ";
      resultStr += "&ScriptC_" + K->getFileName() + "::set__red_Output, ";
      resultStr += "_args" + K->getReduceName() + ", ";
      resultStr += K->getIterationSpace()->getName() + ".width);\n";
      return;
    case TARGET_OpenCLACC:
    case TARGET_OpenCLCPU:
    case TARGET_OpenCLGPU:
      resultStr += red_decl;
      if (options.exploreConfig()) {
        resultStr += "hipaccApplyReductionExploration<" + typeStr + ">(";
        resultStr += "\"" + K->getFileName() + ".cl\", ";
        resultStr += "\"" + K->getReduceName() + "2D\", ";
        resultStr += "\"" + K->getReduceName() + "1D\", ";
      } else {
        resultStr += "hipaccApplyReduction<" + typeStr + ">(";
        resultStr += K->getReduceName() + "2D, ";
        resultStr += K->getReduceName() + "1D, ";
      }
      break;
  }

  // print image name
  resultStr += K->getIterationSpace()->getName() + ", ";

  // print pixels per thread
  std::stringstream KSS;
  KSS << K->getNumThreadsReduce() << ", " << K->getPixelsPerThreadReduce();
  resultStr += KSS.str();

  if (options.emitCUDA()) {
    // print 2D CUDA array texture information - this parameter is only used if
    // the texture type is Array2D
    if (options.exploreConfig()) {
      resultStr += ", hipacc_tex_info(std::string(\"_tex";
      resultStr += K->getIterationSpace()->getImage()->getName() + K->getName();
      resultStr += "\"), " + K->getIterationSpace()->getImage()->getTextureType() + ", ";
      resultStr += K->getIterationSpace()->getImage()->getName() + ", ";
      if (options.useTextureMemory() && options.getTextureType()==Array2D) {
        resultStr += "Array2D";
      } else {
        resultStr += "Global";
      }
      resultStr += "), ";
    } else {
      resultStr += ", _tex";
      resultStr += K->getIterationSpace()->getImage()->getName() + K->getName();
      resultStr += "Ref";
    }

    if (options.exploreConfig()) {
      // print compute capability in case of configuration exploration
      std::stringstream cc_string;
      cc_string << options.getTargetDevice();
      resultStr += cc_string.str();
    }
  }
  resultStr += ");";
}


void CreateHostStrings::writeInterpolationDefinition(HipaccKernel *K,
    HipaccAccessor *Acc, std::string function_name, std::string type_suffix,
    InterpolationMode ip_mode, BoundaryMode bh_mode, std::string &resultStr) {
  // interpolation macro
  switch (ip_mode) {
    case InterpolateNO:
    case InterpolateNN:
      resultStr += "DEFINE_BH_VARIANT_NO_BH(INTERPOLATE_LINEAR_FILTERING";
      break;
    case InterpolateLF:
      resultStr += "DEFINE_BH_VARIANT(INTERPOLATE_LINEAR_FILTERING";
      break;
    case InterpolateCF:
      resultStr += "DEFINE_BH_VARIANT(INTERPOLATE_CUBIC_FILTERING";
      break;
    case InterpolateL3:
      resultStr += "DEFINE_BH_VARIANT(INTERPOLATE_LANCZOS_FILTERING";
      break;
  }
  switch (options.getTargetCode()) {
    case TARGET_C:
    case TARGET_Vivado:
      break;
    case TARGET_Renderscript:
    case TARGET_Filterscript:
      resultStr += "_RS, "; break;
    case TARGET_CUDA:
      resultStr += "_CUDA, "; break;
    case TARGET_OpenCLACC:
    case TARGET_OpenCLCPU:
    case TARGET_OpenCLGPU:
      resultStr += "_OPENCL, "; break;
  }
  // data type
  resultStr += Acc->getImage()->getTypeStr() + ", ";
  // append short data type - overloading is not supported in OpenCL
  if (options.emitOpenCL()) {
    resultStr += type_suffix + ", ";
  }
  // interpolation function
  resultStr += function_name;
  // boundary handling name + function (upper & lower)
  std::string const_parameter("NO_PARM");
  std::string const_suffix;
  switch (bh_mode) {
    case BOUNDARY_CLAMP:
      resultStr += "_clamp, BH_CLAMP_LOWER, BH_CLAMP_UPPER, "; break;
    case BOUNDARY_REPEAT:
      resultStr += "_repeat, BH_REPEAT_LOWER, BH_REPEAT_UPPER, "; break;
    case BOUNDARY_MIRROR:
      resultStr += "_mirror, BH_MIRROR_LOWER, BH_MIRROR_UPPER, "; break;
    case BOUNDARY_CONSTANT:
      resultStr += "_constant, BH_CONSTANT_LOWER, BH_CONSTANT_UPPER, ";
      const_parameter = "CONST_PARM";
      const_suffix = "_CONST";
      break;
    case BOUNDARY_UNDEFINED:
      resultStr += ", NO_BH, NO_BH, "; break;
  }
  // image memory parameter, constant parameter, memory access function
  switch (K->useTextureMemory(Acc)) {
    case NoTexture:
      if (options.emitRenderscript() || options.emitFilterscript()) {
        resultStr += "ALL_PARM, " + const_parameter + ", ALL" + const_suffix;
      } else {
        resultStr += "IMG_PARM, " + const_parameter + ", IMG" + const_suffix;
      }
      break;
    case Linear1D:
      resultStr += "TEX_PARM, " + const_parameter + ", TEX" + const_suffix;
      break;
    case Linear2D:
    case Array2D:
      resultStr += "ARR_PARM, " + const_parameter + ", ARR" + const_suffix;
      break;
    case Ldg:
      resultStr += "LDG_PARM, " + const_parameter + ", LDG" + const_suffix;
      break;
  }
  // image read function for OpenCL
  if (options.emitOpenCL()) {
    resultStr += ", " + Acc->getImage()->getImageReadFunction();
  }
  resultStr += ")\n";
}


void CreateHostStrings::writePyramidAllocation(std::string pyrName, std::string
    type, std::string img, std::string depth, std::string &resultStr) {
  resultStr += "HipaccPyramid " + pyrName + " = ";
  resultStr += "hipaccCreatePyramid<" + type + ">(";
  resultStr += img + ", " + depth + ");";
}

// vim: set ts=2 sw=2 sts=2 et ai:

