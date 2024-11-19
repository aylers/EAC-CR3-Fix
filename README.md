# EAC-CR3-Fix

#### Features:
- Retrieves the CR3 value (Directory Table Base) of a given process.
- Kernel-mode operation ensures accurate low-level memory management.


### How To Use?
```c++
        std::cout << driver::CR3() << std::endl; // make a method to call this function from the driver
        uintptr_t local_player = driver::read<uintptr_t>(driver::base_address + 0x18ECF84); // just read the player data or whatever
        //, the cr3 handling is done through the driver
        // call cr3 thing every x seconds or whatever to update
```
