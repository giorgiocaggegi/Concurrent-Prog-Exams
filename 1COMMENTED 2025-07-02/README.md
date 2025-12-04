# magic-squares-screener

# Operating Systems - UNICT - A.Y. 2024/25

## Exam: Magic Squares Screener (2025-07-02)

### Description

Create a C program that accepts command-line invocations of the following format:

binary &lt;M-verifiers&gt; &lt;file-1&gt; … &lt;file-N&gt;

The program takes as input **N** text files containing descriptions of 3x3 square matrices of numbers and filters out those that represent **magic squares**. A 3x3 matrix is considered a magic square if **all its rows, columns, and both diagonals** contain numbers that sum to the same value (called the **magic total**).

### Examples of Magic Squares

#### Magic total: 15

| 8 | 1 | 6 |
|---|---|---|
| 3 | 5 | 7 |
|---|---|---|
| 4 | 9 | 2 |

#### Magic total: 30

| 16 | 2  | 12 |
|----|----|----|
| 6  | 10 | 14 |
|----|----|----|
| 8  | 18 | 4  |

#### Magic total: 45

| 24 | 3  | 18 |
|----|----|----|
| 9  | 15 | 21 |
|----|----|----|
| 12 | 27 | 6  |

### Program Requirements

At startup, the program must create **N + M threads**:

- **N reader threads**, each responsible for reading one of the input files.  
  - Each line in the file represents a 3x3 matrix of integers in the format:  
    `"36,13,27,9,24,41,18,3,30"`
  - Example input files: `squares-1.txt`, `squares-2.txt`, `squares-3.txt`

- **M verifier threads**, each responsible for checking whether a given 3x3 matrix is a magic square.

### Shared Data Structures

- An **intermediate queue** with a maximum capacity of **10 elements**, containing matrices to verify.
- A **final queue** with a maximum capacity of **3 elements**, containing only verified magic squares.
- **Mutexes and condition variables**. The exact number and usage are to be determined by the student, with a minimal and efficient design.
- Optional **flags or work counters** for signaling completion.

### Thread Behavior

- All **reader threads** operate in parallel. Each one reads the matrices described in its assigned file.  
  For each matrix to verify, a record is created and inserted into the **intermediate queue**.

- All **verifier threads** also operate in parallel. Each one:
  - Extracts a matrix from the intermediate queue.
  - Verifies whether it is a magic square.
  - If valid, inserts it into the **final queue**.

- The **main (parent) thread** is responsible for extracting and displaying the verified magic squares from the final queue.
