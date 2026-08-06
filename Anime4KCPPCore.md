# Anime4KCPP Core

> High-performance C++ image and video upscaling library implementing the Anime4K family of algorithms with multiple hardware acceleration backends.

## Overview

**Anime4KCPP Core** is the computational engine behind Anime4KCPP. It provides a unified C++ framework for high-quality real-time image and video enhancement using both traditional image processing algorithms and modern neural-network-based upscaling.

The library is designed around a modular processing architecture that allows the same API to execute on different hardware accelerators, including:

- CPU
- OpenCL
- CUDA
- NCNN inference engine

The core library can also be integrated into video processing pipelines, GUI applications, command-line tools, or third-party software.

---

# Features

## Multiple Processing Backends

The library supports several execution engines that share a common interface.

### CPU Backend

- Pure C++ implementation
- No GPU required
- SIMD-friendly implementation
- Multi-threaded processing
- Portable across platforms

---

### OpenCL Backend

GPU acceleration through OpenCL.

Features include:

- Cross-vendor GPU support
- Runtime kernel compilation
- Custom OpenCL kernels
- Image processing on GPU memory
- Supports AMD, Intel and NVIDIA hardware

---

### CUDA Backend

Native CUDA implementation for NVIDIA GPUs.

Provides:

- CUDA optimized kernels
- High throughput processing
- Device memory management
- CUDA-specific implementations of Anime4K filters

---

### NCNN Backend

Deep-learning inference using Tencent's NCNN framework.

Supports:

- ACNet neural models
- ONNX-based model loading
- Lightweight inference
- CPU or Vulkan execution (depending on NCNN configuration)

---

# Processing Algorithms

The library contains multiple implementations of Anime4K algorithms.

Examples include:

- Anime4K 0.9
- ACNet
- CNN-based enhancement
- Hybrid image enhancement
- Traditional edge enhancement
- Line reconstruction
- Noise reduction

The architecture allows selecting the appropriate processor depending on the available hardware.

---

# Neural Network Models

The repository includes pretrained models such as:

- ACNet
- ACNetHDNL

The models are distributed as ONNX files and are loaded by the inference backend.

Example model files include:

- ACNetHDNL0.onnx
- ACNetHDNL1.onnx
- ACNetHDNL2.onnx
- ACNetHDNL3.onnx

---

# OpenCL Kernels

The project ships pre-written OpenCL kernels including:

- Anime4K filters
- ACNet kernels
- HDNL kernels

These kernels are compiled at runtime unless built-in kernels are enabled during compilation.

---

# Library Architecture

The project follows a modular object-oriented architecture.

```
Application
      │
      ▼
Anime4KCPP API
      │
      ▼
Processor Interface
      │
 ┌────┼───────────┐
 │    │           │
CPU OpenCL      CUDA
 │    │           │
 └────┼───────────┘
      │
      ▼
Processing Pipeline
      │
      ▼
Image / Video Output
```

---

# Main Components

## Processor

Provides the common interface implemented by every backend.

Responsibilities include:

- loading images
- processing frames
- managing parameters
- backend abstraction

---

## Manager

Responsible for:

- backend initialization
- hardware detection
- resource management
- processor creation

---

## Creator

Factory responsible for constructing processor implementations dynamically.

This keeps user code independent of the selected backend.

---

## Initializer

Performs runtime initialization of processing modules.

Tasks include:

- registering managers
- initializing OpenCL
- initializing CUDA
- loading inference engines

---

## Filter Processors

The library contains reusable filter processors implementing:

- sharpening
- denoising
- edge enhancement
- gradient operations
- color processing

---

## CNN Processors

Dedicated processors perform neural network inference for ACNet models.

These classes encapsulate:

- tensor preparation
- model execution
- output reconstruction

---

# Video Processing

When compiled with video support, the library includes:

- video decoding interfaces
- frame processing
- asynchronous pipelines
- threaded processing
- codec abstraction

Video-related modules include:

- VideoProcessor
- VideoIO
- VideoIOAsync
- VideoIOSerial
- VideoIOThreads
- VideoCodec

This separation allows different execution models without changing application code.

---

# Parallel Processing

The project includes its own parallel execution infrastructure.

Components include:

- thread pool
- task scheduling
- worker management
- configurable parallel library abstraction

The parallel implementation is selected at compile time.

---

# Runtime Configuration

The library exposes compile-time options such as:

- OpenCL support
- CUDA support
- NCNN support
- Video support
- Preview GUI support
- Image I/O support
- Built-in OpenCL kernels
- Fast math optimizations
- Ryzen-specific optimizations
- Eigen support
- Legacy OpenCL API compatibility

---

# Build System

The project uses modern **CMake**.

Features include:

- shared or static libraries
- exported CMake targets
- install rules
- package configuration
- configurable optimization levels

Example:

```cmake
find_package(Anime4KCPPCore REQUIRED)

target_link_libraries(MyApplication
    Anime4KCPP::Anime4KCPPCore)
```

---

# Performance

The library includes a benchmarking helper that measures processing throughput.

The benchmark:

1. Initializes the selected backend
2. Generates random test images
3. Executes warm-up runs
4. Measures processing speed
5. Reports frames per second

This allows direct comparison of:

- CPU
- CUDA
- OpenCL
- NCNN

under identical workloads.

---

# Design Goals

The project emphasizes:

- high performance
- backend independence
- modular architecture
- reusable processing pipeline
- real-time execution
- cross-platform compatibility
- hardware abstraction
- extensibility

---

# Typical Workflow

```
Create Processor
        │
        ▼
Initialize Backend
        │
        ▼
Load Image
        │
        ▼
Execute Anime4K Algorithm
        │
        ▼
Retrieve Enhanced Image
```

For video:

```
Decode Frame
      │
      ▼
Run Anime4K Processor
      │
      ▼
Encode / Display
```

---

# Public API Highlights

The public interface exposes functionality for:

- processor creation
- backend initialization
- parameter configuration
- image loading
- image processing
- benchmarking
- hardware information
- video processing
- exception handling

---

# Directory Structure

```
core/
├── include/
│   ├── AC.hpp
│   ├── ACProcessor.hpp
│   ├── ACCuda.hpp
│   ├── ACOpenCL.hpp
│   ├── ACNCNN.hpp
│   ├── CPUAnime4K09.hpp
│   ├── CudaAnime4K09.hpp
│   ├── OpenCLAnime4K09.hpp
│   ├── VideoProcessor.hpp
│   ├── ThreadPool.hpp
│   ├── Parallel.hpp
│   └── ...
│
├── kernels/
│   ├── Anime4KCPPKernel.cl
│   ├── ACNetKernel.cl
│   └── ...
│
├── models/
│   ├── ACNetHDNL0.onnx
│   ├── ACNetHDNL1.onnx
│   └── ...
│
└── src/
```

---

# Technical Highlights

- Modern C++ implementation
- Object-oriented backend abstraction
- Template-based benchmarking utilities
- Runtime backend registration
- Thread-safe processing infrastructure
- Optional GPU acceleration
- ONNX neural network support
- Runtime OpenCL kernel compilation
- Configurable optimization paths
- CMake package export support
- Installable as a reusable library
- Suitable for both desktop applications and video processing pipelines

---

# Intended Use Cases

Anime4KCPP Core is suitable for:

- Anime upscaling
- Image enhancement
- Video post-processing
- Real-time media players
- Video restoration
- Machine-learning assisted image processing
- GPU image processing research
- Integration into multimedia applications
