# ParallelDataRunner
Linux based parallel data runner (multi threads, multi Process)

# How to use

cc datasplit.c -o datasplit

cat testdata.txt | ./datasplit -n x "inst1 -> inst2"

cat testdata.txt | ./datasplit -n x "inst1 -> inst2 -> inst3"

Exccution example
cat largefile.txt | ./datasplit -n 4 "grep abc -> grep 123 -> awk '{print $1}' -> sort -u -> wc -l"

(Because this program uses stdin, you may need to use pipe redirection for stdin)
```
-n option(integer x) : number of threads. Essential option.
In this execution above, there will be created x threads that process input data.
And one more thread is created, that prints out the result of each threads.
Totally x+1 threads will be created.

inst1, inst2, inst3 are Linux commands like sort -u, grep 123, and so on.
"inst1 -> inst2 -> inst3" means stdin inst1's stdout will be redirected to inst2, and inst2's stdout will be redirected to inst3

On execution of this program, initially there will be created x threads that process data, and 1 thread that print data.
The main thread that created all these threads, will get stdin intput and split the input with '\n' for many strings.
Those strings goes into x threads to be processed.
The x threads will create new processes and replace excutables with inst1, inst2,... by exec() function call.
Then the data flows are like this 
: stdin -> main thread -> i th thread of x threads -> process 1(inst1) -> process 2(inst2) -> i th thread of x threads -> printer thread -> stdout
```
