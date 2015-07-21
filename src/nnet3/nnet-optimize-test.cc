// nnet3/nnet-optimize-test.cc

// Copyright 2015  Johns Hopkins University (author: Daniel Povey)

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#include "nnet3/nnet-nnet.h"
#include "nnet3/nnet-compile.h"
#include "nnet3/nnet-analyze.h"
#include "nnet3/nnet-test-utils.h"
#include "nnet3/nnet-optimize.h"
#include "nnet3/nnet-compute.h"

namespace kaldi {
namespace nnet3 {



// This test runs the computation with and without optimization, and checks that
// the outputs are the same.
void UnitTestNnetOptimize() {
  for (int32 n = 0; n < 20; n++) {
    struct NnetGenerationConfig gen_config;
    
    std::vector<std::string> configs;
    GenerateConfigSequence(gen_config, &configs);
    Nnet nnet;
    for (size_t j = 0; j < configs.size(); j++) {
      KALDI_LOG << "Input config[" << j << "] is: " << configs[j];
      std::istringstream is(configs[j]);
      nnet.ReadConfig(is);
    }

    ComputationRequest request;
    std::vector<Matrix<BaseFloat> > inputs;
    ComputeExampleComputationRequestSimple(nnet, &request, &inputs);
    
    NnetComputation computation;
    Compiler compiler(request, nnet);

    CompilerOptions opts;
    compiler.CreateComputation(opts, &computation);
    {
      std::ostringstream os;
      computation.Print(os, nnet);
      KALDI_LOG << "Generated computation is: " << os.str();
    }
    CheckComputationConfig check_config;
    // we can do the rewrite check since it's before optimization.
    check_config.check_rewrite = true;  
    ComputationChecker checker(check_config, nnet, request, computation);
    checker.Check();


    NnetComputation computation_opt(computation);
    
    {
      NnetOptimizeConfig opt_config;
      Optimize(opt_config, nnet, request, &computation_opt);
      std::ostringstream os;
      computation.Print(os, nnet);
      KALDI_LOG << "Optimized computation is: " << os.str();
    }

    NnetComputeOptions compute_opts;
    if (RandInt(0, 1) == 0)
      compute_opts.debug = true;
    
    computation.ComputeCudaIndexes();
    computation_opt.ComputeCudaIndexes();    
    NnetComputer computer(compute_opts,
                          computation,
                          nnet,
                          &nnet);
    Nnet nnet_opt(nnet);  // copy of the nnet for the optimized computation.
                          // necessary in case backprop changes parameters.

    // NnetComputer for the optimized version of the computation.
    NnetComputer computer_opt(compute_opts,
                              computation_opt,
                              nnet_opt,
                              &nnet_opt);
    
    // provide the input to the computations.
    for (size_t i = 0; i < request.inputs.size(); i++) {
      CuMatrix<BaseFloat> temp(inputs[i]);
      KALDI_LOG << "Input sum is " << temp.Sum();
      computer.AcceptInput(request.inputs[i].name, &temp);
      CuMatrix<BaseFloat> temp2(inputs[i]);
      computer_opt.AcceptInput(request.inputs[i].name, &temp2);
    }
    KALDI_LOG << "Running non-optimized forward computation";
    computer.Forward();
    KALDI_LOG << "Running optimized forward computation";
    computer_opt.Forward();
        
    const CuMatrixBase<BaseFloat> &output(computer.GetOutput("output"));
    KALDI_LOG << "Output sum (not optimized) is " << output.Sum();
    const CuMatrixBase<BaseFloat> &output_opt(computer_opt.GetOutput("output"));
    KALDI_LOG << "Output sum (optimized) is " << output_opt.Sum();
    if (!ApproxEqual(output, output_opt)) {
      KALDI_ERR << "Non-optimized and optimized versions of the computation give "
                << "different outputs.";
    }
    
    CuMatrix<BaseFloat> output_deriv(output.NumRows(), output.NumCols());
    output_deriv.SetRandn();
    CuMatrix<BaseFloat> output_deriv_opt(output_deriv);
    
    if (request.outputs[0].has_deriv) {
      computer.AcceptOutputDeriv("output", &output_deriv);
      computer_opt.AcceptOutputDeriv("output", &output_deriv_opt);
    }

    KALDI_LOG << "Running non-optimized backward computation";
    computer.Backward();
    KALDI_LOG << "Running optimized backward computation";
    computer_opt.Backward();
    for (size_t i = 0; i < request.inputs.size(); i++) {
      if (request.inputs[i].has_deriv) {
        const CuMatrixBase<BaseFloat> &in_deriv =
            computer.GetInputDeriv(request.inputs[i].name);
        const CuMatrixBase<BaseFloat> &in_deriv_opt =
            computer_opt.GetInputDeriv(request.inputs[i].name);
        KALDI_LOG << "Input-deriv sum for input '" << request.inputs[i].name
                  << "' (non-optimized) is " << in_deriv.Sum();
        KALDI_LOG << "Input-deriv sum for input '" << request.inputs[i].name
                  << "' (optimized) is " << in_deriv_opt.Sum();
        if (!ApproxEqual(in_deriv, in_deriv_opt)) {
          KALDI_ERR << "Non-optimized and optimized versions of the computation give "
                    << "different input-derivs.";
        }
      }
    }

    if (!NnetParametersAreIdentical(nnet, nnet_opt, 1.0e-05)) {
      KALDI_ERR << "Neural networks differ after training, between optimized "
                << "and non-optimized computation.";
    }    
  }
}

} // namespace nnet3
} // namespace kaldi

int main() {
  using namespace kaldi;
  using namespace kaldi::nnet3;
  //SetVerboseLevel(2);


  for (int32 loop = 0; loop < 2; loop++) {
#if HAVE_CUDA == 1
    if (loop == 0)
      CuDevice::Instantiate().SelectGpuId("no");
    else
      CuDevice::Instantiate().SelectGpuId("yes");
#endif
    UnitTestNnetOptimize();
  }

  KALDI_LOG << "Nnet tests succeeded.";

  return 0;
}

