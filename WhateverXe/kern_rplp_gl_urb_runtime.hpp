// BSD-3-Clause.
#pragma once
class KernelPatcher;
namespace RPLPGLURBRuntime {
// Called once during kernel setup, after the physical GT/native admission.
void processKernel(KernelPatcher &,bool physicalReady);
}
