# Concurrent Programming Exam Solutions  
Academic Year 2024/2025  
University of Catania - Laboratory Module of Operating Systems Course  


## Overview  
This repository contains solutions to the concurrent programming exams for the academic year 2024/2025. The solutions are implemented using the POSIX concurrent API in a GNU/Linux environment.

The synchronization mechanisms used throughout the solutions are:

- **pthreads** (POSIX threads) in all solutions  
- **Semaphores** and **mutexes** in some  
- **Condition variables** and mutexes in others  
- Only **semaphores** in some cases  

Almost all solutions also involve reading and/or writing **plain text** or **binary files** as part of the OS interaction exercize.

A common theme is the **Producer/Consumer** problem, used for implementing necessary data structures, such as stack or queues, and synchronization logic.  
All assignments ask to let the program self-terminate. Consequently, the sync logic includes mechanisms which allowed the consumer to detect when the producer has terminated.
   

## Notes  
- All the solutions were created as time-challenges, using as the maximum time the one indicated in the assignment. Therefore, with the exception of `1COMMENTED 2025-07-02` and `1COMMENTED 2023-07-28`, none of them is commented.
- All exam folders contain a single source file, except for `2024-10-31` and `2024-10-31`, which contain two source files that must be compiled together (the solution and the data structure).   
- `-lpthread` should be added among GCC flags in compiling the files.

## Repository Structure  

- Each exam solution is contained in a separate folder named after the date of the assigned exam.  
- Inside each folder:  
  - `compito_YYYY-MM-DD.pdf`, the exam assignment in PDF format.  
  - Source file(s) with **.c** extension, named according to the exam's instructions.  
  - Sample text/binary files included with exam assignments.
- In the root repository folder:  
  - `lib-misc.h`, a general-purpose header file intended to simulate the exam environment.  


