# sgminer-stripdown


## About

This is a stripped down fork of sgminer, which is being optimised to work with amdgpu-pro under Linux, with a focus
on Ubuntu 16.04.1 in particular. It will probably compile and run on other things, but portability is not a priority
whilst the code is in a state of flux.  

## Major changes

- Removes ADL (it doesn't work with amdgpu-pro)
- Uses jansson-dev from distro; doesn't build jansson by default; option to build with jansson-2.9
- Build without libcurl / gbt / getwork support (not needed for stratum)



- TODO: separate out gbt/getwork code currentlyisolated by ifdefs 

### Implement cmake build system; remove GNU autotools

- builds the project with GCC and GNU make in ~7 seconds
- TODO: config.h
- TODO: install procedures

## Other changes of note

- Removes a few algorithms that were buggy or simply not useful to GPU mining




## Old thinking aloud


This branch is for stripping sgminer down to what I want to keep. The code base is an eclectic mix of many years of 
good work that I feel loath to rip out, but not all of it is useful any more. This is primarily for my own use and 
interest (refreshing some rusty skills) so whilst it'll be on Github, it's not claimed to be fit for or capable of
any given thing.

## Stuff worth keeping

### Core functions

API
Pool configuration + runtime kernel switching

### Algorithms

Ethash
Cryptonight



## Stuff worth losing

### Core functions

ADL doesn't work with current AMD drivers and isn't going to. The buggy code can go, but some of the framework
will be worth saving as it will be possible to implement some of the same functions via sysfs. Prequisite will
be using vendor specific extensions to match OpenCL devices to their topology and thus determine the paths to
their various sysfs interfaces.

### Algorithms

#### Deprecated in function of ASICs or GPU resistance

##### Scrypt

Scrypt ASICs are power efficient; for the cost of a Sapphire RX470 one can now buy ~25-30Mh/s of hosted Scrypt on 
an ASIC at power costs of $0.01 / Mh / day (about 25-33% of gross output as of late 2016). Off hand I don't have
exact hashrate and power figures for RX480s and 470s but neither do I need to; R9 290 GPUS scrape 1Mh/sec and it's
not going to be profitable to mine scrypt on GPUs at vastly higher power costs.

##### X11

Much the same applies to X11; there are benchmarks of RX480s achieving 8.6Mh/s X11 but you can buy 10Mh/s thereof
for a two year term for $60 with power costs included. Not that this is anything to be excited about; the output
of just over 500 Mh/s of X11 hashrate is currently sitting at ~$3 per day (late 2016). Needless to say I won't be
using a 6 GPU rig to mine X11 at $0.30 a day when it can mine Ethereum for more like $6.

##### Yescrypt

This seems fairly effectively GPU resistant; support has been around for a couple of years, but my Core i5 CPU 
and can outperform the results from respectable graphics cards. Either way, the output generated even from my
CPU (at 2.8kh/s) would currently equate to 10.2 cents / day and the electricity cost (based on 45W for the CPU)
to 14.6 cents / day; despite allegedly being the 'best' normalised algo at Zpool. Not something that we want on
the GPU.




## Stuff needing more investigation

### Algorithms

#### Neoscrypt

Not a prime candidate for me but perhaps worth benchmarking. Reported figures for the RX480 range from 370kh/s to
795kh/s, the latter apparently with substantial optimisations. Zpool lists current payout at 2.08mBTc per Mh per 
day, translating to $1.5247 at time of writing. If we can reach and sustain the upper end of that hashrate, a 
six-card rig should produce gross output of $7.27 per day, of which about $3.20 would be lost to power costs for
net returns of about $4/day. For the time being this is not a superior return to Ethereum, but it's not so obviously 
unprofitable as to support killing it off either. Verdict leans towards keep, improve.



Large source files

./sgminer.c 9331 lines
./api.c 3981 lines
./util.c 3203 lines
./config_parser.c 2323 lines
./adl.c 1795 lines
./driver-opencl.c 1653 lines
./algorithm.c 1623 lines
./algorithm/neoscrypt.c 1408 lines
./algorithm/yescrypt-opt.c 1363 lines
./ocl.c 998 lines
./algorithm/sponge.c 742 lines
./algorithm/pluck.c 456 lines
./algorithm/scrypt.c 407 lines
./algorithm/yescryptcommon.c 360 lines
./lib/sigprocmask.c 329 lines
./algorithm/maxcoin.c 321 lines
./api-example.c 313 lines
./algorithm/cryptonight.c 301 lines
./events.c 277 lines
./algorithm/whirlpoolx.c 244 lines
./findnonce.c 234 lines
./algorithm/sia.c 232 lines
./algorithm/bitblock.c 229 lines
./algorithm/x14.c 225 lines
./algorithm/marucoin.c 223 lines

