import sys
import argparse
import re

# TODO: create histogram

DEBUG = False
TIME = True
do_hist = False
stats = True
#verbose = True


class pattern:
    def __init__(self, f, i, j, k):
        self.num_ev = f
        self.banks = i
        self.aggs = j
        self.bias = k
        self.attks = 0
        self.flips = 0
        self.vics = 0
        self.name = ""

        self.tot = 0
        self.a2a = 0
    
    def print_data(self, verbose):
        if verbose:
            if self.aggs / self.banks == 10:
                mark10 = ""
            else:
                mark10 = ""
            print(
            "evsets: ", self.name.strip().ljust(7),
            "%2d"%self.num_ev,
            "bias: ", self.bias,
            "banks: ", "%2d"%self.banks,
            "aggs: ", self.aggs, 
            "numattks: ", self.attks,
            " patterns: " , "%4d"%self.vics,
            "tot. avg: ", "%7.3f"%(self.tot / self.attks),
            "a2a. avg: ", "%7.3f"%(self.a2a / self.attks),
            "xbk: ", "%10.3f"%(self.a2a / self.attks * self.banks),
            " flips: " , "%6d"%self.flips,
            " flips/attk: ", self.flips/self.attks,
            mark10
            )
        else:
            print(
            self.name.strip().ljust(7),
            "%2d"%self.num_ev,
            self.bias,
            "%2d"%self.banks,
            self.aggs, 
            self.attks,
            "%4d"%self.vics,
            "%6d"%self.flips,
            self.flips/self.attks,
            "%7.3f"%(self.tot / self.attks),
            "%7.3f"%(self.a2a / self.attks),
            "%10.3f"%(self.a2a / self.attks * self.banks),
            )

    def add_data(self, vics, flips, tot, a2a, name):
        if DEBUG: print("adding data")
        self.attks +=1
        self.flips +=int(flips)
        self.vics += int(vics)
        self.name = name

        self.tot += int(tot)
        self.a2a += int(a2a)

class hist:
    dist = [0 for i in range(1024)]
    row = [[0 for i in range(1024)] for j in range(32)]

    def parse_dist(self, agg, vic):
        
        if DEBUG: print("agg: ", agg, " vic: ", vic)

        agg_row = int(agg.split('.')[0].strip()[1:])
        agg_bk = int(agg.split('.')[1].strip()[2:])
        vic_row = int(vic.split('.')[0].split(',')[2].strip()[1:])
        vic_bk = int(vic.split('.')[1].strip()[2:])

        dist_row = vic_row - agg_row

        if DEBUG: print("agg_row: ", agg_row, "bk: ", agg_bk, " vic_row: ", vic_row, " bk: ", vic_bk, "dist: ", dist_row)

        self.dist[dist_row] = self.dist[dist_row] + 1
        self.row[vic_bk][vic_row] = self.row[vic_bk][vic_row] + 1

    def print_data(self):
        print("dist ")
        for i in range(len(self.dist)):
            if self.dist[i] != 0: print(i, ": ", self.dist[i])

        print("rows ")
        for i in range(32):
            for j in range(1024):
                if self.row[i][j] != 0: print(i,",", j, ": ", self.row[i][j])
        
