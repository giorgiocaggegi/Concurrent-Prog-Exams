# multithread-file-duplicator
# Operating Systems - UNICT - A.Y. 2024/25

## Project: Multithreaded File Duplication in C

### Description

Create a C program that accepts command-line invocations of the following format:

binary &lt;file-1&gt; &lt;file-2&gt; ... &lt;file-n&gt; &lt;destination-dir&gt;

The program must duplicate the specified files into the given destination directory.

### Program Requirements

At startup, the program must create **n+1 threads**:

- **n READER-i threads**, each responsible for reading a designated file and passing it, one block at a time, to the WRITER thread.
- **1 WRITER thread**, responsible for writing the received blocks into the duplicated files within the destination directory. It **must not** have direct access to the original files.

### Functionality

- Each READER-i thread works in **parallel** and reads only from its assigned file.
- The data must be transmitted in **1 KiB blocks** via records inserted into a **shared stack**.
- The shared stack has a **fixed capacity of 10 records**.

Each record in the stack must include the following information:

- A buffer of 1024 bytes for the file block.
- The filename.
- The total size of the file.
- The offset of the block within the file (e.g., 0, 1024, 2048, ...).
- The actual size of the data in the buffer (may be less than 1024 bytes).
- An optional end-of-work flag.

### WRITER Thread Responsibilities

- Ensure the destination directory exists. If not, create it.
- For each record extracted from the stack:
  - Create the duplicate file if it doesn't already exist.
  - Write the received block to the correct offset within the file.

### Synchronization

Threads must coordinate using **mutexes and condition variables**. The **minimum number and usage pattern** of synchronization mechanisms is left to the student’s discretion.

### Additional Notes

- All threads must terminate automatically upon completion of their tasks.
- The program must correctly handle **binary** as well as **text** files.