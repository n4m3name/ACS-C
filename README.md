# ACS-C
Airline check-in system simulation using C

[GitHub link](https://github.com/n4m3name/ACS-C)

## Use
There are two ways to compile and run the code:
- `make` followed by `./ACS input.txt`: This will run the file using the `unpit.txt` file given in the assignment outline.
- `make rand` followed by `./ACS randinput.txt`: This first generates a randomized input file `randinput` with limits defined in [`inputGen.c`](inputGen.c). It produces well-formed output - negatives/error cases are not tested.

## Design document
Please see the [included design document](DesignDoc.pdf) for in-depth information on the logic of the program.