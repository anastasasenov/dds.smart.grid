## Smart Grid Simulation with Eclipse Cyclone DDS

[![License: MIT](https://shields.io)](https://opensource.org)
![AI Assisted](https://shields.io)

A decentralized, distributed smart grid simulation written in C using CycloneDDS. Each grid participant runs as an independent process, discovering other nodes dynamically over DDS and making real-time load-balancing decisions based on lexicographical priority and available power.

### Architecture

{ Consumers } < -- > DDS Network < --- > { Generators/Batteries }

                         |                     
                     Logger/View
                     
    DomainParticipant: DDS domain
    Topic: consumer/generator shared data type
    Publisher / Subscriber: containers
    DataWriter / DataReader: send / receive data objects


### Features

* Decentralized Architecture: No central master controller is required. Nodes publish their state and subscribe to the global grid state via a shared DDS topic. 
* Lexicographical Priority: Consumers and generators are prioritized alphabetically by their unique IDs (e.g., "gen_a" has higher priority than "gen_b").
* Node Roles:
  Generator: Supplies power to meet active demand. Supports 24-hour solar output profiles (--factor24) and finite fuel/runtime capacity (--capacity).
  Consumer: Draws power when sufficient generation or battery capacity is available after higher-priority loads are satisfied.
  Battery: Operates dynamically as either a load (charging during power surplus up to 100%) or a generator (discharging during power deficit down to 0%).
  Logger: Runs in read-only mode (no DataWriter) to monitor and print real-time snapshots of the entire grid.
* Startup / Shutdown Delays: Simulates realistic hardware switching times (--switch) using intermediate STARTING_UP and SHUTTING_DOWN states.


### Prerequisites

* C Compiler: gcc or clang (C99 or later)
* Eclipse Cyclone DDS: Installed with idlc compiler and libddsc development headers/libraries.


### Build Instructions

    Generate C source and header files from the IDL definition:
   ```bash
   idlc SmartGrid.idl
   ```
   This generates `SmartGrid.h` and `SmartGrid.c`.

    Compile the application:
   ```bash
   gcc main.c SmartGrid.c -o smartgrid -lddsc
   ```
   CMakeLists.txt file is included to simplify the build process.

### Command-Line Options

    --consumer <Consumer-id> Run node as a consumer. Sorted lexicographically by ID (first = highest priority).
    --generator <Generator-id> Run node as a generator. Sorted lexicographically by ID (first = highest priority).
    --battery <Battery-id> Run node as a battery (operates as a load or generator depending on grid balance).
    --logger <Logger-id> Run node in read-only mode to monitor the grid state.
    --power <Watt> Nominal power rating of the consumer, generator, or battery in Watts.
    --capacity <Seconds> Total runtime in seconds until fully discharged/depleted.
    --percentage <Percent> Initial battery charge percentage (0 to 100, default: 0%).
    --switch <Seconds> Time in seconds required to start up or shut down the node (default: 1s).
    --factor24 <ListOfPercent> Comma-separated list of 24 hourly solar output percentages (default: 100%).
    --help Display help message and exit.


### Usage

```bash
./smartgrid [options]
```

Example Simulation: Open separate terminal windows to launch multiple nodes and observe how the grid balances itself:

    Grid Logger:
    ```bash
    $ ./smartgrid --logger monitor_1
    ```

    Solar Generator (1000W peak, 2s startup/shutdown time):
    ```bash
    ./smartgrid --generator gen_a --power 1000 --capacity 3600 --switch 2 \
        --factor24 0,0,0,0,0,10,30,50,70,90,100,100,100,90,70,50,30,10,0,0,0,0,0,0
    ```

    Start a Backup Battery (500W, 600s capacity, 50% initial charge):
    ```bash
    ./smartgrid --battery bat_1 --power 500 --capacity 600 --percentage 50 --switch 1
    ```

    Start High-Priority and Low-Priority Consumers:
    ```bash
    ./smartgrid --consumer load_a --power 400 --capacity 1200 --switch 1
    ./smartgrid --consumer load_b --power 800 --capacity 1200 --switch 1
   ```