#def parse(input_file, pat, time):
def parse(input_file, pat):

    #global do_hist
    #is_bias = False

    with open(input_file,'r') as reader:
        first_line = reader.readline()
        is_bias = bool(re.search("Bias", first_line))
 
    print("\nParsing: ", input_file, " Bias:", is_bias, " Hist: ", do_hist)


    with open(input_file,'r') as reader:
        
        tmp_hist = hist()
        line_num = 1

        #try:
        for line in reader:

            time_tot_ms = -1
            time_a2a_ns = -1

            if DEBUG: print(line)

            if line[0] == '!':
              continue
 
            if is_bias:
                fields = line.split(':')
                bna = fields[1].split(' ')
                bias = int(bna[1])
                aggs = bna[2].split('/')
                num_aggs = len(aggs)
                flip_info = fields[2].split(' ')
            else:
                fields = line.split(':')
                bias = 0
                aggs = fields[0].split('/')
                num_aggs = len(aggs)
                #flip_info = fields[1].split(' ')
                flip_info = fields[2].split(' ')
                evsets = int(''.join(filter(str.isdigit, fields[1])))
                name = ''.join([i for i in fields[1] if not i.isdigit()])

                #if DEBUG: print("time data: ",fields[3])

                try:
                    time_tot_ms = fields[3].split('/')[0]
                    time_a2a_ns = fields[3].split('/')[1]
                except:
                    if DEBUG: print("No time data")
                    pass
 
            bank_list = []
            for agg in aggs:
                try:
                    b = agg.split('.')[1].strip()
                except: 
                    print("Err @", line_num, " ", agg)
                if b not in bank_list:
                    bank_list.append(b)
            num_banks = len(bank_list)
 
            flips = 0
            vics = 0
 
            if len(flip_info) > 2:
                vics = 1
 
                if do_hist:
                   tmp_hist.parse_dist(aggs[0], flip_info[1])
 
                for data in flip_info[1:-1]:
                    entry = data.split(',')
                    bk_num = entry[2].split('.')[1]
                    #if int(bk_num[2:]) >= 16:
                    if True:
                        try:
                            xord = int(entry[0], 16)^int(entry[1], 16)
                        except:
                            print("Err @", line_num, " ", entry)
                        flips += bin(xord).count("1")

            if DEBUG: print("ev:bk:aggs:bi:vic:flip = ", evsets, ":", num_banks, ":", num_aggs, ":", bias, ":", vics, ":", flips)
            #pat[evsets][num_banks][num_aggs][bias].print_data()
            #pat[evsets][num_banks][num_aggs][bias].add_data(vics, flips, name)
            pat[evsets][num_banks][int(num_aggs/num_banks)][bias].add_data(vics, flips, time_tot_ms, time_a2a_ns, name)
            if DEBUG: print("Added to pat")
            line_num = line_num + 1

            #if (time_tot_ms != -1):
            #    time[evsets][num_banks][int(num_aggs/num_banks)][0].add_data(time_tot_ms, time_a2a_ns, name)
                #if vics:
                #    time[evsets][num_banks][num_aggs][1].add_data(time_tot_ms, time_a2a_ns, name)

                #else:
                #    time[evsets][num_banks][num_aggs][0].add_data(time_tot_ms, time_a2a_ns, name)



        
        if do_hist: tmp_hist.print_data()
        #except:
        #    print("Err @", line_num)
        #    print(line)


def main(argv):
    global do_hist
    global stats

    parser = argparse.ArgumentParser(description="Parses output data from tresspass")

    #parser.add_argument('-b', '--bias', required=False, action='store_true', help="set if output specifies bias")
#parser.add_argument('-H', '--hist', required=False, action='store_true', help="set if you want to see histogram")
#parser.add_argument('-a', '--all', required=False, action='store_true', help="Do everything")
    parser.add_argument('-v', '--verbose', required=False, action='store_true', help="print more")
    parser.add_argument('INPUT_FILE_LIST', nargs='+', help="Name of the file to parse")

    args = parser.parse_args()

    #do_hist = args.hist
    #stats = not do_hist
    verbose = args.verbose
    print(verbose)

    #if args.all:
    #    do_hist = True
    #    stats = True


    inputs = args.INPUT_FILE_LIST


    for input in inputs:

        pat = [[[[pattern(f, i, j, k) for k in range(1)] for j in range(30)] for i in range(33)] for f in range(161)]
        #time = [[[[time_data(f, i, j, k) for k in range(2)] for j in range(30)] for i in range(33)] for f in range(161)]
        if DEBUG: print("Running parcer")

        #parse(input, pat, time)
        parse(input, pat)

        sum_flips = 0
        sum_attks = 0
        sum_vics = 0

        for o in pat: 
            for p in o:
                for q in p:
                    for r in q:
                        if r.attks != 0:
                            if stats: r.print_data(verbose)
                            #sum_attks += r.attks
                            #sum_flips += r.flips
                            #sum_vics += r.vics


        #if stats:
        #    print("SumAttacks: ", sum_attks , 
        #    " VulnerablePatterns: ", sum_vics, 
        #    " Flips: " , sum_flips)
        
        #if TIME:
        #    for o in time: 
        #        for p in o:
        #            for q in p:
        #                for r in q:
        #                    if r.attks != 0:
        #                        if stats: r.print_data()

if __name__ == "__main__":
    main(sys.argv)
