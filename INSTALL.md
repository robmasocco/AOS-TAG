# AOS-TAG INSTALLATION INSTRUCTIONS

You have two possibilities: using an automated script to take care of everything or doing things manually with *make*.

## The automatic way

Run *insert.sh*, optionally providing two input arguments like this:

```bash
./insert.sh MAX_TAGS MAX_MSG_SZ
```

The two arguments are custom max number of active instances and message size, and will be passed to *insmod* as module parameters during insertion. If the script is run without them, they will be set to the default values.
Then, the following tasks are performed:

- The kernel modules in *aos-tag/* are compiled and linked with *make*.
- The *SCTH* module is inserted using *insmod*.
- The *AOS-TAG* module is inserted as well, with the two input arguments or their default values passed as module parameters.
- The results of the operations, together with the contents of the parameter pseudofiles in */sys/modules/aos_tag/parameters/*, are displayed.
- A ready to use header named *aos_tag.h* is generated in the current folder, using *sed* to substitute system calls numbers with those got from the aforementioned parameter pseudofiles.

Later, to remove the module, just run *remove.sh*, which executes a couple of *rmmod* commands.
To remove compilation leftovers just type:

```bash
cd aos-tag
make clean
```

## The manual way

The module can be manually built inside *aos-tag/* using *make*. Debug printks can be enabled in many parts of the module by typing:

```bash
make DEBUG=1
```

The Makefile specifies recursive calls to compile *SCTH* and then *AOS-TAG*, so a single command is necessary.
Then, from inside *aos-tag/*, to insert the module you have to run:

```bash
sudo insmod scth/scth.ko  # Optional parameters here.
sudo insmod aos_tag/aos_tag.ko  # Optional parameters here.
```

To remove the modules later:

```bash
sudo rmmod aos_tag
sudo rmmod scth
```

and *make clean* can be used to remove object files and the like from *aos-tag/*.