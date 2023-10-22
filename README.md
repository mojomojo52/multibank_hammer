# Mult-bank Hammering

## Quickstart:

1. Adjust hash function in `src/main.c` to your machine.
2. Adjust `row_aggs` in line 766, `src/hammer-suite.c` to the optimal number of aggressors to your memory.
3. Run `make`
4. Run `sudo ./obj/tester -v`

The output defaults to data/test.csv
If output name is set ("-o" option), the date is stamped before the name.

This can be run with 1GB hugepages or Transparent Huge Pages. 
To choose between the two, set `USE_THP`, `USE_1GB` in `src/include/params.h`, accordingly.

```
python3 -v ./parse_result.py ./data/file_name
```
Is used to parse the flip information output data.

Built base on:
[1] "https://github.com/vusec/trrespass"